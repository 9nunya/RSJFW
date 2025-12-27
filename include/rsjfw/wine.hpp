#ifndef RSJFW_WINE_HPP
#define RSJFW_WINE_HPP

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <filesystem>
#include <functional>

namespace rsjfw {
namespace wine {

class Prefix {
public:
    // Root: Path to the Wine installation (e.g. /path/to/wine-9.0)
    // Dir: Path to the Wine Prefix (e.g. ~/.rsjfw/prefix)
    Prefix(const std::string& root, const std::string& dir);

    // Determines if the current Prefix Root is Proton
    bool isProton() const;

    // Returns the directory of the Wineprefix
    std::string dir() const { return dir_; }

    // Returns the Wine root path
    std::string root() const { return root_; }

    // Sets additional environment variables for this prefix
    void setEnv(const std::map<std::string, std::string>& env);
    void appendEnv(const std::string& key, const std::string& value);
    std::string getEnv(const std::string& key) const;

    // Runs a command within the Wineprefix
    // Returns true on success (exit code 0), false otherwise
    // onOutput: Optional callback for stdout/stderr streaming
    // cwd: Optional working directory for the process
    bool runCommand(const std::string& exe, const std::vector<std::string>& args, std::function<void(const std::string&)> onOutput = nullptr, const std::string& cwd = "", bool wait = true);

    // Wrapper to run 'wine' or 'wine64' based on availability
    bool wine(const std::string& exe, const std::vector<std::string>& args, std::function<void(const std::string&)> onOutput = nullptr, const std::string& cwd = "", bool wait = true);

    // Helper to resolve a binary path (accounting for Proton structure)
    std::string bin(const std::string& prog) const;

    // Helper: Registry operations
    bool registryAdd(const std::string& key, const std::string& valueName, const std::string& value, const std::string& type = "REG_SZ");
    
    struct RegistryEntry {
        std::string key;
        std::string valueName;
        std::string value;
        std::string type; // e.g. "REG_SZ", "REG_DWORD", "REG_BINARY"
    };
    bool registryApply(const std::vector<RegistryEntry>& entries);

    // Kill all processes in this prefix (wineserver -k)
    bool kill();

private:
    std::string root_;
    std::string dir_;
    std::map<std::string, std::string> env_;
    
    // Internal helper to construct full environment vector
    std::vector<std::string> buildEnv() const;
};

} // namespace wine
} // namespace rsjfw

#endif // RSJFW_WINE_HPP
