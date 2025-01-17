#include "./HttpRequest.hpp"

const size_t HttpRequest::URL_MAX_LENGTH = 2000;

HttpRequest::HttpRequest():
  header(),
  cgi(false),
  recv_status(HEADER_RECEIVE),
  contentLength(0),
  errorStatusCode(BAD_REQUEST)
{}

HttpRequest::~HttpRequest() {}

HttpRequest::HttpRequest(const HttpRequest& obj):
  method(obj.method),
  path(obj.path),
  queryString(obj.queryString),
  version(obj.version),
  body(obj.body),
  header(obj.header),
  sc(obj.sc),
  lc(obj.lc),
  cgi(obj.cgi),
  scriptPath(obj.scriptPath),
  cgiPath(obj.getCGIPath()),
  pathInfo(obj.pathInfo),
  recv_status(obj.recv_status),
  contentLength(obj.contentLength),
  errorStatusCode(obj.errorStatusCode) {
}

HttpRequest& HttpRequest::operator=(const HttpRequest& obj) {
  if (this != &obj) {
    this->method = obj.method;
    this->path = obj.path;
    this->queryString = obj.queryString;
    this->version = obj.version;
    this->body = obj.body;
    this->header = obj.header;
    this->sc = obj.sc;
    this->lc = obj.lc;
    this->cgi = obj.cgi;
    this->scriptPath = obj.scriptPath;
    this->cgiPath = obj.getCGIPath();
    this->pathInfo = obj.pathInfo;
    this->recv_status = obj.recv_status;
    this->contentLength = obj.contentLength;
    this->errorStatusCode = obj.errorStatusCode;
  }

  return *this;
}

void HttpRequest::parse(const std::string& req, const Config& conf) {
  size_t pos;

    // Request line & header
    if ((pos = req.find("\r\n")) != std::string::npos) {
      parseStatusLine(req.substr(0, pos));
      this->header.parse(req.substr(pos + 2));
    }
    else
      parseStatusLine(req);

    // Set config
    std::string host = this->header.get(HttpRequestHeader::HOST);
    this->sc = conf.getHttpConfig().findServerConfig(host);
    this->lc = this->sc.findLocationConfig(this->getPath());

    // setup CGI
    setupCGI();
}

void HttpRequest::parseStatusLine(const std::string& line) {
  std::vector<std::string> vs = util::split(line, ' ');
  if (vs.size() != 3) throw BAD_REQUEST;

  setMethod(vs[0]);
  setURI(vs[1]);
  setVersion(vs[2]);
}

void HttpRequest::validateMethod(const std::string &method) {
  if (method.empty())
    throw BAD_REQUEST;
  if (method != request_method::GET &&
      method != request_method::POST &&
      method != request_method::DELETE &&
      method != request_method::PUT &&
      method != request_method::HEAD)
    throw NOT_IMPLEMENTED;
  this->method = method;
}

void HttpRequest::validateURI(const std::string &path) {
  size_t i;

  i = 0;
  if (path.length() > URL_MAX_LENGTH)     throw URI_TOO_LONG;
  if (path[i++] != '/')                   throw BAD_REQUEST;
  while (i < path.length()) {
    if (!std::isalnum(path[i]) && !strchr(":%._\\+~#?&/=-", path[i]))
      throw BAD_REQUEST;
    ++i;
  }
}

void HttpRequest::validateVersion(const std::string &version) {
  char*                                   left;
  double                                  v;
  std::vector<std::string>                ss = util::split(version, '/');

  if (ss.size() != 2 || ss[0] != "HTTP")  throw BAD_REQUEST;
  v = std::strtod(ss[1].c_str(), &left);
  if (std::strlen(left) > 0)              throw BAD_REQUEST;
  if (v > 1.1)                            throw HTTP_VERSION_NOT_SUPPORTED;
  if (v < 1.1)                            throw UPGRADE_REQUIRED;
}

void HttpRequest::setupCGI() {
  size_t                                        reqPathPos;
  std::string                                   reqPath = getPath();
  std::map<std::string, std::string>::iterator  it;
  std::map<std::string, std::string>            cgi = this->sc.getCGI();

  for (it = cgi.begin(); it != cgi.end(); ++it) {
    std::string targetPath = getTargetPath();
    std::string ext = it->first;
    std::string cgiPath = it->second;
    size_t      targetPathPos;

    if ((reqPathPos = reqPath.find(ext)) != std::string::npos) {
      this->cgi = true;
      if ((targetPathPos = targetPath.find(ext)) != std::string::npos)
        this->scriptPath = targetPath.substr(0, targetPathPos + ext.length());
      else
        throw BAD_REQUEST;
      this->pathInfo = getPath().substr(reqPathPos + ext.length());
      this->cgiPath = cgiPath;
      break;
    }
  }
}

void HttpRequest::unchunkBody(void) {
  std::string       ret;
  std::string       s;
  size_t            s_pos = 0;
  size_t            e_pos;
  bool              is_hex = true;
  size_t            read_size = 0;

  while ((e_pos = this->body.find("\r\n", s_pos)) != std::string::npos) {
    s = this->body.substr(s_pos, e_pos - s_pos);

    if (is_hex == true)
      read_size = strToHex(s);
    else if (read_size == 0)
      break;
    else if (read_size == s.size())
      ret += s;
    else
      throw (BAD_REQUEST);
    s_pos = e_pos + 2;
    is_hex = !is_hex;
  }

  this->body = ret;
}

unsigned int HttpRequest::strToHex(std::string s) {
  unsigned int      ret;
  std::stringstream ss(s);

  ss >> std::hex >> ret;

  return ret;
}

/*
 * -------------------------- Getter -------------------------------
 */

std::string HttpRequest::getMethod() const { return this->method; }

bool HttpRequest::isMethod(std::string method) const {
  if (this->method == method)
    return true;
  return false;
}

std::string HttpRequest::getPath() const { return this->path; }

std::string HttpRequest::getSubstitutedPath() const {
  std::string root_path = getLocationConfig().getRoot();
  std::string alias_path = getLocationConfig().getAlias();
  std::string req_path = getPath();
  std::string loc_path = getLocationConfig().getPath();
  size_t      loc_path_len = getLocationConfig().getPath().length();

  if (req_path.find(loc_path) != std::string::npos) {

    if (alias_path != "" && req_path != "/" && (req_path.length() == loc_path_len || req_path[loc_path_len] == '/')) {
      if (loc_path != req_path && alias_path == "/")
        alias_path = "";
      req_path.replace(req_path.find(loc_path), loc_path_len, alias_path);
    }

    if (loc_path == "/" && alias_path != "/") {
      req_path = alias_path + req_path;
    }

  }
  req_path = util::combinePath(root_path, req_path);

  return req_path;
}

std::string HttpRequest::getTargetPath() const { return "." + getSubstitutedPath(); }

std::string HttpRequest::getQueryString() const { return this->queryString; }

std::string HttpRequest::getVersion() const { return this->version; }

const HttpRequestHeader& HttpRequest::getHeader() const { return this->header; }

std::string HttpRequest::getBody() const {
  return this->body;
}

const std::string HttpRequest::getContentType(void) const {
  std::string content_type;
  std::string path;
  MimeType    mt;
  struct stat _stat;

  content_type = getHeader().get(HttpRequestHeader::CONTENT_TYPE);
  if (content_type != "")
    return content_type;

  if (stat(this->getTargetPath().c_str(), &_stat) == -1)
    path = this->getTargetPath();
  else if (S_ISDIR(_stat.st_mode) && this->getLocationConfig().getIndex() != "")
    path = this->getTargetPath() + this->getLocationConfig().getIndex();
  else
    path = this->getTargetPath();

  return mt.getMimeType(path);
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

bool HttpRequest::isRecvStatus(HttpRequest::recvStatus rs) const {
  if (this->recv_status == rs)
    return true;
  return false;
}

int HttpRequest::getContentLength() const {
  return this->contentLength;
}

HttpStatus HttpRequest::getErrorStatusCode() const {
  return this->errorStatusCode;
}

/*
 * -------------------------- Setter -------------------------------
 */

void HttpRequest::setBody(const std::string& body) { this->body = body; }

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

void HttpRequest::setRecvStatus(recvStatus status) {
  this->recv_status = status;
}

void HttpRequest::setContentLength(int len) {
  this->contentLength = len;
}

void HttpRequest::setError(HttpStatus status) {
  this->errorStatusCode = status;
  this->recv_status = RECEIVE_ERROR;
}

void HttpRequest::setConnection(HttpRequestHeader::connection c) {
  this->header.setConnection(c);
}
