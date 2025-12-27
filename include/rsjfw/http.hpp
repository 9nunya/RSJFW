#ifndef RSJFW_HTTP_HPP
#define RSJFW_HTTP_HPP

#include <string>
#include <curl/curl.h>
#include <functional>

namespace rsjfw {

class HTTP {
public:
    using ProgressCallback = std::function<void(size_t current, size_t total)>;
    static std::string get(const std::string& url);
    static bool download(const std::string& url, const std::string& filepath, ProgressCallback callback = nullptr);

private:
    static size_t writeCallback(void* contents, size_t size, size_t nmemb, std::string* userp);
    static size_t fileWriteCallback(void* contents, size_t size, size_t nmemb, void* userp);
    static int progressCallback(void* clientp, double dltotal, double dlnow, double ultotal, double ulnow);
};

} // namespace rsjfw

#endif // RSJFW_HTTP_HPP
