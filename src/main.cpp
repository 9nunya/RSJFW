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

// Displays the help message to stdout
void showHelp() {
    std::cout << "RSJFW - Roblox Studio Just Fucking Works\n\n"
              << "Usage: rsjfw [command] [args...]\n\n"
              << "Commands:\n"
              << "  config     Open the configuration editor (Default)\n"
              << "  install    Download and install the latest Roblox Studio\n"
              << "  launch     Launch the installed Roblox Studio\n"
              << "  kill       Kill any running Roblox Studio instances\n"
              << "  help       Show this help message\n\n"
              << "Flags:\n"
              << "  -v, --verbose  Enable verbose logging to stdout\n";
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

    if (command == "launch" || command == "install") {
        auto& gui = rsjfw::GUI::instance();
        bool useGui = gui.init(500, 300, "RSJFW", false);
        
        if (useGui) {
            gui.setMode(rsjfw::GUI::MODE_LAUNCHER);
            
            std::thread worker([&]() {
                rsjfw::Downloader downloader(rsjfwRoot);
                rsjfw::Launcher launcher(rsjfwRoot);
                
                try {
                    gui.setProgress(0.1f, "Checking for updates...");
                    std::string latestVersion = downloader.getLatestVersionGUID();
                    
                    gui.setProgress(0.3f, "Verifying " + latestVersion + "...");
                    if (!downloader.installVersion(latestVersion)) {
                        gui.setError("Failed to install Roblox Studio.");
                        return;
                    }
                    
                    if (command == "install") {
                        gui.setProgress(1.0f, "Installation Complete!");
                        std::this_thread::sleep_for(std::chrono::seconds(1));
                        gui.close();
                        return;
                    }

                    gui.setProgress(0.5f, "Setting up Wine prefix...");
                    launcher.setupPrefix();

                    gui.setProgress(0.6f, "Checking DXVK...");
                    launcher.setupDxvk(latestVersion);
                    
                    gui.setProgress(0.7f, "Installing WebView2...");
                    launcher.installWebView2(latestVersion);
                    
                    gui.setProgress(0.8f, "Injecting FFlags...");
                    launcher.setupFFlags(latestVersion);

                    gui.setProgress(0.9f, "Launching Roblox Studio...");
                    
                    std::vector<std::string> extraArgs;
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
                    
                    if (!launcher.launchVersion(latestVersion, extraArgs)) {
                        LOG_ERROR("Launch failed.");
                    }
                    
                } catch (const std::exception& e) {
                    gui.setError(e.what());
                }
            });
            
            gui.run(nullptr);
            gui.shutdown(); // Destroy window immediately
            
            if (worker.joinable()) worker.join(); // Wait for Studio to exit
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
        return launcher.killStudio() ? 0 : 1;
    } 
    
    showHelp();
    return 1;
}
