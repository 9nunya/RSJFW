#include "rsjfw/registry.hpp"
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <vector>

namespace rsjfw {

Registry::Registry(rsjfw::wine::Prefix &pfx) : pfx_(pfx) {}

bool Registry::add(const std::string &key, const std::string &valueName,
                   const std::string &value) {
  return pfx_.registryAdd(key, valueName, value, "REG_SZ");
}

bool Registry::add(const std::string &key, const std::string &valueName,
                   unsigned int value) {
  std::stringstream ss;
  ss << value;
  return pfx_.registryAdd(key, valueName, ss.str(), "REG_DWORD");
}

bool Registry::addBinary(const std::string &key, const std::string &valueName,
                         const std::vector<unsigned char> &data) {
  std::stringstream ss;
  ss << std::hex << std::setfill('0');
  for (auto byte : data) {
    ss << std::setw(2) << static_cast<int>(byte);
  }
  return pfx_.registryAdd(key, valueName, ss.str(), "REG_BINARY");
}

bool Registry::exists(const std::string &key, const std::string &valueName) {
  std::vector<std::string> args = {"query", key, "/v", valueName};
  bool found = false;

  // reg query returns 0 (true) if found, but we need to check exit code
  // The Prefix::wine function returns true if the process executed successfully
  // (exit code 0) So if it returns true, the key exists.
  return pfx_.wine("reg", args, nullptr, "", true);
}

std::string Registry::readString(const std::string &key,
                                 const std::string &valueName) {
  std::vector<std::string> args = {"query", key, "/v", valueName};
  std::string result = "";

  pfx_.wine(
      "reg", args, [&](const std::string &line) { result += line; }, "", true);

  // Parse output:
  std::istringstream iss(result);
  std::string line;
  while (std::getline(iss, line)) {
    if (line.find(valueName) != std::string::npos &&
        line.find("REG_SZ") != std::string::npos) {
      size_t pos = line.find("REG_SZ");
      if (pos != std::string::npos) {
        std::string val = line.substr(pos + 6);
        val.erase(0, val.find_first_not_of(" \t\r\n"));
        val.erase(val.find_last_not_of(" \t\r\n") + 1);
        return val;
      }
    }
  }
  return "";
}

std::vector<unsigned char> Registry::readBinary(const std::string &key,
                                                const std::string &valueName) {
  std::vector<std::string> args = {"query", key, "/v", valueName};
  std::string result = "";

  pfx_.wine(
      "reg", args, [&](const std::string &line) { result += line; }, "", true);

  std::istringstream iss(result);
  std::string line;
  std::string hexStr = "";

  while (std::getline(iss, line)) {
    if (line.find(valueName) != std::string::npos &&
        line.find("REG_BINARY") != std::string::npos) {
      size_t pos = line.find("REG_BINARY");
      if (pos != std::string::npos) {
        hexStr = line.substr(pos + 10);
        hexStr.erase(0, hexStr.find_first_not_of(" \t\r\n"));
        hexStr.erase(hexStr.find_last_not_of(" \t\r\n") + 1);
        break;
      }
    }
  }

  if (hexStr.empty())
    return {};

  std::vector<unsigned char> data;
  for (size_t i = 0; i < hexStr.length(); i += 2) {
    if (i + 1 < hexStr.length()) {
      std::string byteString = hexStr.substr(i, 2);
      try {
        unsigned char byte =
            (unsigned char)strtol(byteString.c_str(), NULL, 16);
        data.push_back(byte);
      } catch (...) {
      }
    }
  }
  return data;
}

} // namespace rsjfw
