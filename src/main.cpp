#include <iostream>
#include <string>
#include <vector>
#include "rsjfw/downloader.hpp"
#include "rsjfw/launcher.hpp"
#include "rsjfw/logger.hpp"
#include "rsjfw/config.hpp"
#include "rsjfw/gui.hpp"
#include <cstdlib>
#include <thread>
#include <filesystem>
#include <algorithm>
#include "rsjfw/socket.hpp"
#include <random>

void showHelp() {
    std::cout << "RSJFW - Roblox Studio Just Fucking Works\n\n"
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
              << "  -d, --debug    Enable full Wine debug logging (disable WINEDEBUG suppression)\n";
}

int main(int argc, char* argv[]) {
    std::vector<std::string> args(argv + 1, argv + argc);

    if (args.empty() || args[0] == "help" || args[0] == "--help" || args[0] == "-h") {
        if (!args.empty()) { 
            showHelp();
            return 0;
        }
    }

    bool verbose = false;
    auto it = std::find_if(args.begin(), args.end(), [](const std::string& arg) {
        return arg == "-v" || arg == "--verbose";
    });
    
    if (it != args.end()) {
        verbose = true;
        args.erase(it); 
    }

    bool debug = false;
    auto itDebug = std::find_if(args.begin(), args.end(), [](const std::string& arg) {
        return arg == "-d" || arg == "--debug";
    });
    
    if (itDebug != args.end()) {
        debug = true;
        args.erase(itDebug); 
    }

    const std::string home = std::getenv("HOME");
    const std::string logPath = home + "/.local/share/rsjfw/rsjfw.log";
    rsjfw::Logger::instance().init(logPath, verbose);
    
    rsjfw::SingleInstance singleInstance("main");
    if (!singleInstance.isPrimary()) {
        if (!args.empty()) {
            LOG_INFO("Another instance is running. Forwarding arguments...");
            singleInstance.sendArgs(args);
        } else {
            LOG_INFO("Another instance is running. Exiting.");
        }
        return 0;
    }

    singleInstance.listen([&](const std::vector<std::string>& receivedArgs) {
        LOG_INFO("Received arguments from secondary instance");
        for (const auto& arg : receivedArgs) {
             const std::string rsjfwRoot = home + "/.rsjfw";
             rsjfw::Launcher launcher(rsjfwRoot);
             launcher.setDebug(debug);
             rsjfw::Downloader downloader(rsjfwRoot);
             std::string latestVersion = downloader.getLatestVersionGUID();
             
             std::vector<std::string> launchArgs;

             if (arg.find("roblox-studio-auth:") == 0) {
                 LOG_INFO("Launching auth protocol: " + arg);
                 launchArgs.push_back(arg);
             } 
             else if (arg.find("roblox-studio:") == 0) {
                 LOG_INFO("Launching studio protocol: " + arg);
                 launchArgs.push_back("-protocolString");
                 launchArgs.push_back(arg);
             }
             else if (std::filesystem::exists(arg)) {
                 LOG_INFO("Opening file: " + arg);
                 launchArgs.push_back("-task");
                 launchArgs.push_back("EditFile");
                 launchArgs.push_back("-localPlaceFile");
                 launchArgs.push_back(arg);
             }

             if (!launchArgs.empty()) {
                 std::thread([launcher, latestVersion, launchArgs]() mutable {
                     launcher.setupFFlags(latestVersion); 
                     launcher.launchVersion(latestVersion, launchArgs);
                 }).detach();
             }
        }
    });

    const std::string configPath = home + "/.config/rsjfw/config.json";
    rsjfw::Config::instance().load(configPath);

    const std::string command = args.empty() ? "config" : args[0];
    const std::string rsjfwRoot = home + "/.rsjfw";

    LOG_INFO("RSJFW Started. Command: " + command);

    if (command == "config") {
        auto& gui = rsjfw::GUI::instance();
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
    
    if (command == "launch" || command == "install" || command == "reinstall") {
        auto& gui = rsjfw::GUI::instance();
        bool useGui = gui.init(500, 300, "RSJFW", false);
        
        if (useGui) {
            bool isProtocol = false;
            std::vector<std::string> extraArgs;
            if (args.size() > 1) {
                 for (size_t i = 1; i < args.size(); ++i) {
                    std::string arg = args[i];
                    if (arg.find("roblox-studio-auth:") == 0) {
                        extraArgs.push_back(arg);
                        isProtocol = true;
                    } 
                    else if (arg.find("roblox-studio:") == 0) {
                        extraArgs.push_back("-protocolString");
                        extraArgs.push_back(arg);
                        isProtocol = true;
                    } else {
                        extraArgs.push_back(arg);
                    }
                }
            }

            if (isProtocol) {
                rsjfw::Downloader downloader(rsjfwRoot);
                rsjfw::Launcher launcher(rsjfwRoot);
                launcher.setDebug(debug);
                std::string latestVersion = downloader.getLatestVersionGUID();
                
                launcher.setupFFlags(latestVersion);
                
                if (!launcher.launchVersion(latestVersion, extraArgs)) {
                     LOG_ERROR("Protocol launch failed.");
                     return 1;
                }
                return 0; 
            }

            gui.setMode(rsjfw::GUI::MODE_LAUNCHER);
            
            std::thread worker([&]() {
                rsjfw::Downloader downloader(rsjfwRoot);
                rsjfw::Launcher launcher(rsjfwRoot);
                launcher.setDebug(debug);
                
                try {
                    if (isReinstall) {
                        gui.setProgress(0.05f, "Removing old versions...");
                        std::filesystem::path versionsDir = std::filesystem::path(rsjfwRoot) / "versions";
                        if (std::filesystem::exists(versionsDir)) {
                            std::filesystem::remove_all(versionsDir);
                        }
                        std::filesystem::path prefixMarker = std::filesystem::path(rsjfwRoot) / "prefix" / ".rsjfw_setup_complete";
                        if (std::filesystem::exists(prefixMarker)) {
                            std::filesystem::remove(prefixMarker);
                        }
                    }
                    
                    gui.setProgress(0.1f, "Checking for updates...");
                    std::string latestVersion = downloader.getLatestVersionGUID();
                    
                    gui.setProgress(0.2f, "Downloading " + latestVersion + "...");
                    
                    auto progressCb = [&](const std::string& item, float itemProgress, size_t index, size_t total) {
                         float totalProg = (float)index / (float)total;
                         gui.setProgress(0.2f + (totalProg * 0.4f), "Installing " + std::to_string(index) + "/" + std::to_string(total) + " packages...");
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

                    gui.setProgress(0.95f, "Launching Roblox Studio...");
                    
                    if (args.size() > 1) {
                         for (size_t i = 1; i < args.size(); ++i) {
                            std::string arg = args[i];
                            if (arg.find("roblox-studio:") == 0 || arg.find("roblox-studio-auth:") == 0) {
                                extraArgs.push_back("-protocolString");
                                extraArgs.push_back(arg);
                            } else {
                                extraArgs.push_back(arg);
                            }
                        }
                    }

                    gui.close(); 
                    
                    if (!launcher.launchVersion(latestVersion, extraArgs, launcherProgress)) {
                        LOG_ERROR("Launch failed.");
                    }
                    
                } catch (const std::exception& e) {
                    gui.setError(e.what());
                }
            });
            
            gui.run(nullptr);
            gui.shutdown();
            
            if (worker.joinable()) worker.join();
            return 0;
            
        } else {
             if (command == "install") {
                LOG_INFO("Starting installation to " + rsjfwRoot);
                rsjfw::Downloader downloader(rsjfwRoot);
                return downloader.installLatest() ? 0 : 1;
            } else {
                LOG_ERROR("GUI initialization-failed-fallback not fully implemented for launch.");
                return 1;
            }
        }
    }

    if (command == "kill") {
        LOG_INFO("Terminating Studio...");
        rsjfw::Launcher launcher(rsjfwRoot);
        launcher.setDebug(debug);
        return launcher.killStudio() ? 0 : 1;
    } 
    
    showHelp();
    return 1;
}
