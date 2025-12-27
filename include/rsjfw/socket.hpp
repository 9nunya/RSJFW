#ifndef RSJFW_SOCKET_HPP
#define RSJFW_SOCKET_HPP

#include <filesystem>
#include <functional>
#include <string>

namespace rsjfw {

class SingleInstance {
public:
  explicit SingleInstance(const std::filesystem::path &lockPath);
  ~SingleInstance();

  // Returns true if this is the primary instance (acquired the lock).
  // Returns false if another instance is already running.
  bool isPrimary();

private:
  std::string lockPath_;
  int lockFd_ = -1;
};

} // namespace rsjfw

#endif // RSJFW_SOCKET_HPP
