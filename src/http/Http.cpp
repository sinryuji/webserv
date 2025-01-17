#include "./Http.hpp"
#include "HttpStatus.hpp"

Http::Http() {}

Http::~Http() {}

HttpResponse Http::processing(const HttpRequest& req, SessionManager& manager) {
  HttpResponse res;

  try {
    checkAndThrowError(req);
    if (req.getLocationConfig().hasReturn()) {
      res.setStatusCode(static_cast<HttpStatus>(req.getLocationConfig().getReturn().first));
      res.getHeader().set(HttpResponseHeader::LOCATION, req.getLocationConfig().getReturn().second);
      return res;
    }
    if (req.isCGI()) res = executeCGI(req, manager);
    else if (req.isMethod(request_method::GET) || req.isMethod(request_method::HEAD))
      res = getMethod(req);
    else if (req.isMethod(request_method::POST))
      res = postMethod(req);
    else if (req.isMethod(request_method::DELETE))
      res = deleteMethod(req);
    else if (req.isMethod(request_method::PUT))
      res = putMethod(req);

    if (res.isSetFd() && fcntl(res.getFd(), F_SETFL, O_NONBLOCK) == -1) {
      logger::error << "method's file fcntl failed" << logger::endl;
      close(res.getFd());
      res.unsetFd();
      throw (INTERNAL_SERVER_ERROR);
    }
  } catch (HttpStatus status) {
    res = getErrorPage(status, req);
  }

  return res;
}

void Http::checkAndThrowError(const HttpRequest& req) {
  if (req.isRecvStatus(HttpRequest::RECEIVE_ERROR))
    throw (req.getErrorStatusCode());
  if (req.getBody().size() > static_cast<size_t>(req.getLocationConfig().getClientMaxBodySize()))
    throw (PAYLOAD_TOO_LARGE);
  if (req.getLocationConfig().isMethodAllowed(req.getMethod()) == false)
    throw (METHOD_NOT_ALLOWED);
}

HttpResponse Http::executeCGI(const HttpRequest& req, SessionManager& sm) {
  std::string                         cgi_ret;
  HttpResponse                        res;
  std::string                         body;
  std::map<std::string, std::string>  header;

  try {
    std::map<std::string, std::string> c = util::splitHeaderField(req.getHeader().get(HttpRequestHeader::COOKIE));
    res.getCGI().initCGI(req, sm.isSessionAvailable(c[SessionManager::SESSION_KEY]));
    res.setCgiStatus(HttpResponse::IS_CGI);
  } catch (std::exception& e) {
    throw INTERNAL_SERVER_ERROR;
  }

  return res;
}

void Http::finishCGI(HttpResponse& res, const HttpRequest& req, SessionManager& sm) {
  std::string                         body;
  std::map<std::string, std::string>  header;

  std::pair<std::string, std::string> p = util::splitHeaderBody(res.getCGI().getCgiResult(), CRLF + CRLF);
  header = util::parseCGIHeader(p.first);
  body = p.second;

  for (std::map<std::string, std::string>::iterator it = header.begin(); it != header.end(); ++it)
    res.getHeader().set(it->first, it->second);

  res.setBody(body);

  std::string setCookieVal = res.getHeader().get(HttpResponseHeader::SET_COOKIE);
  if (setCookieVal != "")
    sm.addSession(setCookieVal, req.getServerConfig().getSessionTimeout());

  const std::string CGI_STATUS = "status";
  std::string statusVal = res.getHeader().get(CGI_STATUS);
  if (statusVal != "") {
    res.setStatusCode(static_cast<HttpStatus>(util::atoi(statusVal)));
    res.getHeader().remove(CGI_STATUS);
  }
  else
    res = Http::getErrorPage(BAD_GATEWAY, req);
}

HttpResponse Http::getMethod(const HttpRequest& req) {
  HttpResponse res;
  struct stat _stat;

  if (stat(req.getTargetPath().c_str(), &_stat) == -1)
    throw NOT_FOUND;

  if (S_ISDIR(_stat.st_mode)) {
    if (req.getLocationConfig().isAutoindex()) {
      res.setStatusCode(OK);
      res.getHeader().set(HttpResponseHeader::CONTENT_TYPE, "text/html");
      res.setBody(generateAutoindex(req));
    }
    else if (req.getLocationConfig().getIndex() != "") {
      try {
        int fd = util::openToRead(req.getTargetPath() + req.getLocationConfig().getIndex());
        res.setFd(fd);
      } catch(util::SystemFunctionException& e) {
        throw (NOT_FOUND);
      }
    }
    else
      throw (NOT_FOUND);
  }
  else if (S_ISREG(_stat.st_mode)) {
    try {
      int fd = util::openToRead(req.getTargetPath());
      res.setFd(fd);
    } catch(util::SystemFunctionException& e) {
      throw (NOT_FOUND);
    }
  }
  else
    throw (FORBIDDEN);

  res.setStatusCode(OK);
  res.getHeader().set(HttpResponseHeader::CONTENT_TYPE, req.getContentType());

  return res;
}

HttpResponse Http::postMethod(const HttpRequest& req) {
  HttpResponse res;
  struct stat _stat;

  if (stat(req.getTargetPath().c_str(), &_stat) == 0) {
    if (S_ISDIR(_stat.st_mode))
      throw (FORBIDDEN);
  }

  try {
    int fd = util::openToWrite(req.getTargetPath());
    res.setFd(fd);
    res.setFileBuffer(req.getBody());
  } catch (util::SystemFunctionException& e) {
    throw (FORBIDDEN);
  }

  res.setStatusCode(CREATED);
  res.getHeader().set(HttpResponseHeader::CONTENT_TYPE, req.getContentType());
  res.getHeader().set(HttpResponseHeader::LOCATION,\
      req.getServerConfig().getServerName()
      + ":"
      + util::itoa(req.getServerConfig().getPort())
      + req.getSubstitutedPath());

  return res;
}

HttpResponse Http::deleteMethod(const HttpRequest& req) {
  HttpResponse res;
  struct stat _stat;

  if (stat(req.getTargetPath().c_str(), &_stat) == 0) {
    if (S_ISDIR(_stat.st_mode))
      throw (FORBIDDEN);
  }
  else
    throw (NOT_FOUND);

  if (std::remove(req.getTargetPath().c_str()) == -1)
    throw (INTERNAL_SERVER_ERROR);

  res.setStatusCode(OK);

  return res;
}

HttpResponse Http::putMethod(const HttpRequest& req) {
  HttpResponse res;
  struct stat _stat;

  if (stat(req.getTargetPath().c_str(), &_stat) == 0) {
    if (S_ISDIR(_stat.st_mode))
      throw (FORBIDDEN);
  }

  try {
    int fd = util::openToWrite(req.getTargetPath());
    res.setFd(fd);
    res.setFileBuffer(req.getBody());
  } catch(util::SystemFunctionException& e) {
    throw (FORBIDDEN);
  }

  res.setStatusCode(NO_CONTENT);

  return res;
}

HttpResponse Http::getErrorPage(HttpStatus status, const HttpRequest& req) {
  HttpResponse          res = HttpResponse();
  std::string           data;
  const LocationConfig& config = req.getLocationConfig();

  std::string errorPagePath = config.getErrorPageTargetPath(status);
  if (errorPagePath.empty())
    res.setBody(generateDefaultErrorPage(status));
  else {
    try {
      int fd = util::openToRead(errorPagePath);
      res.setFd(fd);
      if (fcntl(fd, F_SETFL, O_NONBLOCK) == -1) {
        logger::error << "error page file's fcntl failed" << logger::endl;
        close(fd);
        throw (util::SystemFunctionException());
      }
    } catch (util::SystemFunctionException& e) {
      res.unsetFd();
      res.setBody(generateDefaultErrorPage(status));
    }
  }
  res.setError(true);
  res.setStatusCode(status);
  res.getHeader().set(HttpResponseHeader::CONTENT_TYPE, "text/html");

  return res;
}

std::string Http::generateDefaultErrorPage(HttpStatus s) {
  std::string ret = "<html><head><title>"\
+ util::itoa(s) + " " + getStatusText(s)\
+ "</title></head>\
<body>\
<center><h1>"\
+ util::itoa(s) + " " + getStatusText(s)\
+ "</h1></center>\
<hr><center>webserv/1.0.0</center>\
</body>\
</html>";

  return ret;
}

std::string Http::generateAutoindex(const HttpRequest& req) {
  std::string     ret;
  DIR*            dir;
  struct dirent*  ent;

  if ((dir = opendir(req.getTargetPath().c_str())) == NULL) {
    if (errno == ENOTDIR)
      throw (FORBIDDEN);
    if (errno == ENOENT)
      throw (NOT_FOUND);
    else
      throw (INTERNAL_SERVER_ERROR);
  }
  ret = "<!DOCTYPE html>\
    <html>\
    <head>\
    <style>\
    table { width: 300px; }\
    th { height: 17px; }\
    </style>\
    <title>Index of " + req.getPath() + "</title>\
    </head>\
    <body>\
    <h1>Index of " + req.getPath() + "</h1>\
    <table>";

  while ((ent = readdir(dir)) != NULL) {
    std::string name(ent->d_name);
    if (name == ".")
      continue;
    ret += "<tr><td>";
    if (ent->d_type == DT_DIR) ret += "<a href=" + name + "/>" + name + "/</a></td><td align=\"right\">directory";
    else if (ent->d_type == DT_REG) ret += "<a href=" + name + ">" + name + "</a></td><td align=\"right\">file";
    ret += "</td></tr>\n";
  }
  ret += "</table>\
          </body>\
          </html>";

  closedir(dir);

  return ret;
}
