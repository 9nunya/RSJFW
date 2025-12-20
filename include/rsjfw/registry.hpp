#pragma once

#include <string>
#include <vector>

namespace rsjfw {

class Registry {
public:
    Registry(const std::string& prefixDir);
    
    bool add(const std::string& key, const std::string& valueName, const std::string& value);
    bool add(const std::string& key, const std::string& valueName, unsigned int value);
    
private:
    std::string prefixDir_;
    
    bool runWineReg(const std::vector<std::string>& args);
};

} // namespace rsjfw
