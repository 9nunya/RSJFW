#include "rsjfw/http.hpp"
#include <stdexcept>
#include <fstream>
#include <iostream>
#include <filesystem>

namespace rsjfw {

size_t HTTP::writeCallback(void* contents, size_t size, size_t nmemb, std::string* userp) {
    userp->append((char*)contents, size * nmemb);
    return size * nmemb;
}

std::string HTTP::get(const std::string& url) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        throw std::runtime_error("Failed to initialize cURL");
    }

    std::string response;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36");

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        curl_easy_cleanup(curl);
        throw std::runtime_error("cURL request failed: " + std::string(curl_easy_strerror(res)));
    }

    curl_easy_cleanup(curl);
    return response;
}

struct ProgressData {
    HTTP::ProgressCallback callback;
};

size_t HTTP::fileWriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    std::ofstream* ofs = static_cast<std::ofstream*>(userp);
    size_t totalSize = size * nmemb;
    ofs->write(static_cast<char*>(contents), totalSize);
    return totalSize;
}

int HTTP::progressCallback(void* clientp, double dltotal, double dlnow, double ultotal, double ulnow) {
    ProgressData* data = static_cast<ProgressData*>(clientp);
    if (data && data->callback && dltotal > 0) {
        data->callback(static_cast<size_t>(dlnow), static_cast<size_t>(dltotal));
    }
    return 0;
}

bool HTTP::download(const std::string& url, const std::string& filepath, ProgressCallback callback) {
    CURL* curl = curl_easy_init();
    if (!curl) return false;

    std::string partPath = filepath + ".part";
    std::ofstream ofs(partPath, std::ios::binary);
    if (!ofs) {
        curl_easy_cleanup(curl);
        return false;
    }

    ProgressData data{callback};

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, fileWriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &ofs);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36");
    
    if (callback) {
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(curl, CURLOPT_PROGRESSFUNCTION, progressCallback);
        curl_easy_setopt(curl, CURLOPT_PROGRESSDATA, &data);
    }

    CURLcode res = curl_easy_perform(curl);
    ofs.close();
    curl_easy_cleanup(curl);
    
    if (res == CURLE_OK) {
        try {
            if (std::filesystem::exists(filepath)) std::filesystem::remove(filepath);
            std::filesystem::rename(partPath, filepath);
            return true;
        } catch (...) {
            return false;
        }
    } else {
        std::filesystem::remove(partPath);
        return false;
    }
}

} // namespace rsjfw
