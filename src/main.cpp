#include <iostream>
#include <string>
#include "rsjfw/downloader.hpp"
#include "rsjfw/launcher.hpp"
#include <cstdlib>

void showHelp() {
    std::cout << "RSJFW - Roblox Studio Just Fucking Works (Prototype)\n\n"
              << "Usage: rsjfw [command] [args...]\n\n"
              << "Commands:\n"
              << "  install    Download and install the latest Roblox Studio\n"
              << "  launch     Launch the installed Roblox Studio\n"
              << "  kill       Kill any running Roblox Studio instances\n"
              << "  help       Show this help message\n";
}

int main(int argc, char* argv[]) {
    std::vector<std::string> args(argv + 1, argv + argc);

    if (args.empty() || args[0] == "help" || args[0] == "--help" || args[0] == "-h") {
        showHelp();
        return 0;
    }

    const std::string command = args[0];
    const std::string home = std::getenv("HOME");
    const std::string rsjfwRoot = home + "/.rsjfw";

    if (command == "install") {
        std::cout << "[RSJFW] Starting installation to " << rsjfwRoot << "...\n";
        rsjfw::Downloader downloader(rsjfwRoot);
        if (downloader.installLatest()) {
            std::cout << "[RSJFW] Installation complete!\n";
        } else {
            std::cerr << "[RSJFW] Installation failed.\n";
            return 1;
        }
        return 0;
    } else if (command == "launch") {
        std::vector<std::string> extraArgs;
        if (args.size() > 1) {
            for (size_t i = 1; i < args.size(); ++i) {
                std::string arg = args[i];
                // Vinegar style: check if it's a protocol
                if (arg.find("roblox-studio:") == 0 || arg.find("roblox-studio-auth:") == 0) {
                    extraArgs.push_back("-protocolString");
                    extraArgs.push_back(arg);
                } else {
                    extraArgs.push_back(arg);
                }
            }
        }

        std::cout << "[RSJFW] Preparing and launching Studio...\n";
        rsjfw::Launcher launcher(rsjfwRoot);
        launcher.setupPrefix();
        if (launcher.launchLatest(extraArgs)) {
            std::cout << "[RSJFW] Studio launched successfully!\n";
        } else {
            std::cerr << "[RSJFW] Launch failed.\n";
            return 1;
        }
        return 0;
    } else if (command == "kill") {
        std::cout << "[RSJFW] Terminating Studio...\n";
        rsjfw::Launcher launcher(rsjfwRoot);
        if (launcher.killStudio()) {
            std::cout << "[RSJFW] Studio terminated.\n";
        } else {
            std::cerr << "[RSJFW] Failed to terminate Studio.\n";
            return 1;
        }
        return 0;
    } else {
        std::cerr << "[RSJFW] Unknown command: " << command << "\n";
        showHelp();
        return 1;
    }

    return 0;
}
