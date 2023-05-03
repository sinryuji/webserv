#include "./HttpRequest.hpp"

const size_t HttpRequest::URL_MAX_LENGTH = 2000;

HttpRequest::HttpRequest(): cgi(false) {}

//HttpRequest::HttpRequest(const HttpRequest& obj):
//  method(obj.method),
//  path(obj.path),
//  queryString(obj.queryString),
//  version(obj.version),
//  body(obj.body),
//  field(obj.field),
//  locationConfig(obj.locationConfig),
//  serverConfig(obj.serverConfig),
//  cgi(obj.isCGI()),
//  scriptPath(obj.getScriptPath()),
//  cgiPath(obj.getCGIPath()),
//  pathInfo(obj.getPathInfo()) {}

//HttpRequest& HttpRequest::operator=(const HttpRequest& obj) {
//  if (this != &obj) {
//    this->method = obj.method;
//    this->path = obj.path;
//    this->queryString = obj.queryString;
//    this->version = obj.version;
//    this->body = obj.body;
//    this->field = obj.field;
//    this->locationConfig = obj.locationConfig;
//    this->serverConfig = obj.serverConfig;
//    this->cgi = obj.cgi;
//    this->scriptPath = obj.scriptPath;
//    this->cgiPath = obj.getCGIPath();
//    this->pathInfo = obj.pathInfo;
//  }
//  return *this;
//}

HttpRequest::~HttpRequest() {}

void HttpRequest::parse(std::string request) {
  size_t pos;

  if ((pos = request.find(CRLF + CRLF)) != std::string::npos) {
    parseHeader(request.substr(0, pos));
    setBody(request.substr(pos + (CRLF + CRLF).length()));
  }

  checkCGI(getPath(), getServerConfig());
}

void HttpRequest::parseHeader(const std::string& h) throw(HttpStatus) {
  std::vector<std::string>            vs;
  std::vector<std::string>::iterator  it;

  vs = util::split(h, CRLF);
  it = vs.begin();
  parseStatusLine(*it);
  while (++it != vs.end()) {
    std::pair<std::string, std::string> ret = splitField(*it);
    this->field.insert(ret);
  }
}

void HttpRequest::parseStatusLine(const std::string& line) {
  std::vector<std::string> vs = util::split(line, " ");
  if (vs.size() != 3) throw BAD_REQUEST;

  setMethod(vs[0]);
  setURI(vs[1]);
  setVersion(vs[2]);
}

//void HttpRequest::parseCookie(const std::string& cookie) {
//  std::vector<std::string> vs;
//
//  vs = util::split(cookie, ';');
//  for (std::vector<std::string>::iterator it = vs.begin(); it != vs.end(); ++it) {
//    std::vector<std::string> vvs = util::split(*it, '=');
//
//    std::string key;
//    std::string value;
//
//    key = util::trimSpace(vvs[0]);
//    value = util::trimSpace(vvs[1]);
//    this->cookieMap.insert(std::make_pair(key, value));
//  }
//}

void HttpRequest::validateMethod(const std::string &method) {
  if (method != request_method::GET &&
      method != request_method::POST &&
      method != request_method::DELETE &&
      method != request_method::PUT)
    throw METHOD_NOT_ALLOWED;
  this->method = method;
}

void HttpRequest::validateURI(const std::string &path) {
  size_t i;

  i = 0;
  if (path.length() > URL_MAX_LENGTH)     throw URI_TOO_LONG;
  if (path[i++] != '/')                   throw BAD_REQUEST;
  while (i < path.length()) {
    if (!std::isalnum(path[i]) && !std::strchr(":%._\\+~#?&/=-", path[i]))
      throw BAD_REQUEST;
    ++i;
  }
}

void HttpRequest::validateVersion(const std::string &version) {
  char*                                   left;
  double                                  v;
  std::vector<std::string>                ss = util::split(version, "/");

  if (ss.size() != 2 || ss[0] != "HTTP")  throw BAD_REQUEST;
  v = std::strtod(ss[1].c_str(), &left);
  if (std::strlen(left) > 0)              throw BAD_REQUEST;
  if (v > 1.1)                            throw HTTP_VERSION_NOT_SUPPORTED;
  if (v < 1.1)                            throw UPGRADE_REQUIRED;
}

std::pair<std::string, std::string> HttpRequest::splitField(const std::string& line) {
  std::string field;
  std::string value;
  size_t pos;

  if ((pos = line.find(":")) == std::string::npos) throw BAD_REQUEST;
  field = util::toLowerStr(util::trimSpace(line.substr(0, pos)));
  value = util::trimSpace(line.substr(pos + 1));

  return std::make_pair(field, value);
}

void HttpRequest::checkCGI(const std::string& path, const ServerConfig& sc) {
  std::map<std::string, std::string>::iterator it;
  std::map<std::string, std::string> cgi = sc.getCGI();
  size_t pos;

  for (it = cgi.begin(); it != cgi.end(); ++it) {
    if ((pos = path.find(it->first)) != std::string::npos) {
      this->cgi = true;
      this->scriptPath = getRelativePath().substr(0, pos + it->first.length() + 1);
      this->pathInfo = getPath().substr(pos + it->first.length());
      this->cgiPath = it->second;
      break;
    }
  }
}

/*
 * -------------------------- Getter -------------------------------
 */

std::string HttpRequest::getMethod() const { return this->method; }

std::string HttpRequest::getPath() const { return this->path; }

std::string HttpRequest::getRelativePath() const {
  std::string root_path = getLocationConfig().getRoot();
  std::string req_path = getPath();
  std::string loc_path = getLocationConfig().getPath();
  size_t      loc_path_len = getLocationConfig().getPath().length();

  if (req_path.find(loc_path) != std::string::npos) {

    if (req_path.length() == loc_path_len || req_path[loc_path_len] == '/') {
      req_path.replace(req_path.find(loc_path), loc_path_len, root_path);
    }

    if (loc_path == "/" && root_path != "/") {
      req_path = root_path + req_path;
    }

  }

  return "." + req_path;
}

std::string HttpRequest::getQueryString() const { return this->queryString; }

std::string HttpRequest::getVersion() const { return this->version; }

std::string HttpRequest::getField(const std::string& field) const {
  std::map<std::string, std::string>::const_iterator it;
  std::string                                        ret;

  if ((it = this->field.find(util::toLowerStr(field))) != this->field.end())
    ret = it->second;

  return ret;
}

std::string HttpRequest::getBody() const {
  return this->body;
}

const std::string HttpRequest::getContentType(void) const {
  if (this->field.find(header_field::CONTENT_TYPE) != this->field.end())
    return this->field.at(header_field::CONTENT_TYPE);

  MimeType mt;
  return mt.getMimeType(getPath());
}

const LocationConfig& HttpRequest::getLocationConfig() const {
  return this->lc;
}

const ServerConfig& HttpRequest::getServerConfig() const {
  return this->sc;
}

bool HttpRequest::isCGI() const {
  return this->cgi;
}

const std::string HttpRequest::getScriptPath() const {
  return this->scriptPath;
}

const std::string HttpRequest::getCGIPath() const {
  return this->cgiPath;
}

const std::string HttpRequest::getPathInfo() const {
  return this->pathInfo;
}

/*
 * -------------------------- Setter -------------------------------
 */

#include "../Logger.hpp"

void HttpRequest::setBody(const std::string& body) { this->body = body; }

void HttpRequest::setConfig(const ServerConfig& conf) {
  this->sc = conf;
  log::debug << "this server server_name:" << conf.getServerName() << log::endl;
  this->lc = conf.findLocationConfig(this->getPath());
}

void HttpRequest::setURI(const std::string& URI) {
  validateURI(URI);

  size_t pos = (URI.find('?'));
  this->path = URI.substr(0, pos);
  if (pos != std::string::npos) this->queryString = URI.substr(pos + 1);
}

void HttpRequest::setMethod(const std::string& method) {
  validateMethod(method);

  this->method = method;
}

void HttpRequest::setVersion(const std::string& version) {
  validateVersion(version);

  this->version = version;
}

//void HttpRequest::parseCacheControl(const std::string &s) {
//}
//
//void HttpRequest::parseHost(const std::string &s) {
//}
//
//void HttpRequest::parsePragma(const std::string &s) {
//}
//
//void HttpRequest::parseRange(const std::string &s) {
//}
//
//void HttpRequest::parseTE(const std::string &s) {
//}
//
//void HttpRequest::parseExpect(const std::string &s) {
//}
//
//void HttpRequest::parseMaxForwards(const std::string &s) {
//}
//
//void HttpRequest::parseIfMatch(const std::string &s) {
//}
//
//void HttpRequest::parseIfNoneMatch(const std::string &s) {
//}
//
//void HttpRequest::parseIfModifiedSince(const std::string &s) {
//}
//
//void HttpRequest::parseIfUnmodifiedSince(const std::string &s) {
//}
//
//void HttpRequest::parseIfRange(const std::string &s) {
//}
//
//void HttpRequest::parseAccept(const std::string &s) {
//}
//
//void HttpRequest::parseAcceptCharset(const std::string &s) {
//}
//
//void HttpRequest::parseAcceptEncoding(const std::string &s) {
//}
//
//void HttpRequest::parseAcceptLanguage(const std::string &s) {
//}
//
//void HttpRequest::parseAuthorization(const std::string &s) {
//}
//
//void HttpRequest::parseProxyAuthorization(const std::string &s) {
//}
//
//void HttpRequest::parseFrom(const std::string &s) {
//}
//
//void HttpRequest::parseReferer(const std::string &s) {
//}
//
//void HttpRequest::parseUserAgent(const std::string &s) {
//}
