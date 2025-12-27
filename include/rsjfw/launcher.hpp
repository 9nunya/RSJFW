#ifndef RSJFW_LAUNCHER_HPP
#define RSJFW_LAUNCHER_HPP

#include <functional>
#include <string>
#include <vector>

namespace rsjfw {
namespace wine {
class Prefix;
}

class Launcher {
public:
  Launcher(const std::string &rootDir);

  // Progress callback type: (progress 0.0-1.0, status message)
  using ProgressCb = std::function<void(float, std::string)>;
  using OutputCb = std::function<void(const std::string &)>;

  bool launchLatest(const std::vector<std::string> &extraArgs = {},
                    ProgressCb progressCb = nullptr,
                    OutputCb outputCb = nullptr, bool wait = true);
  bool launchVersion(const std::string &versionGUID,
                     const std::vector<std::string> &extraArgs = {},
                     ProgressCb progressCb = nullptr,
                     OutputCb outputCb = nullptr, bool wait = true);
  bool setupPrefix(ProgressCb progressCb = nullptr);
  bool killStudio();
  bool setupFFlags(const std::string &versionGUID,
                   ProgressCb progressCb = nullptr);
  bool openWineConfiguration();
  bool setupDxvk(const std::string &versionGUID,
                 ProgressCb progressCb = nullptr);

private:
  std::string rootDir_;
  std::string versionsDir_;
  std::string prefixDir_;
  std::string compatDataDir_;

  std::string findStudioExecutable(const std::string &versionDir);
  bool runWine(const std::string &executablePath,
               const std::vector<std::string> &args = {},
               OutputCb outputCb = nullptr, bool wait = true);

  // Configures environment variables for the prefix (Proton logic etc)
  void configureEnvironment(rsjfw::wine::Prefix &pfx, bool isProton);

public:
  void setDebug(bool debug) { debug_ = debug; }

private:
  bool debug_ = false;
};

} // namespace rsjfw

#endif // RSJFW_LAUNCHER_HPP
