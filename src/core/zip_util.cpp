#include "rsjfw/zip_util.hpp"
#include <zip.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <cstring>
#include <algorithm>

namespace rsjfw {

bool ZipUtil::extract(const std::string& archivePath, const std::string& destPath) {
    int err = 0;
    zip* z = zip_open(archivePath.c_str(), 0, &err);
    if (!z) {
        return false;
    }

    std::filesystem::create_directories(destPath);

    zip_int64_t num_entries = zip_get_num_entries(z, 0);
    for (zip_int64_t i = 0; i < num_entries; ++i) {
        const char* name = zip_get_name(z, i, 0);
        if (!name) continue;

        std::string entryName(name);
        std::replace(entryName.begin(), entryName.end(), '\\', '/');
        
        // Ensure the path is relative to destPath
        while (!entryName.empty() && entryName.front() == '/') {
            entryName.erase(0, 1);
        }

        if (entryName.empty()) continue;

        std::filesystem::path entryPath = std::filesystem::path(destPath) / entryName;
        
        if (entryName.back() == '/') {
            std::filesystem::create_directories(entryPath);
            continue;
        }

        std::filesystem::create_directories(entryPath.parent_path());

        zip_file* f = zip_fopen_index(z, i, 0);
        if (!f) continue;

        std::ofstream ofs(entryPath, std::ios::binary);
        if (!ofs) {
            zip_fclose(f);
            continue;
        }

        char buffer[8192];
        zip_int64_t n;
        while ((n = zip_fread(f, buffer, sizeof(buffer))) > 0) {
            ofs.write(buffer, n);
        }

        zip_fclose(f);
        ofs.close();
    }

    zip_close(z);
    return true;
}

} // namespace rsjfw
