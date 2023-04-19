#ifndef UTIL_HPP
# define UTIL_HPP

# include <vector>
# include <sstream>
# include <fstream>

# include "Except.hpp"

namespace util {
  std::vector<std::string> split(const std::string& str, char delim);
  std::vector<std::string> split(const std::string& str, const std::string& delim);
  size_t find(const std::string& str, const std::string& target);
  std::string trimSpace(std::string s);
  std::string readFile(const std::string& fileName);
}

#endif