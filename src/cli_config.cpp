#include "cli_config.hpp"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace fs = std::filesystem;

namespace {

std::string trim(const std::string &s) {
  size_t b = 0, e = s.size();
  while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
  while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
  return s.substr(b, e - b);
}

std::string home_dir() {
#if defined(_WIN32)
  const char *a = std::getenv("APPDATA");
  if (a && *a) return a;
  const char *u = std::getenv("USERPROFILE");
  if (u && *u) return u;
  return ".";
#else
  const char *h = std::getenv("HOME");
  if (h && *h) return h;
  return ".";
#endif
}

} // namespace

bool read_kv_file(const std::filesystem::path &path,
                  std::map<std::string, std::string> &kv, std::string &error) {
  kv.clear();
  std::ifstream f(path);
  if (!f) {
    error.clear();  // 不存在：由调用方决定
    return false;
  }
  std::string line;
  while (std::getline(f, line)) {
    line = trim(line);
    if (line.empty() || line[0] == '#') continue;
    size_t eq = line.find('=');
    if (eq == std::string::npos) continue;  // 宽容：忽略无 '=' 的行
    std::string key = trim(line.substr(0, eq));
    std::string val = trim(line.substr(eq + 1));
    if (!key.empty()) kv[key] = val;
  }
  if (f.bad()) {
    error = "read failed: " + path.string();
    return false;
  }
  return true;
}

bool write_kv_file(const std::filesystem::path &path,
                   const std::map<std::string, std::string> &kv, std::string &error) {
  std::error_code ec;
  fs::create_directories(path.parent_path(), ec);
  if (ec) {
    error = "cannot create directory: " + path.parent_path().string() +
            " (" + ec.message() + ")";
    return false;
  }
  std::ofstream f(path, std::ios::trunc);
  if (!f) {
    error = "cannot open for writing: " + path.string();
    return false;
  }
  for (const auto &p : kv) {
    f << p.first << " = " << p.second << "\n";
  }
  if (!f) {
    error = "write failed: " + path.string();
    return false;
  }
  return true;
}

std::filesystem::path default_config_path() {
  if (const char *env = std::getenv("TTT_CLI_CONFIG")) {
    if (*env) return fs::path(env);
  }
#if defined(_WIN32)
  return fs::path(home_dir()) / "ttt-cli" / "config.txt";
#else
  const char *xdg = std::getenv("XDG_CONFIG_HOME");
  if (xdg && *xdg) return fs::path(xdg) / "ttt-cli" / "config.txt";
  return fs::path(home_dir()) / ".config" / "ttt-cli" / "config.txt";
#endif
}

bool load_config(const std::filesystem::path &path, CliConfig &cfg, std::string &error) {
  std::map<std::string, std::string> kv;
  if (!read_kv_file(path, kv, error)) return false;
  cfg = CliConfig{};
  auto it = kv.find("bank_dir");
  if (it != kv.end() && !it->second.empty()) cfg.bank_dir = it->second;
  it = kv.find("default_time_limit_ms");
  if (it != kv.end()) {
    try {
      int v = std::stoi(it->second);
      if (v > 0) cfg.default_time_limit_ms = v;
    } catch (...) {
    }
  }
  it = kv.find("default_memory_limit_mb");
  if (it != kv.end()) {
    try {
      float v = std::stof(it->second);
      if (v > 0) cfg.default_memory_limit_mb = v;
    } catch (...) {
    }
  }
  if (cfg.bank_dir.empty()) cfg.bank_dir = fs::path(home_dir()) / "ttt-bank";
  return true;
}

bool save_config(const std::filesystem::path &path, const CliConfig &cfg,
                 std::string &error) {
  std::map<std::string, std::string> kv;
  kv["bank_dir"] = cfg.bank_dir.string();
  kv["default_time_limit_ms"] = std::to_string(cfg.default_time_limit_ms);
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%.2f", cfg.default_memory_limit_mb);
  kv["default_memory_limit_mb"] = buf;
  return write_kv_file(path, kv, error);
}

bool ensure_config(const std::filesystem::path &path, CliConfig &cfg,
                   std::string &error) {
  if (fs::exists(path)) return load_config(path, cfg, error);
  // 创建默认配置
  CliConfig def;
  def.bank_dir = fs::path(home_dir()) / "ttt-bank";
  if (!save_config(path, def, error)) return false;
  cfg = def;
  return true;
}
