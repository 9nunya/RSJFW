#include "rsjfw/wine.hpp"
#include "rsjfw/logger.hpp"
#include <algorithm>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <sstream>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

namespace rsjfw {
namespace wine {

Prefix::Prefix(const std::string &root, const std::string &dir)
    : root_(root), dir_(dir) {
  if (dir_.empty()) {
    const char *home = getenv("HOME");
    dir_ = std::string(home ? home : ".") + "/.wine";
  }
}

bool Prefix::isProton() const {
  return std::filesystem::exists(std::filesystem::path(root_) / "proton");
}

std::string Prefix::bin(const std::string &prog) const {
  auto getPath = [&](const std::string &name) -> std::string {
    std::filesystem::path p(root_);
    if (isProton()) {
      return (p / "files" / "bin" / name).string();
    } else if (!root_.empty()) {
      if (std::filesystem::exists(p / "files/bin")) {
        return (p / "files" / "bin" / name).string();
      } else if (std::filesystem::exists(p / "dist/bin")) {
        return (p / "dist" / "bin" / name).string();
      } else {
        return (p / "bin" / name).string();
      }
    }
    return name;
  };

  if (prog == "wine") {
    std::string p64 = getPath("wine64");
    // Only check existence if root is set (valid path), otherwise "wine64" is
    // just the command
    if (!root_.empty() && std::filesystem::exists(p64))
      return p64;

    // If 64-bit not found, try 32-bit/standard
    return getPath("wine");
  }

  if (prog == "wine64") {
    std::string p64 = getPath("wine64");
    if (!root_.empty() && std::filesystem::exists(p64))
      return p64;

    // Fallback to "wine" if "wine64" is missing (e.g. Kombucha/Vinegar builds)
    std::string p32 = getPath("wine");
    if (!root_.empty() && std::filesystem::exists(p32))
      return p32;

    return p64; // Return original path even if missing (let execvp fail)
  }

  return getPath(prog);
}

void Prefix::setEnv(const std::map<std::string, std::string> &env) {
  env_ = env;
}

void Prefix::appendEnv(const std::string &key, const std::string &value) {
  env_[key] = value;
}

std::string Prefix::getEnv(const std::string &key) const {
  auto it = env_.find(key);
  if (it != env_.end())
    return it->second;
  const char *val = getenv(key.c_str());
  return val ? std::string(val) : "";
}

std::vector<std::string> Prefix::buildEnv() const {
  std::vector<std::string> finalEnv;
  std::map<std::string, std::string> currentMap;

  for (char **s = environ; *s; s++) {
    std::string str(*s);
    size_t pos = str.find('=');
    if (pos != std::string::npos) {
      std::string key = str.substr(0, pos);
      std::string val = str.substr(pos + 1);
      currentMap[key] = val;
    }
  }

  for (const auto &[key, val] : env_) {
    currentMap[key] = val;
  }

  if (!dir_.empty()) {
    currentMap["WINEPREFIX"] = dir_;
  }

  for (const auto &[key, val] : currentMap) {
    finalEnv.push_back(key + "=" + val);
  }

  return finalEnv;
}

bool Prefix::runCommand(const std::string &exe,
                        const std::vector<std::string> &args,
                        std::function<void(const std::string &)> onOutput,
                        const std::string &cwd, bool wait) {
  std::vector<std::string> finalArgs;
  finalArgs.push_back(exe);
  finalArgs.insert(finalArgs.end(), args.begin(), args.end());

  std::vector<char *> argv;
  for (const auto &s : finalArgs)
    argv.push_back(const_cast<char *>(s.c_str()));
  argv.push_back(nullptr);

  int pipefd[2];
  if (wait && onOutput) {
    if (pipe(pipefd) == -1)
      return false;
  }

  pid_t pid = fork();
  if (pid == -1)
    return false;

  if (pid == 0) {
    if (wait && onOutput) {
      close(pipefd[0]);
      dup2(pipefd[1], STDOUT_FILENO);
      dup2(pipefd[1], STDERR_FILENO);
      close(pipefd[1]);
    } else if (!wait) {
      // Redirect to /dev/null for detached processes to avoid hanging term
      int devNull = open("/dev/null", O_RDWR);
      if (devNull != -1) {
        dup2(devNull, STDOUT_FILENO);
        dup2(devNull, STDERR_FILENO);
        close(devNull);
      }
    }

    if (!cwd.empty()) {
      if (chdir(cwd.c_str()) != 0) {
        perror("chdir");
      }
    }

    for (const auto &[key, val] : env_) {
      setenv(key.c_str(), val.c_str(), 1);
    }

    if (!dir_.empty()) {
      setenv("WINEPREFIX", dir_.c_str(), 1);
    }

    execvp(exe.c_str(), argv.data());

    std::cerr << "Failed to exec: " << exe << "\n";
    _exit(127);
  }

  if (wait) {
    if (onOutput) {
      close(pipefd[1]);
      FILE *stream = fdopen(pipefd[0], "r");
      if (stream) {
        char buffer[1024];
        while (fgets(buffer, sizeof(buffer), stream)) {
          onOutput(std::string(buffer));
        }
        fclose(stream);
      }
      close(pipefd[0]);
    }

    int status;
    waitpid(pid, &status, 0);

    if (WIFEXITED(status)) {
      int code = WEXITSTATUS(status);
      if (code != 0) {
        std::cerr << "[RSJFW] Process exited with code: " << code << "\n";
      }
      return code == 0;
    } else if (WIFSIGNALED(status)) {
      int sig = WTERMSIG(status);
      std::cerr << "[RSJFW] Process terminated by signal: " << sig << "\n";
      return false;
    }
    return false;
  }

  return true; // Successfully forked in detached mode
}

bool Prefix::wine(const std::string &target,
                  const std::vector<std::string> &args,
                  std::function<void(const std::string &)> onOutput,
                  const std::string &cwd, bool wait) {
  std::string wineBin = "wine64"; // Default to 64-bit
  bool protonMode = isProton();

  if (protonMode) {
    wineBin = root_ + "/proton";
  } else if (!root_.empty()) {
    wineBin = bin("wine64");
  }

  std::cerr << "[RSJFW-DEBUG] isProton: " << (protonMode ? "true" : "false")
            << "\n";
  std::cerr << "[RSJFW-DEBUG] Resolved wineBin: " << wineBin << "\n";

  std::vector<std::string> finalArgs;
  if (protonMode) {
    finalArgs.push_back("run");
  }
  finalArgs.push_back(target);
  finalArgs.insert(finalArgs.end(), args.begin(), args.end());

  std::string fullCmd = wineBin;
  for (const auto &a : finalArgs)
    fullCmd += " \"" + a + "\"";
  std::cerr << "[RSJFW-DEBUG] Full Command: " << fullCmd << "\n";

  return runCommand(wineBin, finalArgs, onOutput, cwd, wait);
}

bool Prefix::registryAdd(const std::string &key, const std::string &valueName,
                         const std::string &value, const std::string &type) {
  std::vector<std::string> args = {"reg", "add", key, "/f"};
  if (!valueName.empty()) {
    args.push_back("/v");
    args.push_back(valueName);
  } else {
    args.push_back("/ve");
  }

  if (!type.empty()) {
    args.push_back("/t");
    args.push_back(type);
  }

  if (!value.empty()) {
    args.push_back("/d");
    args.push_back(value);
  }

  return wine("reg", args, [](const std::string &s) {});
}

bool Prefix::kill() { return wine("wineserver", {"-k"}); }

bool Prefix::registryApply(const std::vector<RegistryEntry> &entries) {
  if (entries.empty())
    return true;

  std::stringstream ss;
  ss << "Windows Registry Editor Version 5.00\r\n\r\n";

  std::string lastKey = "";
  for (const auto &entry : entries) {
    if (entry.key != lastKey) {
      ss << "[" << entry.key << "]\r\n";
      lastKey = entry.key;
    }

    std::string name =
        entry.valueName.empty() ? "@" : "\"" + entry.valueName + "\"";
    ss << name << "=";

    if (entry.type == "REG_DWORD") {
      try {
        unsigned int val = std::stoul(entry.value, nullptr, 0);
        char buf[16];
        snprintf(buf, sizeof(buf), "dword:%08x", val);
        ss << buf;
      } catch (...) {
        ss << "dword:00000000";
      }
    } else if (entry.type == "REG_BINARY") {
      ss << "hex:" << entry.value; // Expecting hex string format
    } else {
      // Default to string
      std::string escaped = entry.value;
      // Basic escaping for .reg
      size_t pos = 0;
      while ((pos = escaped.find('\\', pos)) != std::string::npos) {
        escaped.replace(pos, 1, "\\\\");
        pos += 2;
      }
      pos = 0;
      while ((pos = escaped.find('"', pos)) != std::string::npos) {
        escaped.replace(pos, 1, "\\\"");
        pos += 2;
      }
      ss << "\"" << escaped << "\"";
    }
    ss << "\r\n";
  }

  std::filesystem::path tempReg =
      std::filesystem::path(dir_) / "rsjfw_batch.reg";
  std::ofstream ofs(tempReg);
  if (!ofs)
    return false;
  ofs << ss.str();
  ofs.close();

  bool res = wine("regedit", {"/s", "Z:" + tempReg.string()});
  std::filesystem::remove(tempReg);
  return res;
}

} // namespace wine
} // namespace rsjfw
