#include "rsjfw/config.hpp"
#include "rsjfw/downloader.hpp"
#include "rsjfw/gui.hpp"
#include "rsjfw/launcher.hpp"
#include "rsjfw/logger.hpp"
#include "rsjfw/path_manager.hpp"
#include "rsjfw/socket.hpp"
#include "rsjfw/task_runner.hpp"
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <random>
#include <string>
#include <thread>
#include <vector>

void showHelp() {
  std::cout
      << "RSJFW - Roblox Studio Just Fucking Works\n\n"
      << "Usage: rsjfw [command] [args...]\n\n"
      << "Commands:\n"
      << "  config     Open the configuration editor (Default)\n"
      << "  install    Download and install the latest Roblox Studio\n"
      << "  reinstall  Force reinstall Roblox Studio (removes versions first)\n"
      << "  launch     Launch the installed Roblox Studio\n"
      << "  kill       Kill any running Roblox Studio instances\n"
      << "  help       Show this help message\n\n"
      << "Flags:\n"
      << "  -v, --verbose  Enable verbose logging to stdout\n"
      << "  -d, --debug    Enable full Wine debug logging (disable WINEDEBUG "
         "suppression)\n";
}

#include "rsjfw/version.hpp"

int main(int argc, char *argv[]) {
  std::vector<std::string> args;
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg != "%u" && !arg.empty()) {
      args.push_back(arg);
    }
  }

  if (!args.empty() &&
      (args[0] == "help" || args[0] == "--help" || args[0] == "-h")) {
    showHelp();
    return 0;
  }

  if (!args.empty() && (args[0] == "--version" || args[0] == "-version")) {
    std::cout << "RSJFW v" << rsjfw::RSJFW_VERSION_STRING << "\n";
    return 0;
  }

  bool verbose = false;
  auto it = std::find_if(args.begin(), args.end(), [](const std::string &arg) {
    return arg == "-v" || arg == "--verbose";
  });

  if (it != args.end()) {
    verbose = true;
    args.erase(it);
  }

  bool debug = false;
  auto itDebug =
      std::find_if(args.begin(), args.end(), [](const std::string &arg) {
        return arg == "-d" || arg == "--debug";
      });

  if (itDebug != args.end()) {
    debug = true;
    args.erase(itDebug);
  }

  // Initialize PathManager early for SingleInstance and Logger
  rsjfw::PathManager::instance().init();
  auto &pathMgr = rsjfw::PathManager::instance();
  const std::string rsjfwRoot = pathMgr.root().string();

  rsjfw::Logger::instance().init(pathMgr.currentLog(), verbose);
  LOG_INFO("=== RSJFW Main Boot Started ===");

  // Log all arguments for debugging protocol issues
  {
    std::string argStr;
    for (int i = 0; i < argc; ++i) {
      argStr +=
          (i > 0 ? " " : "") + std::string("'") + argv[i] + std::string("'");
    }
    LOG_INFO("Raw Command Line: " + argStr);
    if (verbose)
      std::cerr << "[RSJFW-DEBUG] Raw argv: " << argStr << "\n";
  }

  // Load configuration early
  const std::string configPath = (pathMgr.root() / "config.json").string();
  rsjfw::Config::instance().load(configPath);

  // Fast Protocol Path - search for roblox-studio links
  std::string protocolArg;
  for (const auto &arg : args) {
    // The args vector is already filtered for "%u" and empty strings
    if (arg.find("roblox-studio-auth:") == 0 ||
        arg.find("roblox-studio:") == 0) {
      protocolArg = arg;
      break;
    }
  }

  if (!protocolArg.empty()) {
    rsjfw::Downloader downloader(rsjfwRoot);
    rsjfw::Launcher launcher(rsjfwRoot);
    launcher.setDebug(debug);

    std::string targetVersion;
    try {
      targetVersion = downloader.getLatestVersionGUID();
    } catch (...) {
    }
    if (targetVersion.empty() ||
        !downloader.isVersionInstalled(targetVersion)) {
      auto versions = downloader.getInstalledVersions();
      if (!versions.empty()) {
        std::sort(versions.rbegin(), versions.rend());
        targetVersion = versions[0];
      }
    }

    if (!targetVersion.empty()) {
      std::vector<std::string> launchArgs;
      // roblox-studio-auth: should NOT use -protocolString
      if (protocolArg.find("roblox-studio-auth:") == 0) {
        launchArgs.push_back(protocolArg);
      } else {
        launchArgs.push_back("-protocolString");
        launchArgs.push_back(protocolArg);
      }

      LOG_INFO("Fast-Path: Launching " + targetVersion + " (Detached)");
      launcher.setupFFlags(targetVersion);
      launcher.launchVersion(targetVersion, launchArgs, nullptr, nullptr,
                             false); // Detached
      return 0;
    }
  }

  // Not a protocol link. Enforce single instance.
  rsjfw::SingleInstance singleInstance(pathMgr.root() / "rsjfw.lock");
  if (!singleInstance.isPrimary()) {
    std::cout << "[RSJFW] Another instance is already running. Exiting.\n";
    return 0;
  }

  if (!args.empty() && args[0] == "kill") {
    LOG_INFO("Terminating Studio...");
    rsjfw::Launcher launcher(rsjfwRoot);
    launcher.setDebug(debug);
    return launcher.killStudio() ? 0 : 1;
  }

  std::string command = args.empty() ? "config" : args[0];
  // If the first argument IS a protocol, the command is 'launch'
  if (!args.empty() && (args[0].find("roblox-studio-auth:") == 0 ||
                        args[0].find("roblox-studio:") == 0)) {
    command = "launch";
  }

  LOG_INFO("RSJFW Started. Command: " + command);

  if (command == "config") {
    auto &gui = rsjfw::GUI::instance();
    if (gui.init(800, 600, "RSJFW - Config", true)) {
      gui.setMode(rsjfw::GUI::MODE_CONFIG);
      gui.run(nullptr);
      return 0;
    } else {
      LOG_ERROR("Could not initialize GUI for config editor. Check logs.");
      return 1;
    }
  }

  bool isReinstall = (command == "reinstall");
  bool isInstallOnly = (command == "install" || isReinstall);

  // ...

  if (command == "launch" || command == "install" || command == "reinstall") {
    auto &gui = rsjfw::GUI::instance();
    std::string title = "RSJFW v" + rsjfw::RSJFW_VERSION_STRING;
    bool useGui = gui.init(500, 300, title.c_str(), false);

    if (useGui) {
      // We skip the early-exit for protocols to ensure the installer and health
      // checks run.

      gui.setMode(rsjfw::GUI::MODE_LAUNCHER);

      rsjfw::TaskRunner::instance().run([&]() {
        rsjfw::Downloader downloader(rsjfwRoot);
        rsjfw::Launcher launcher(rsjfwRoot);
        launcher.setDebug(debug);

        try {
          // Re-parse extra arguments inside the task thread from the original
          // command line args
          std::vector<std::string> extraArgs;
          if (args.size() >
              0) { // args[0] is the command (launch/install/reinstall)
            for (size_t i = 1; i < args.size(); ++i) {
              std::string arg = args[i];
              if (arg.find("roblox-studio-auth:") == 0) {
                extraArgs.push_back(arg); // AUTH IS RAW
              } else if (arg.find("roblox-studio:") == 0) {
                extraArgs.push_back("-protocolString");
                extraArgs.push_back(arg);
              } else {
                extraArgs.push_back(arg);
              }
            }
          }

          LOG_DEBUG("Parsed " + std::to_string(extraArgs.size()) +
                    " extra arguments.");
          for (const auto &ea : extraArgs)
            LOG_DEBUG("Extra arg: " + ea);

          if (isReinstall) {
            gui.setProgress(0.05f, "Removing old versions...");
            std::filesystem::path versionsDir =
                std::filesystem::path(rsjfwRoot) / "versions";
            if (std::filesystem::exists(versionsDir)) {
              std::filesystem::remove_all(versionsDir);
            }
            std::filesystem::path prefixMarker =
                std::filesystem::path(rsjfwRoot) / "prefix" /
                ".rsjfw_setup_complete";
            if (std::filesystem::exists(prefixMarker)) {
              std::filesystem::remove(prefixMarker);
            }
          }

          // GPU Compatibility Check - auto-fix DXVK if incompatible
          gui.setProgress(0.05f, "Checking GPU compatibility...");
          std::string vkCmd = "vulkaninfo --summary 2>/dev/null | grep "
                              "'apiVersion' | head -n 1 | awk '{print $3}'";
          FILE *vkPipe = popen(vkCmd.c_str(), "r");
          if (vkPipe) {
            char vkBuf[64] = {0};
            if (fgets(vkBuf, sizeof(vkBuf), vkPipe)) {
              int major = 0, minor = 0;
              sscanf(vkBuf, "%d.%d", &major, &minor);

              auto &cfg = rsjfw::Config::instance();
              auto &gen = cfg.getGeneral();
              std::string dxvkVer = gen.dxvkVersion;
              std::string dxvkClean = dxvkVer;
              if (!dxvkClean.empty() &&
                  (dxvkClean[0] == 'v' || dxvkClean[0] == 'V')) {
                dxvkClean = dxvkClean.substr(1);
              }

              // Robust check: config version, root path, or "latest"
              bool isV2FromConfig = dxvkClean.find("2.") == 0;
              bool isV2FromRoot =
                  gen.dxvkRoot.find("dxvk-2.") != std::string::npos;
              bool isLatest = (dxvkClean == "Latest" || dxvkClean == "latest");

              // VK 1.2 or below + DXVK 2.x = INCOMPATIBLE
              if (major == 1 && minor < 3 &&
                  (isV2FromConfig || isV2FromRoot || isLatest)) {
                std::string msg = "FUCK! You can't use DXVK 2.x on VK 1." +
                                  std::to_string(minor) + "...";
                gui.setProgress(0.07f, msg);
                LOG_WARN(msg + " Auto-fixing to v1.10.3");

                // Auto-fix: switch to DXVK 1.10.3
                gen.dxvkSource.version = "v1.10.3";
                gen.dxvkSource.repo = "doitsujin/dxvk";
                gen.dxvkSource.installedRoot = ""; // Clear to force re-download
                cfg.save();

                std::this_thread::sleep_for(std::chrono::seconds(2));
              }
            }
            pclose(vkPipe);
          }

          gui.setProgress(0.1f, "Checking for updates...");
          std::string latestVersion = downloader.getLatestVersionGUID();

          gui.setProgress(0.2f, "Downloading " + latestVersion + "...");

          auto progressCb = [&](const std::string &item, float itemProgress,
                                size_t index, size_t total) {
            float totalProg = (float)index / (float)total;
            gui.setProgress(0.2f + (totalProg * 0.4f),
                            "Installing " + std::to_string(index) + "/" +
                                std::to_string(total) + " packages...");
            std::string subStatus = "Downloading " + item + "...";
            gui.setSubProgress(itemProgress, subStatus);
          };

          if (!downloader.installVersion(latestVersion, progressCb)) {
            gui.setError("Failed to install Roblox Studio.");
            return;
          }

          gui.setSubProgress(0.0f, "");

          gui.setProgress(0.65f, "Setting up Wine prefix...");
          auto launcherProgress = [&](float p, std::string msg) {
            gui.setSubProgress(p, msg);
          };
          launcher.setupPrefix(launcherProgress);

          gui.setProgress(0.75f, "Installing DXVK...");
          launcher.setupDxvk(latestVersion, launcherProgress);

          gui.setProgress(0.85f, "Injecting FFlags...");
          launcher.setupFFlags(latestVersion, launcherProgress);

          if (isInstallOnly) {
            gui.setProgress(1.0f, "Installation Complete!");
            std::this_thread::sleep_for(std::chrono::seconds(2));
            gui.close();
            return;
          }

          // Health Checks and Fixes (BEFORE Launch Status)
          gui.setProgress(0.85f, "Running health checks...");
          auto &diag = rsjfw::Diagnostics::instance();
          diag.runChecks();

          auto results = diag.getResults();
          int failingCount = 0;
          for (const auto &res : results)
            if (!res.second.ok && res.second.fixable)
              failingCount++;

          if (failingCount > 0) {
            int currentFix = 0;
            for (const auto &res : results) {
              if (!res.second.ok && res.second.fixable) {
                currentFix++;
                float fixProg =
                    0.85f + ((float)currentFix / (float)failingCount * 0.1f);
                gui.setProgress(
                    fixProg, "Applying Fix " + std::to_string(currentFix) +
                                 " of " + std::to_string(failingCount) + "...");

                diag.fixIssue(res.first, [&](float p, std::string msg) {
                  gui.setSubProgress(p, "Fixing " + res.first + ": " + msg);
                });
              }
            }
          }

          gui.setProgress(0.95f, "Launching Roblox Studio...");
          gui.setSubProgress(-1.0f, "Waiting for wine...");

          // Create window detector flag
          std::atomic<bool> studioStarted{false};

          auto persistentProgress = [&](float p, std::string msg) {
            gui.setSubProgress(p, msg);
          };

          if (!launcher.launchVersion(
                  latestVersion, extraArgs, persistentProgress,
                  [&](const std::string &line) {
                    // Window Detection Heuristic - looking for engine
                    // initialization
                    if (studioStarted)
                      return;

                    // Check for specific Roblox initialization logs that
                    // indicate window creation
                    if (line.find("SurfaceController") != std::string::npos ||
                        line.find("ViewPort") != std::string::npos ||
                        line.find("D3D11Adapter") != std::string::npos ||
                        line.find("D3D11CoreCreateDevice") !=
                            std::string::npos ||
                        line.find("Presenter: Actual swap chain") !=
                            std::string::npos ||
                        line.find("Place") != std::string::npos) {

                      LOG_INFO("Studio window detected: " + line);
                      studioStarted = true;
                      gui.setSubProgress(1.0f, "Studio Started.");
                      std::this_thread::sleep_for(
                          std::chrono::milliseconds(800));
                      gui.close();
                    }
                  })) {
            LOG_ERROR("Launch failed.");
            gui.setError("Launch failed.");
          } else {
            gui.close();
          }

        } catch (const std::exception &e) {
          gui.setError(e.what());
        }
      });

      gui.run(nullptr);
      gui.shutdown();
      rsjfw::TaskRunner::instance().shutdown();
      return 0;

    } else {
      if (command == "install") {
        LOG_INFO("Starting installation to " + rsjfwRoot);
        rsjfw::Downloader downloader(rsjfwRoot);
        return downloader.installLatest() ? 0 : 1;
      } else {
        LOG_ERROR("GUI initialization-failed-fallback not fully implemented "
                  "for launch.");
        return 1;
      }
    }
  }

  showHelp();
  return 1;
}
