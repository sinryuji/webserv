#include "./CommonConfig.hpp"

const int         CommonConfig::DEFAULT_CLIENT_BODY_SIZE = 8192;
const int         CommonConfig::DEFAULT_TIMEOUT = 60;
const std::string CommonConfig::DEFAULT_ROOT = "/html";
const std::string CommonConfig::DEFAULT_INDEX = "index.html";

CommonConfig::CommonConfig():
  clientBodySize(DEFAULT_CLIENT_BODY_SIZE),
  timeout(DEFAULT_TIMEOUT),
  root(DEFAULT_ROOT),
  errorPage(),
  index() {}

CommonConfig::CommonConfig(const CommonConfig& obj):
  clientBodySize(obj.getClientBodySize()),
  timeout(obj.getTimeout()),
  root(obj.getRoot()),
  errorPage(obj.getErrorPage()),
  index(obj.getIndex()) {}

CommonConfig::~CommonConfig() {}

CommonConfig &CommonConfig::operator=(const CommonConfig& obj) {
  if (this != &obj) {
    this->clientBodySize = obj.getClientBodySize();
    this->timeout = obj.getTimeout();
    this->root = obj.getRoot();
    this->errorPage = obj.getErrorPage();
    this->index = obj.getIndex();
  }
  return *this;
}

// getter

int CommonConfig::getClientBodySize() const { return this->clientBodySize; }

int CommonConfig::getTimeout() const { return this->timeout; }

std::string CommonConfig::getRoot() const { return this->root; }

std::map<int, std::string> CommonConfig::getErrorPage() const {
  return this->errorPage;
}

std::vector<std::string> CommonConfig::getIndex() const {
  if (this->index.size() == 0) {
    std::vector<std::string> ret;
    ret.push_back(DEFAULT_INDEX);
    return ret;
  }
  return this->index;
}

// setter

void CommonConfig::setClientBodySize(int n) { this->clientBodySize = n; }

void CommonConfig::setTimeout(int n) { this->timeout = n; }

void CommonConfig::setRoot(std::string root) { this->root = root; }

void CommonConfig::addErrorPage(int statusCode, std::string path) {
  this->errorPage.insert(std::make_pair(statusCode, path));
}

void CommonConfig::addIndex(std::string index) { this->index.push_back(index); }
