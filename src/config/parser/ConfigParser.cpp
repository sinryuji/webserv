#include "./ConfigParser.hpp"

ConfigParser::ConfigParser() {
  pos = 0;
};

ConfigParser::~ConfigParser() {};

ServerConfig ConfigParser::parse(const std::string& fileName) throw(std::runtime_error) {
  ServerConfig conf;

  generateToken(fileName);

  if (curToken().is(Token::SERVER)) conf = parseServer();
  else throwBadSyntax();

  expectNextToken(Token::END_OF_FILE);

  return conf;
}

ServerConfig ConfigParser::parseServer() {
  ServerConfig conf;

  expectNextToken(Token::LBRACE);
  for (nextToken(); curToken().isNot(Token::END_OF_FILE) && curToken().isNot(Token::RBRACE); nextToken()) {
    if (curToken().is(Token::LOCATION)) conf.addLocationConfig(parseLocation(conf));
    else if (curToken().isCommon()) parseCommon(conf);
    else if (curToken().is(Token::TIMEOUT)) parseTimeout(conf);
    else if (curToken().is(Token::LISTEN)) parseListen(conf);
    else if (curToken().is(Token::SERVER_NAME)) parseServerName(conf);
    else if (curToken().is(Token::CGI)) parseCGI(conf);
    else throwBadSyntax();
  }
  expectCurToken(Token::RBRACE);

  return conf;
}

LocationConfig ConfigParser::parseLocation(ServerConfig& serverConf) {
  LocationConfig conf(serverConf);

  expectNextToken(Token::IDENT);
  conf.setPath(curToken().getLiteral());
  expectNextToken(Token::LBRACE);
  for (nextToken(); curToken().isNot(Token::END_OF_FILE) && curToken().isNot(Token::RBRACE); nextToken()) {
    if (curToken().is(Token::LOCATION)) conf.addLocationConfig(parseLocation(conf));
    else if (curToken().isCommon()) parseCommon(conf);
    else if (curToken().is(Token::ALIAS)) parseAlias(conf);
    else if (curToken().is(Token::LIMIT_EXCEPT)) parseLimitExcept(conf);
    else if (curToken().is(Token::AUTOINDEX)) parseAutoIndex(conf);
    else if (curToken().is(Token::RETURN)) parseReturn(conf);
    else throwBadSyntax();
  }
  expectCurToken(Token::RBRACE);

  return conf;
}

LocationConfig ConfigParser::parseLocation(LocationConfig& locationConf) {
  LocationConfig conf(locationConf);

  expectNextToken(Token::IDENT);
  conf.setPath(curToken().getLiteral());
  expectNextToken(Token::LBRACE);
  for (nextToken(); curToken().isNot(Token::END_OF_FILE) && curToken().isNot(Token::RBRACE); nextToken()) {
    if (curToken().is(Token::LOCATION)) conf.addLocationConfig(parseLocation(conf));
    else if (curToken().isCommon()) parseCommon(conf);
    else if (curToken().is(Token::ALIAS)) parseAlias(conf);
    else if (curToken().is(Token::LIMIT_EXCEPT)) parseLimitExcept(conf);
    else if (curToken().is(Token::AUTOINDEX)) parseAutoIndex(conf);
    else if (curToken().is(Token::RETURN)) parseReturn(conf);
    else throwBadSyntax();
  }
  expectCurToken(Token::RBRACE);

  return conf;
}

void ConfigParser::parseCommon(CommonConfig& conf) {
  if (curToken().is(Token::ROOT)) parseRoot(conf);
  else if (curToken().is(Token::ERROR_PAGE)) parseErrorPage(conf);
  else if (curToken().is(Token::CLIENT_BODY_BUFFER_SIZE)) parseClientBodyBufferSize(conf);
  else if (curToken().is(Token::INDEX)) parseIndex(conf);
}

// server
// server
// server

// timeout [second(int)];
void ConfigParser::parseTimeout(ServerConfig& conf) {
  expectNextToken(Token::INT);
  conf.setTimeout(atoi(curToken().getLiteral()));
  expectNextToken(Token::SEMICOLON);
}

// listen [host(ident)]:[port(int)];
void ConfigParser::parseListen(ServerConfig& conf) {
  expectNextToken(Token::IDENT);
  std::vector<std::string> sp = util::split(curToken().getLiteral(), ':');
  if (sp.size() != 2) throw std::runtime_error("listen error");
  conf.setHost(sp[0]);
  conf.setPort(atoi(sp[1]));
  expectNextToken(Token::SEMICOLON);
}

// server_name [path(ident)];
void ConfigParser::parseServerName(ServerConfig& conf) {
  expectNextToken(Token::IDENT);
  conf.setServerName(curToken().getLiteral());
  expectNextToken(Token::SEMICOLON);
}

// location
// location
// location

// alias [path(ident)];
void ConfigParser::parseAlias(LocationConfig& conf) {
  expectNextToken(Token::IDENT);
  conf.setAlias(curToken().getLiteral());
  expectNextToken(Token::SEMICOLON);
}

// limit_except [HTTP method(indent) ...] ;
void ConfigParser::parseLimitExcept(LocationConfig& conf) {
  while (peekToken().is(Token::IDENT)) {
    nextToken();
    conf.addLimitExcept(curToken().getLiteral());
  }
  expectNextToken(Token::SEMICOLON);
}

// cgi [extension(indent)] [CGI path(ident)]
void ConfigParser::parseCGI(ServerConfig& conf) {
  std::string ext;
  std::string path;

  expectNextToken(Token::IDENT);
  ext = curToken().getLiteral();
  expectNextToken(Token::IDENT);
  path = curToken().getLiteral();
  conf.insertCGI(ext, path);
  expectNextToken(Token::SEMICOLON);
}

// autoindex [on(ident)/off(ident)]
void ConfigParser::parseAutoIndex(LocationConfig& conf) {
  expectNextToken(Token::IDENT);
  if (curToken().getLiteral() == "on") conf.setAutoIndex(true);
  else if (curToken().getLiteral() == "off") conf.setAutoIndex(false);
  else throw std::runtime_error("autoindex error");
  expectNextToken(Token::SEMICOLON);
}

// return [HTTP status code(int)] [URI(ident)]
void ConfigParser::parseReturn(LocationConfig& conf) {
  expectNextToken(Token::INT);
  int status_code = atoi(curToken().getLiteral());
  expectNextToken(Token::IDENT);
  conf.addReturnRes(std::pair<int, std::string>(status_code, curToken().getLiteral()));
  expectNextToken(Token::SEMICOLON);
}

// common
// common
// common

// root [path(ident)]
void ConfigParser::parseRoot(CommonConfig& conf) {
  expectNextToken(Token::IDENT);
  conf.setRoot(curToken().getLiteral());
  expectNextToken(Token::SEMICOLON);
}

// error_page [HTTP status code(int) ...] [path(ident)]
void ConfigParser::parseErrorPage(CommonConfig& conf) {
  std::vector<int> statusList;

  while (peekToken().is(Token::INT)) {
    nextToken();
    int status = atoi(curToken().getLiteral());
    statusList.push_back(status);
  }

  expectNextToken(Token::IDENT);

  for (int i = 0; i < statusList.size(); ++i)
    conf.addErrorPage(statusList[i], curToken().getLiteral());
  expectNextToken(Token::SEMICOLON);
}

// client_body_buffer_size [size(int)]
void ConfigParser::parseClientBodyBufferSize(CommonConfig& conf) {
  expectNextToken(Token::INT);
  conf.setClientBodySize(atoi(curToken().getLiteral()));
  expectNextToken(Token::SEMICOLON);
}

// index [file_name(ident)]
void ConfigParser::parseIndex(CommonConfig& conf) {
  expectNextToken(Token::IDENT);
  conf.setIndex(curToken().getLiteral());
  expectNextToken(Token::SEMICOLON);
}

void ConfigParser::generateToken(std::string fileName) {
  size_t        lineCount = 1;
  Lexer         lexer;
  Token         token;
  std::string   line;
  std::ifstream fileIn(fileName.c_str(), std::ifstream::in);

  this->fileName = fileName;
  if (!fileIn.is_open())
    throw std::runtime_error("config file open failed");
  for (;!std::getline(fileIn, line).eof(); ++lineCount) {
    lexer.setInput(line);
    while ((token = lexer.nextToken()).isNot(Token::END_OF_FILE)) {
      token.setLineNumber(lineCount);
      tokens.push_back(token);
    }
  }
  tokens.push_back(Token(Token::END_OF_FILE, "END_OF_FILE"));
}

void ConfigParser::nextToken() {
  if (this->pos + 1 < tokens.size()) {
    this->pos += 1;
  }
};

Token ConfigParser::curToken() const {
  return this->tokens[this->pos];
};

Token ConfigParser::peekToken() const {
  if (this->pos + 1 >= this->tokens.size())
    throw std::runtime_error("tokenization error");
  return this->tokens[this->pos + 1];
}

void ConfigParser::expectNextToken(const std::string& expected) {
  nextToken();

  if (curToken().getType() != expected)
    throwExpectError(expected);
}

void ConfigParser::expectCurToken(const std::string& expected) const {
  if (curToken().getType() != expected)
    throwExpectError(expected);
}

void ConfigParser::throwExpectError(const std::string& expected) const throw (std::runtime_error) {
  std::string errorMsg =
    this->fileName
    + " "
    + std::to_string(curToken().getLineNumber())
    + ":" + util::itoa(curToken().getPos())
    + " expected \'" + expected + "\' but \'" + curToken().getLiteral() + "\'";

  throw std::runtime_error(errorMsg);
}

void ConfigParser::throwBadSyntax() const throw (std::runtime_error){
  std::string errorMsg =
    this->fileName
    + " "
    + std::to_string(curToken().getLineNumber())
    + ":" + util::itoa(curToken().getPos())
    + " bad syntax \'" + curToken().getLiteral() + "\'";

  throw std::runtime_error(errorMsg);
}

int ConfigParser::atoi(const std::string& s) const {
  int ret;

  try {
    ret = std::atoi(s.c_str());
  } catch (std::exception& e) {
    throwBadSyntax();
  }
  return ret;
}
