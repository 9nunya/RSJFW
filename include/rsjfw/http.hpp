#pragma once

#include <string>
#include <curl/curl.h>

namespace rsjfw {

class HTTP {
public:
    static std::string get(const std::string& url);
    
private:
    static size_t writeCallback(void* contents, size_t size, size_t nmemb, std::string* userp);
};

} // namespace rsjfw
