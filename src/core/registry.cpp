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

} // namespace rsjfw
