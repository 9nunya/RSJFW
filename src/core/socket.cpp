#include "rsjfw/socket.hpp"
#include "rsjfw/logger.hpp"
#include <cstring>
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>

namespace rsjfw {

SingleInstance::SingleInstance(const std::filesystem::path &lockPath) {
  lockPath_ = lockPath.string();
}

SingleInstance::~SingleInstance() {
  if (lockFd_ != -1) {
    flock(lockFd_, LOCK_UN);
    close(lockFd_);
    lockFd_ = -1;
  }
}

bool SingleInstance::isPrimary() {
  lockFd_ = open(lockPath_.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0666);
  if (lockFd_ == -1) {
    LOG_ERROR("Failed to open lock file: " + lockPath_ + " (" +
              strerror(errno) + ")");
    return false;
  }

  if (flock(lockFd_, LOCK_EX | LOCK_NB) == -1) {
    if (errno == EWOULDBLOCK) {
      // Another instance holds the lock
      close(lockFd_);
      lockFd_ = -1;
      return false;
    }
    LOG_ERROR("Failed to flock: " + std::string(strerror(errno)));
    close(lockFd_);
    lockFd_ = -1;
    return false;
  }

  return true;
}

} // namespace rsjfw
