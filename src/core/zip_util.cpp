#include "rsjfw/zip_util.hpp"
#include "rsjfw/logger.hpp"
#include <archive.h>
#include <archive_entry.h>
#include <filesystem>
#include <iostream>
#include <cstring>
#include <algorithm>

namespace rsjfw {

static int copy_data(struct archive* ar, struct archive* aw) {
    int r;
    const void* buff;
    size_t size;
    la_int64_t offset;

    for (;;) {
        r = archive_read_data_block(ar, &buff, &size, &offset);
        if (r == ARCHIVE_EOF) return ARCHIVE_OK;
        if (r < ARCHIVE_OK) return r;
        r = archive_write_data_block(aw, buff, size, offset);
        if (r < ARCHIVE_OK) {
            std::cerr << "[RSJFW] Archive write error: " << archive_error_string(aw) << std::endl;
            return r;
        }
    }
}

bool ZipUtil::extract(const std::string& archivePath, const std::string& destPath) {
    struct archive* a;
    struct archive* ext;
    struct archive_entry* entry;
    int flags;
    int r;

    flags = ARCHIVE_EXTRACT_TIME;
    flags |= ARCHIVE_EXTRACT_PERM;
    flags |= ARCHIVE_EXTRACT_ACL;
    flags |= ARCHIVE_EXTRACT_FFLAGS;

    a = archive_read_new();
    archive_read_support_format_all(a);
    archive_read_support_filter_all(a);
    
    ext = archive_write_disk_new();
    archive_write_disk_set_options(ext, flags);
    archive_write_disk_set_standard_lookup(ext);

    if ((r = archive_read_open_filename(a, archivePath.c_str(), 10240))) {
        LOG_ERROR("Could not open archive " + archivePath + ": " + std::string(archive_error_string(a)));
        archive_read_free(a);
        archive_write_free(ext);
        return false;
    }

    std::filesystem::path dest(destPath);
    std::filesystem::create_directories(dest);

    for (;;) {
        r = archive_read_next_header(a, &entry);
        if (r == ARCHIVE_EOF) break;
        if (r < ARCHIVE_OK) {
             LOG_WARN("Archive header warning: " + std::string(archive_error_string(a)));
        }
        if (r < ARCHIVE_WARN) {
            archive_read_free(a);
            archive_write_free(ext);
            return false;
        }

        const char* currentFile = archive_entry_pathname(entry);
        std::string relPath = currentFile;
        
        // Sanitize: Remove leading slashes/backslashes
        while (!relPath.empty() && (relPath[0] == '/' || relPath[0] == '\\')) {
            relPath = relPath.substr(1);
        }
        
        // Sanitize: Prevent directory traversal (..)
        // This is a basic check.
        if (relPath.find("..") != std::string::npos) {
             LOG_WARN("Skipping potentially unsafe entry: " + std::string(currentFile));
             continue;
        }

        std::filesystem::path fullPath = dest / relPath;
        archive_entry_set_pathname(entry, fullPath.string().c_str());

        r = archive_write_header(ext, entry);
        if (r < ARCHIVE_OK) {
             LOG_WARN("Archive write header warning: " + std::string(archive_error_string(ext)));
        } else if (archive_entry_size(entry) > 0) {
            r = copy_data(a, ext);
            if (r < ARCHIVE_OK) {
                LOG_ERROR("Archive data copy error: " + std::string(archive_error_string(ext)));
            }
            if (r < ARCHIVE_WARN) {
                archive_read_free(a);
                archive_write_free(ext);
                return false;
            }
        }
        r = archive_write_finish_entry(ext);
        if (r < ARCHIVE_OK) {
             LOG_WARN("Archive finish entry warning: " + std::string(archive_error_string(ext)));
        }
    }

    archive_read_close(a);
    archive_read_free(a);
    archive_write_close(ext);
    archive_write_free(ext);

    return true;
}

} // namespace rsjfw
