#ifndef HTTP_HPP
# define HTTP_HPP

# include "../config/ServerConfig.hpp"
# include "./HttpDataFetcher.hpp"
# include "./HttpRequest.hpp"
# include "./HttpResponse.hpp"
# include "./HttpResponseBuilder.hpp"

# include <vector>
# include <utility>

class Http {
  public:
    Http(ServerConfig config);
    ~Http();

    std::string processing(std::string request);

  private:
    ServerConfig  config;
};

#endif
