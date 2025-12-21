#pragma once

#include <string>
#include <vector>

namespace rsjfw {

class Registry {
public:
    Registry(const std::string& prefixDir);
    
    bool add(const std::string& key, const std::string& valueName, const std::string& value);
    bool add(const std::string& key, const std::string& valueName, unsigned int value);
    bool addBinary(const std::string& key, const std::string& valueName, const std::vector<unsigned char>& data);
    
    bool exists(const std::string& key, const std::string& valueName);
    
private:
    std::string prefixDir_;
};

} // namespace rsjfw
