#include "./Http.hpp"
#include "HttpStatus.hpp"

Http::Http() {}

Http::~Http() {}

HttpResponse Http::processing(const HttpRequest& req, SessionManager& manager) {
  HttpResponse res;

  checkAndThrowError(req);

  if (req.getLocationConfig().isSetReturn()) {
    res.setStatusCode(static_cast<HttpStatus>(req.getLocationConfig().getReturnRes().first));
    res.getHeader().set(HttpResponseHeader::LOCATION, req.getLocationConfig().getReturnRes().second);
    return res;
  }

  try {
    if (req.isCGI()) res = executeCGI(req, manager);
    else if (req.getMethod() == request_method::GET || req.getMethod() == request_method::HEAD) res = getMethod(req);
    else if (req.getMethod() == request_method::POST) res = postMethod(req);
    else if (req.getMethod() == request_method::DELETE) res = deleteMethod(req);
    else if (req.getMethod() == request_method::PUT) res = putMethod(req);
    res.setMethod(req.getMethod());
  } catch (HttpStatus status) {
    res = getErrorPage(status, req);
  }

  return res;
}

void Http::checkAndThrowError(const HttpRequest& req) {
  if (req.getRecvStatus() == HttpRequest::ERROR)
    throw req.getErrorStatusCode();
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
    res.set_cgi_status(HttpResponse::IS_CGI);
    res.getCGI().initCGI(req, sm.isSessionAvailable(c[SessionManager::SESSION_KEY]));
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
  // FIXME:
  for (std::map<std::string, std::string>::iterator it = header.begin(); it != header.end(); ++it) {
    res.getHeader().set(it->first, it->second);

    std::string lower_first = util::toLowerStr(it->first);
    if (lower_first == "status") {
      std::vector<std::string> vs = util::split(it->second, ' ');
      if (vs.size() < 1) throw INTERNAL_SERVER_ERROR;
      res.setStatusCode(static_cast<HttpStatus>(util::atoi(vs[0])));
    }
    else if (lower_first == HttpResponseHeader::SET_COOKIE)
      sm.addSession(it->second, req.getServerConfig().getSessionTimeout());

  }

  res.getHeader().remove("status");
  res.setBody(body);
}

HttpResponse Http::getMethod(const HttpRequest& req) {
  HttpResponse res;
  struct stat _stat;

  if (stat(req.getTargetPath().c_str(), &_stat) == -1)
    throw NOT_FOUND;

  if (S_ISDIR(_stat.st_mode)) {
    if (req.getLocationConfig().isAutoindex()) {
      res.setAutoIndex(true);
      res.setBody(autoindex(req));
    }
    else if (req.getLocationConfig().getIndex() != "")
      res.openToRead(req.getTargetPath() + req.getLocationConfig().getIndex());
    else
      throw NOT_FOUND;
  }
  else if (S_ISREG(_stat.st_mode))
    res.openToRead(req.getTargetPath());
  else
    throw FORBIDDEN;

  return res;
}

HttpResponse Http::postMethod(const HttpRequest& req) {
  HttpResponse res;

  std::ofstream out(req.getTargetPath().c_str(), std::ofstream::out);
  if (!out.is_open()) throw FORBIDDEN;

  out.write(req.getBody().c_str(), req.getBody().length());
  if (out.fail() || out.bad() || out.eof()) throw INTERNAL_SERVER_ERROR;

  res.setStatusCode(CREATED);
  res.getHeader().set(HttpResponseHeader::CONTENT_TYPE, req.getContentType());
  res.getHeader().set(HttpResponseHeader::LOCATION,\
      req.getServerConfig().getServerName()
      + ":"
      + util::itoa(req.getServerConfig().getPort())
      + req.getSubstitutedPath());

  res.setBody(req.getBody());

  return res;
}

HttpResponse Http::deleteMethod(const HttpRequest& req) {
  HttpResponse res;
  struct stat _stat;

  if (stat(req.getTargetPath().c_str(), &_stat) == 0) {
    if (S_ISDIR(_stat.st_mode))
      throw (FORBIDDEN);
  }

  if (std::remove(req.getTargetPath().c_str()) == -1)
    throw NOT_FOUND;

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

  std::ofstream out(req.getTargetPath().c_str(), std::ofstream::out);
  if (!out.is_open()) throw NOT_FOUND;

  out.write(req.getBody().c_str(), req.getBody().length());
  if (out.fail() || out.bad() || out.eof()) throw INTERNAL_SERVER_ERROR;

  res.setStatusCode(NO_CONTENT);

  return res;
}

HttpResponse Http::getErrorPage(HttpStatus status, const HttpRequest& req) {
  HttpResponse          res;
  std::string           data;
  const LocationConfig& config = req.getLocationConfig();

  std::string errorPagePath = config.getErrorPageTargetPath(status);
  if (errorPagePath.empty())
    data = defaultErrorPage(status);
  else {
    try {
      data = HttpDataFecther::readFile(errorPagePath);
    } catch (HttpStatus status) {
      data = defaultErrorPage(status);
    }
  }

  res.setStatusCode(status);
  res.getHeader().set(HttpResponseHeader::CONTENT_TYPE, "text/html");
  res.setBody(data);

  return res;
}

std::string Http::defaultErrorPage(HttpStatus s) {
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

std::string Http::autoindex(const HttpRequest& req) {
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
