#include "./Server.hpp"

const size_t        Server::BIND_MAX_TRIES = 10;
const size_t        Server::LISTEN_MAX_TRIES = 10;
const size_t        Server::TRY_SLEEP_TIME = 5;
const size_t        Server::BUF_SIZE = 1024 * 16;
const size_t        Server::MANAGE_FD_MAX = 1024;
const std::string   Server::HEADER_DELIMETER = "\r\n\r\n";
const std::string   Server::CHUNKED_DELIMETER = "0\r\n\r\n";

/*
 * ==============================================
 *                 Constructor
 * ==============================================
 */

Server::Server(Config& config) :
  fdMax(-1),
  config(config),
  connection(config),
  sessionManager() {
    FD_ZERO(&this->reads);
    FD_ZERO(&this->writes);
    FD_ZERO(&this->listens);
}

/*
 * ==============================================
 *                 Destructor
 * ==============================================
 */

Server::~Server() {
  for (size_t i = 0; i < this->listens_fd.size(); ++i) {
    if (close(this->listens_fd[i]) == -1)
      logger::warning << "Closed, server(" << i << ") with -1" << logger::endl;
    else
      logger::info << "Closed, server(" << i << ")" << logger::endl;
  }

  logger::info << "Server closed" << logger::endl;
}

/*
 * ==============================================
 *                   Public
 * ==============================================
 */

void Server::run() {
  setup_server();
  logger::info << "Server setup done" << logger::endl;
  logger::info << "Server is running..." << logger::endl;
  loop();
}

/*
 * ==============================================
 *                 Server core
 * ==============================================
 */

void Server::setup_server() {
  std::vector<ServerConfig> servers = this->config.getHttpConfig().getServerConfig();
  for (std::vector<ServerConfig>::iterator sit = servers.begin(); sit != servers.end(); ++sit) {
    sockaddr_in sock;
    int         fd;

    fd = init_socket();
    this->listens_fd.push_back(fd);

    init_sockaddr_in(sit->getHost(), sit->getPort(), sock);
    open_socket(fd, sock);

    ft_fd_set(fd, this->reads);
    ft_fd_set(fd, this->listens);
  }
}

void Server::loop() {
  struct timeval t;

  t.tv_sec = 1;
  t.tv_usec = 0;
  while (1) {

    fd_set readsCpy = this->reads;
    fd_set writesCpy = this->writes;

    if (select(this->fdMax + 1, &readsCpy, &writesCpy, 0, &t) == -1) {
      logger::error << "Select returns -1, break" << logger::endl;
      break;
    }

    cleanUpConnection();

    for (int i = 0; i < this->fdMax + 1; i++) {
      if (FD_ISSET(i, &this->writes)) {
        if (FD_ISSET(i, &writesCpy)) {
          if (isFileFd(i))
            writeFile(i);
          else if (isCgiPipe(i))
            writeCGI(i);
          else {
            HttpResponse::SendStatus send_status = this->responses[i].getSendStatus();
            if (send_status == HttpResponse::SENDING)
              sendData(i);
            else if (send_status == HttpResponse::DONE) {
              if (this->requests[i].getHeader().getConnection() == HttpRequestHeader::CLOSE)
                closeConnection(i);
              else
                keepAliveConnection(i);
            }
          }
        }
      }
      else if (FD_ISSET(i, &readsCpy)) {
        if (isFileFd(i))
          readFile(i);
        else if (isCgiPipe(i))
          readCGI(i);
        else if (FD_ISSET(i, &this->listens))
          acceptConnect(i);
        else
          receiveData(i);
      }
    }
  }
}

/*
 * ==============================================
 *             Interact with client
 * ==============================================
 */

// Receive

void Server::acceptConnect(int server_fd) {
  struct sockaddr_in  client_addr;
  socklen_t           size;

  size = sizeof(client_addr);
  int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &size);
  if (client_fd == -1) {
    logger::warning << "accept failed" << logger::endl;
    return;
  }
  if (fcntl(client_fd, F_SETFL, O_NONBLOCK) == -1) {
    close(client_fd);
    logger::warning << "accept fcntl failed" << logger::endl;
    return;
  }

  this->requests.insert(std::make_pair(client_fd, HttpRequest()));
  this->responses.insert(std::make_pair(client_fd, HttpResponse()));
  this->recvs.insert(std::make_pair(client_fd, std::string()));

  ft_fd_set(client_fd, this->reads);

  logger::info << "Accept, client(" << client_fd << ", " << inet_ntoa(client_addr.sin_addr) << ":" << ntohs(client_addr.sin_port) << ") into (" << server_fd << ")" << logger::endl;
  this->connection.update(client_fd, Connection::HEADER);

  return ;
}

void Server::receiveData(int client_fd) {
  static char buf[BUF_SIZE + 1];
  int         recv_size;

  recv_size = recv(client_fd, buf, BUF_SIZE, 0);
  if (recv_size <= 0) {
    if (recv_size < 0)
      logger::warning << "recv_size < 0 with client(" << client_fd  << ")" << logger::endl;
    closeConnection(client_fd);
    return ;
  }
  buf[recv_size] = 0;
  logger::debug << "recv_size(" << client_fd << "): " << recv_size << logger::endl;
  this->recvs[client_fd] += std::string(buf, recv_size);
  logger::debug << "total(" << client_fd << "): " << this->recvs[client_fd].length() << logger::endl;
  checkReceiveDone(client_fd);
}

void Server::checkReceiveDone(int client_fd) {
  HttpRequest& req = this->requests[client_fd];

  if (req.isRecvStatus(HttpRequest::HEADER_RECEIVE))
    receiveHeader(client_fd, req);

  if (req.isRecvStatus(HttpRequest::BODY_RECEIVE)) {
    if (req.getHeader().getTransferEncoding() == HttpRequestHeader::CHUNKED) {

      size_t pos = this->recvs[client_fd].find(CHUNKED_DELIMETER);
      if (pos != std::string::npos) {
        req.setBody(this->recvs[client_fd].substr(0, pos));
        req.setRecvStatus(HttpRequest::RECEIVE_DONE);
        try {
          req.unchunkBody();
        } catch (HttpStatus s) {
          logger::debug << "Chunked message is wrong" << logger::endl;
          req.setError(s);
        }
      }

    }
    else if (req.getHeader().getTransferEncoding() == HttpRequestHeader::UNSET) {

      if (req.getContentLength() <= static_cast<int>(recvs[client_fd].length())) {
        req.setBody(this->recvs[client_fd].substr(0, req.getContentLength()));
        req.setRecvStatus(HttpRequest::RECEIVE_DONE);
      }

    }
  }

  if (req.isRecvStatus(HttpRequest::RECEIVE_DONE) || req.isRecvStatus(HttpRequest::RECEIVE_ERROR)) {
    this->recvs.erase(client_fd);
    this->responses[client_fd] = Http::processing(this->requests[client_fd], this->sessionManager);
    prepareIO(client_fd);
  }
}

void Server::receiveHeader(int client_fd, HttpRequest& req) {
  size_t pos = this->recvs[client_fd].find(HEADER_DELIMETER);

  if (pos != std::string::npos) {
    std::string header = this->recvs[client_fd].substr(0, pos);
    std::string left = this->recvs[client_fd].substr(pos + HEADER_DELIMETER.length());
    this->recvs[client_fd] = left;

    try {
      req.parse(header, this->config);
      logger::info << "Request from " << client_fd << " to " << req.getServerConfig().getServerName() << ", Method=\"" << req.getMethod() << "\" URI=\"" << req.getPath() << "\"" << logger::endl;
      this->connection.update(client_fd, Connection::BODY);
    } catch (HttpStatus s) {
      logger::debug << "Request header message is wrong" << logger::endl;
      req.setError(s);
      return;
    }

    if (req.getHeader().getTransferEncoding() == HttpRequestHeader::CHUNKED)
      req.setRecvStatus(HttpRequest::BODY_RECEIVE);
    else {
      std::string contentLength = req.getHeader().get(HttpRequestHeader::CONTENT_LENGTH);
      if (contentLength.empty())
        req.setRecvStatus(HttpRequest::RECEIVE_DONE);
      else {
        req.setContentLength(util::atoi(contentLength));
        req.setRecvStatus(HttpRequest::BODY_RECEIVE);
      }
    }
  }
}

// I/O

void Server::prepareIO(int client_fd) {
  HttpRequest&  req = this->requests[client_fd];
  HttpResponse& res = this->responses[client_fd];

  if (res.getCgiStatus() == HttpResponse::IS_CGI) {
    CGI& cgi = res.getCGI();
    this->connection.updateGateway(client_fd, req.getServerConfig());
    ft_fd_set(cgi.getWriteFD(), this->writes);
    cgi_map.insert(std::make_pair(cgi.getWriteFD(), client_fd));
  }
  else if (res.getCgiStatus() == HttpResponse::NOT_CGI) {
    this->connection.update(client_fd, Connection::SEND);
    if (res.isSetFd()) {
      int fileFd = res.getFd();
      file_map.insert(std::make_pair(fileFd, client_fd));
      if (isReadFd(req, res))
        ft_fd_set(fileFd, this->reads);
      else
        ft_fd_set(fileFd, this->writes);
    }
    else
      postProcessing(client_fd);
  }
}

bool Server::isReadFd(const HttpRequest& req, const HttpResponse& res) {
  if (res.isError() || req.isMethod(request_method::GET) || req.isMethod(request_method::HEAD))
    return true;
  return false;
}

// Send

void Server::postProcessing(int client_fd) {
  HttpRequest&  req = this->requests[client_fd];
  HttpResponse& res = this->responses[client_fd];

  if (req.getMethod() == request_method::HEAD) {
    res.getHeader().remove(HttpRequestHeader::CONTENT_TYPE);
    res.removeBody();
  }
  addExtraHeader(client_fd, req, res);
  ft_fd_set(client_fd, this->writes);
  logger::info << "Response to " << client_fd << " from " << req.getServerConfig().getServerName() << ", Status=" << res.getStatusCode() << logger::endl;
}

void Server::addExtraHeader(int client_fd, HttpRequest& req, HttpResponse& res) {
  // connection
  if (req.getHeader().getConnection() == HttpRequestHeader::KEEP_ALIVE) {
    int timeout = req.getServerConfig().getKeepAliveTimeout();
    int req_max = this->connection.updateRequests(client_fd, req.getServerConfig());
    if (req_max > 0) {
      res.getHeader().set(HttpResponseHeader::CONNECTION, "keep-alive");
      res.getHeader().set(HttpResponseHeader::KEEP_ALIVE, "timeout=" + util::itoa(timeout) + ", max=" + util::itoa(req_max));
    }
    else
      req.setConnection(HttpRequestHeader::CLOSE);
  }

  if (req.getHeader().getConnection() == HttpRequestHeader::CLOSE)
    res.getHeader().set(HttpResponseHeader::CONNECTION, "close");

  // allow
  if (res.getStatusCode() == METHOD_NOT_ALLOWED)
    res.getHeader().set(HttpResponseHeader::ALLOW, req.getLocationConfig().toStringLimitExcept());

  // upgrade
  if (res.getStatusCode() == UPGRADE_REQUIRED)
    res.getHeader().set(HttpResponseHeader::UPGRADE, "HTTP/1.1");
}

void Server::sendData(int client_fd) {
  HttpResponse& res = this->responses[client_fd];
  std::string   data = res.toString();
  int           send_size;

  if ((send_size = send(client_fd, data.c_str(), data.length(), 0)) <= 0) {
    if (send_size == -1)
      logger::warning << "send_size < 0 with client(" << client_fd  << ")" << logger::endl;
    closeConnection(client_fd);
  }
  else
    res.addSendLength(send_size);
}

/*
 * ==============================================
 *                   Connection
 * ==============================================
 */

void Server::closeConnection(int client_fd) {
  HttpResponse& res = this->responses[client_fd];

  ft_fd_clr(client_fd, this->writes);
  ft_fd_clr(client_fd, this->reads);
  if (client_fd == this->fdMax)
    --this->fdMax;
  if (close(client_fd) == -1)
    logger::warning << "Closed, client(" << client_fd << ") with -1" << logger::endl;
  else
    logger::info << "Closed, client(" << client_fd << ")" << logger::endl;
  this->connection.remove(client_fd);
  this->connection.removeRequests(client_fd);

  if (res.isSetFd()) {
    close(res.getFd());
    ft_fd_clr(res.getFd(), this->reads);
    ft_fd_clr(res.getFd(), this->writes);
    file_map.erase(res.getFd());
  }

  if (res.getCgiStatus() == HttpResponse::IS_CGI) {
    CGI& cgi = res.getCGI();

    cgi.withdrawResource();
    ft_fd_clr(cgi.getReadFD(), this->reads);
    ft_fd_clr(cgi.getWriteFD(), this->writes);
    cgi_map.erase(cgi.getReadFD());
    cgi_map.erase(cgi.getWriteFD());
  }

  this->requests.erase(client_fd);
  this->responses.erase(client_fd);
  this->recvs.erase(client_fd);
}

void Server::keepAliveConnection(int client_fd) {
  ft_fd_clr(client_fd, this->writes);

  this->connection.updateKeepAlive(client_fd, this->requests[client_fd].getServerConfig());

  this->requests.erase(client_fd);
  this->responses.erase(client_fd);
  this->recvs.erase(client_fd);
}

/*
 * ==============================================
 *                     Timeout
 * ==============================================
 */

void Server::cleanUpConnection() {
  std::set<int> fd_list;

  fd_list = this->connection.getTimeoutList();
  for (std::set<int>::iterator it = fd_list.begin(); it != fd_list.end(); ++it) {
    int           fd = *it;
    HttpRequest&  req = this->requests[fd];
    HttpResponse& res = this->responses[fd];
    std::string   what;

    if (req.isRecvStatus(HttpRequest::HEADER_RECEIVE) || req.isRecvStatus(HttpRequest::BODY_RECEIVE)) {
      what = "Request ";

      ft_fd_clr(fd, this->reads);
      res = Http::getErrorPage(REQUEST_TIMEOUT, req);
      req.setConnection(HttpRequestHeader::CLOSE);
      prepareIO(fd);
    }
    else if (res.getCgiStatus() == HttpResponse::IS_CGI) {
      what = "Gateway ";
      CGI& cgi = res.getCGI();

      this->cgi_map.erase(cgi.getReadFD());
      this->cgi_map.erase(cgi.getWriteFD());
      ft_fd_clr(cgi.getReadFD(), this->reads);
      ft_fd_clr(cgi.getWriteFD(), this->writes);

      cgi.withdrawResource();

      res = Http::getErrorPage(GATEWAY_TIMEOUT, req);
      req.setConnection(HttpRequestHeader::CLOSE);
      res.setCgiStatus(HttpResponse::NOT_CGI);
      prepareIO(fd);
    }
    else
      closeConnection(fd);
    logger::debug << what << "Timeout, client(" << fd << ")" << logger::endl;
  }
}

/*
 * ==============================================
 *                   CGI I/O
 * ==============================================
 */

bool Server::isCgiPipe(int fd) const {
  std::map<int, int>::const_iterator it;

  it = this->cgi_map.find(fd);
  if (it == this->cgi_map.end())
    return false;
  return it->second;
}

void Server::writeCGI(int fd) {
  int   client_fd  = cgi_map[fd];
  CGI&  cgi = this->responses[client_fd].getCGI();

  int write_size = cgi.writeCGI();
  if (write_size <= 0) {
    ft_fd_clr(fd, this->writes);
    cgi_map.erase(fd);
    if (write_size < 0) {
      logger::error << "cgi write error" << logger::endl;
      cgi.withdrawResource();
      this->responses[client_fd] = Http::getErrorPage(INTERNAL_SERVER_ERROR, this->requests[client_fd]);
      prepareIO(client_fd);
    }
    else {
      lseek(fd, 0, SEEK_SET);
      try {
        cgi.forkCGI();
        ft_fd_set(cgi.getReadFD(), this->reads);
        cgi_map.insert(std::make_pair(cgi.getReadFD(), client_fd));
      } catch (HttpStatus s) {
        this->responses[client_fd] = Http::getErrorPage(s, this->requests[client_fd]);
        prepareIO(client_fd);
      }
    }
  }
}

void Server::readCGI(int fd) {
  int   client_fd = cgi_map[fd];
  CGI&  cgi = this->responses[client_fd].getCGI();

  int read_size = cgi.readCGI();
  if (read_size <= 0) {
    ft_fd_clr(fd, this->reads);
    cgi_map.erase(fd);
    cgi.withdrawResource();
    if (read_size < 0) {
      logger::error << "cgi read error" << logger::endl;
      this->responses[client_fd] = Http::getErrorPage(INTERNAL_SERVER_ERROR, this->requests[client_fd]);
      prepareIO(client_fd);
    }
    else {
      Http::finishCGI(this->responses[client_fd], this->requests[client_fd], this->sessionManager);
      postProcessing(client_fd);
    }
  }
}

/*
 * ==============================================
 *                   File I/O
 * ==============================================
 */

bool Server::isFileFd(int fd) const {
  std::map<int, int>::const_iterator it;

  it = this->file_map.find(fd);
  if (it == this->file_map.end())
    return false;
  return it->second;
}

void Server::writeFile(int fd) {
  int           client_fd = this->file_map[fd];
  HttpResponse& res = this->responses[client_fd];
  HttpRequest&  req = this->requests[client_fd];
  int           writeSize;

  std::string data = res.getFileBufferOffSet();
  writeSize = write(fd, data.c_str(), data.length());
  if (writeSize <= 0) {
    ft_fd_clr(fd, this->writes);
    this->file_map.erase(fd);
    close(fd);
    if (writeSize < 0) {
      logger::error << "write file failed" << logger::endl;
      res = Http::getErrorPage(INTERNAL_SERVER_ERROR, req);
      prepareIO(client_fd);
    }
    else
      postProcessing(client_fd);
  }
  else
    res.addOffSet(writeSize);
}

void Server::readFile(int fd) {
  char          buf[BUF_SIZE + 1];
  int           read_size;
  int           client_fd = this->file_map[fd];
  HttpResponse& res = this->responses[client_fd];
  HttpRequest&  req = this->requests[client_fd];

  read_size = read(fd, buf, BUF_SIZE);
  if (read_size <= 0) {
    ft_fd_clr(fd, this->reads);
    this->file_map.erase(fd);
    close(fd);
    if (read_size < 0) {
      logger::error << "read file failed" << logger::endl;
      res = Http::getErrorPage(INTERNAL_SERVER_ERROR, req);
      prepareIO(client_fd);
    }
    else {
      res.setBody(res.getFileBuffer());
      postProcessing(client_fd);
    }
  }
  else {
    buf[read_size] = 0;
    res.addFileBuffer(std::string(buf, read_size));
  }
}

/*
 * ==============================================
 *                Select Utility
 * ==============================================
 */

void Server::ft_fd_set(int fd, fd_set& set) {
  FD_SET(fd, &set);
  if (this->fdMax < fd)
    this->fdMax = fd;
}

void Server::ft_fd_clr(int fd, fd_set& set) {
  if (FD_ISSET(fd, &set))
    FD_CLR(fd, &set);
}

/*
 * ==============================================
 *                Server Utility
 * ==============================================
 */

int Server::init_socket() {
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd == -1)
    throw std::runtime_error("Server initialization failed");

//  int option = 1;
//  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &option, sizeof(option));

  return fd;
}

void Server::init_sockaddr_in(const std::string& host, int port, sockaddr_in& in) {
  if (!memset(&in, 0, sizeof(in)))
    throw std::runtime_error("Server initialization failed");

  in.sin_family = AF_INET;
  inet_pton(AF_INET, host.c_str(), &(in.sin_addr));
  in.sin_port = htons(port);

  logger::info << "Preparing... Host=[" << inet_ntoa(in.sin_addr) << "] Port=[" << ntohs(in.sin_port) << "]" << logger::endl;
}

void Server::open_socket(int server_fd, sockaddr_in& in) {
  bool  bind_success = false;
  bool  listen_success = false;

  for (size_t i = 0; i < BIND_MAX_TRIES; ++i) {
    if (bind(server_fd, (struct sockaddr*)&in, sizeof(in)) == -1)
      logger::warning << "Bind failed... retry... " << i + 1 << logger::endl;
    else {
      bind_success = true;
      break;
    }
    sleep(TRY_SLEEP_TIME);
  }

  if (bind_success == false)
    throw std::runtime_error("Server bind failed");

  for (size_t i = 0; i < LISTEN_MAX_TRIES; ++i) {
    if (listen(server_fd, MANAGE_FD_MAX) == -1)
      logger::warning << "Listen failed... retry... " << i + 1 << logger::endl;
    else {
      listen_success = true;
      break;
    }
    sleep(TRY_SLEEP_TIME);
  }

  if (listen_success == false)
    throw std::runtime_error("Server listen failed");

  logger::info << "Listening... (" << server_fd << ") \n\n\"http://" << inet_ntoa(in.sin_addr) << ":" << ntohs(in.sin_port) << "\"\n" << logger::endl;
}
