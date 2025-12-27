#ifndef RSJFW_REGISTRY_HPP
#define RSJFW_REGISTRY_HPP

#include <string>
#include <vector>

#include "rsjfw/wine.hpp"

namespace rsjfw {

class Registry {
public:
  Registry(rsjfw::wine::Prefix &pfx);

  bool add(const std::string &key, const std::string &valueName,
           const std::string &value);
  bool add(const std::string &key, const std::string &valueName,
           unsigned int value);
  bool addBinary(const std::string &key, const std::string &valueName,
                 const std::vector<unsigned char> &data);

  bool exists(const std::string &key, const std::string &valueName);

  std::string readString(const std::string &key, const std::string &valueName);
  std::vector<unsigned char> readBinary(const std::string &key,
                                        const std::string &valueName);

private:
  rsjfw::wine::Prefix &pfx_;
};

} // namespace rsjfw

#endif // RSJFW_REGISTRY_HPP
