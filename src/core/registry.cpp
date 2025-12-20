#include "rsjfw/registry.hpp"
#include <iostream>
#include <vector>
#include <cstdlib>
#include <sstream>

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

} // namespace rsjfw
