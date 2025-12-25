#include "rsjfw/registry.hpp"
#include <iostream>
#include <vector>
#include <cstdlib>
#include <sstream>
#include <iomanip>

namespace rsjfw {

Registry::Registry(const std::string& prefixDir)
    : prefixDir_(prefixDir) {}

bool Registry::add(const std::string& key, const std::string& valueName, const std::string& value) {
    std::stringstream ss;
    ss << "wine reg add \"" << key << "\" /v \"" << valueName << "\" /t REG_SZ /d \"" << value << "\" /f";
    
    setenv("WINEPREFIX", prefixDir_.c_str(), 1);
    setenv("WINEDEBUG", "-all", 1);
    
    int res = std::system(ss.str().c_str());
    return res == 0;
}

bool Registry::add(const std::string& key, const std::string& valueName, unsigned int value) {
    std::stringstream ss;
    ss << "wine reg add \"" << key << "\" /v \"" << valueName << "\" /t REG_DWORD /d " << value << " /f";
    
    setenv("WINEPREFIX", prefixDir_.c_str(), 1);
    setenv("WINEDEBUG", "-all", 1);
    
    int res = std::system(ss.str().c_str());
    return res == 0;
}

bool Registry::addBinary(const std::string& key, const std::string& valueName, const std::vector<unsigned char>& data) {
    std::stringstream ss;
    ss << std::hex << std::setfill('0');
    for (auto byte : data) {
        ss << std::setw(2) << static_cast<int>(byte);
    }
    
    std::stringstream cmd;
    cmd << "wine reg add \"" << key << "\" /v \"" << valueName << "\" /t REG_BINARY /d " << ss.str() << " /f";
    
    setenv("WINEPREFIX", prefixDir_.c_str(), 1);
    setenv("WINEDEBUG", "-all", 1);
    
    int res = std::system(cmd.str().c_str());
    return res == 0;
}

bool Registry::exists(const std::string& key, const std::string& valueName) {
    std::stringstream ss;
    ss << "wine reg query \"" << key << "\" /v \"" << valueName << "\"";
    
    setenv("WINEPREFIX", prefixDir_.c_str(), 1);
    setenv("WINEDEBUG", "-all", 1);
    
    // reg query returns 0 if found, 1 if not
    int res = std::system((ss.str() + " > /dev/null 2>&1").c_str());
    return res == 0;
}

std::string Registry::readString(const std::string& key, const std::string& valueName) {
    std::stringstream ss;
    ss << "wine reg query \"" << key << "\" /v \"" << valueName << "\"";
    
    setenv("WINEPREFIX", prefixDir_.c_str(), 1);
    setenv("WINEDEBUG", "-all", 1);
    
    FILE* pipe = popen(ss.str().c_str(), "r");
    if (!pipe) return "";
    
    char buffer[128];
    std::string result = "";
    while (fgets(buffer, 128, pipe) != NULL) {
        result += buffer;
    }
    pclose(pipe);
    
    // Parse output:
    // HKEY_...
    //     valueName    REG_SZ    value
    
    std::istringstream iss(result);
    std::string line;
    while (std::getline(iss, line)) {
        if (line.find(valueName) != std::string::npos && line.find("REG_SZ") != std::string::npos) {
            size_t pos = line.find("REG_SZ");
            if (pos != std::string::npos) {
                std::string val = line.substr(pos + 6);
                // Trim separate whitespace
                 val.erase(0, val.find_first_not_of(" \t\r\n"));
                 val.erase(val.find_last_not_of(" \t\r\n") + 1);
                 return val;
            }
        }
    }
    return "";
}

std::vector<unsigned char> Registry::readBinary(const std::string& key, const std::string& valueName) {
    std::stringstream ss;
    ss << "wine reg query \"" << key << "\" /v \"" << valueName << "\"";
    
    setenv("WINEPREFIX", prefixDir_.c_str(), 1);
    setenv("WINEDEBUG", "-all", 1);
    
    FILE* pipe = popen(ss.str().c_str(), "r");
    if (!pipe) return {};
    
    char buffer[128];
    std::string result = "";
    while (fgets(buffer, 128, pipe) != NULL) {
        result += buffer;
    }
    pclose(pipe);
    
    std::istringstream iss(result);
    std::string line;
    std::string hexStr = "";
    
    while (std::getline(iss, line)) {
        if (line.find(valueName) != std::string::npos && line.find("REG_BINARY") != std::string::npos) {
            size_t pos = line.find("REG_BINARY");
            if (pos != std::string::npos) {
                hexStr = line.substr(pos + 10);
                // Trim
                hexStr.erase(0, hexStr.find_first_not_of(" \t\r\n"));
                hexStr.erase(hexStr.find_last_not_of(" \t\r\n") + 1);
                break;
            }
        }
    }
    
    if (hexStr.empty()) return {};
    
    std::vector<unsigned char> data;
    for (size_t i = 0; i < hexStr.length(); i += 2) {
        if (i + 1 < hexStr.length()) {
            std::string byteString = hexStr.substr(i, 2);
            try {
                unsigned char byte = (unsigned char)strtol(byteString.c_str(), NULL, 16);
                data.push_back(byte);
            } catch (...) {}
        }
    }
    return data;
}

} // namespace rsjfw
