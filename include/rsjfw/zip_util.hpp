#ifndef RSJFW_ZIP_UTIL_HPP
#define RSJFW_ZIP_UTIL_HPP

#include <string>

namespace rsjfw {

class ZipUtil {
public:
    static bool extract(const std::string& archivePath, const std::string& destPath);
};

} // namespace rsjfw

#endif // RSJFW_ZIP_UTIL_HPP
