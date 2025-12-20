#pragma once

#include <string>

namespace rsjfw {

class ZipUtil {
public:
    static bool extract(const std::string& archivePath, const std::string& destPath);
};

} // namespace rsjfw
