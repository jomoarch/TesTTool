#include "ttt_cli.hpp"

#include "file_picker.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <map>
#include <random>
#include <string>

#if defined(_WIN32)
#include <io.h>
#include <process.h>
#define READ_FD _read
#else
#include <unistd.h>
#define READ_FD ::read
#endif

namespace fs = std::filesystem;

std::string trim(const std::string &s) {
  size_t b = 0, e = s.size();
  while (b < e && (s[b] == ' ' || s[b] == '\t'))
    ++b;
  while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t'))
    --e;
  return s.substr(b, e - b);
}

bool read_line_stdin(std::string &out) {
  out.clear();
  char c;
  while (true) {
    int n = READ_FD(0, &c, 1);
    if (n <= 0)
      return !out.empty();
    if (c == '\n')
      break;
    if (c != '\r')
      out.push_back(c);
  }
  return true;
}

bool prompt_line(const std::string &hint, std::string &out) {
  std::printf("%s", hint.c_str());
  std::fflush(stdout);
  return read_line_stdin(out);
}

bool valid_name(const std::string &name) {
  if (name.empty() || name == "." || name == "..")
    return false;
  return name.find('/') == std::string::npos &&
         name.find('\\') == std::string::npos;
}

fs::path pick_with_picker(const fs::path &initial, bool want_dir) {
  SelectionOptions opts;
  opts.initial_path = initial.empty() ? fs::current_path() : initial;
  opts.mode =
      want_dir ? SelectionMode::SINGLE_DIRECTORY : SelectionMode::SINGLE_FILE;
  opts.allow_files = !want_dir;
  opts.allow_directories = want_dir;
  SelectionResult r = pick_files(opts);
  if (!r.ok || r.paths.empty())
    return {};
  return r.paths[0];
}

fs::path ask_problem(const CliConfig &cfg) {
  std::string line;
  if (!prompt_line("Problem name (Enter to pick from bank with file picker): ",
                   line))
    return {};
  line = trim(line);
  if (!line.empty())
    return fs::path(line);
  fs::path picked = pick_with_picker(cfg.bank_dir, true);
  if (picked.empty())
    return {};
  return picked.filename();
}

bool ensure_dir(const fs::path &dir, const char *what) {
  std::error_code ec;
  fs::create_directories(dir, ec);
  if (ec) {
    std::fprintf(stderr, "error: cannot create %s: %s (%s)\n", what,
                 dir.string().c_str(), ec.message().c_str());
    return false;
  }
  return true;
}

fs::path make_result_path(const fs::path &records_dir) {
  std::random_device rd;
  static const char kHex[] = "0123456789abcdef";
  std::string rand;
  for (int i = 0; i < 8; ++i)
    rand.push_back(kHex[rd() % 16]);

  std::time_t t = std::time(nullptr);
  std::tm tmv{};
#if defined(_WIN32)
  localtime_s(&tmv, &t);
#else
  localtime_r(&t, &tmv);
#endif
  char ts[32];
  std::strftime(ts, sizeof(ts), "%Y%m%d_%H%M%S", &tmv);

  fs::path p =
      records_dir / ("result_" + std::string(ts) + "_" + rand + ".txt");
  int tries = 0;
  while (fs::exists(p) && tries++ < 8) {
    rand.clear();
    for (int i = 0; i < 8; ++i)
      rand.push_back(kHex[rd() % 16]);
    p = records_dir / ("result_" + std::string(ts) + "_" + rand + ".txt");
  }
  return p;
}

bool parse_args(int argc, char **argv, CliArgs &out) {
  for (int i = 0; i < argc; ++i) {
    std::string a = argv[i];
    if (a.rfind("--", 0) != 0) {
      std::fprintf(stderr, "error: unexpected argument: %s\n", a.c_str());
      return false;
    }
    std::string key = a.substr(2);
    std::string val;
    size_t eq = key.find('=');
    if (eq != std::string::npos) {
      val = key.substr(eq + 1);
      key = key.substr(0, eq);
    } else if (i + 1 < argc && std::string(argv[i + 1]).rfind("--", 0) != 0) {
      val = argv[++i];
    }
    if (key == "non-o2") {
      out.non_o2 = true;
    } else if (key.empty()) {
      std::fprintf(stderr, "error: empty option name\n");
      return false;
    } else {
      out.values[key] = val;
    }
  }
  return true;
}

void print_usage(const char *prog) {
  std::printf(
      "Usage:\n"
      "  %s add    [--name <name>] [--dir <dir_path>] [--time-limit <ms>] "
      "[--memory-limit <mb>]\n"
      "  %s submit [--name <name>] [--source <source_path>] [--non-o2]\n"
      "  %s view   [--name <name>] [--id <test_id>]\n"
      "  %s remove [--name <name>]\n"
      "\n"
      "Unspecified options are asked interactively; pressing Enter at a "
      "prompt\n"
      "opens the full-screen file picker where applicable.\n",
      prog, prog, prog, prog);
}

bool load_problem_conf(const fs::path &dir, ProblemConf &out) {
  out = ProblemConf{};
  std::map<std::string, std::string> kv;
  std::string error;
  if (!read_kv_file(dir / "problem.conf", kv, error))
    return false;
  auto it = kv.find("time_limit_ms");
  if (it != kv.end()) {
    try {
      int v = std::stoi(it->second);
      if (v > 0)
        out.time_limit_ms = v;
    } catch (...) {
    }
  }
  it = kv.find("memory_limit_mb");
  if (it != kv.end()) {
    try {
      float v = std::stof(it->second);
      if (v > 0)
        out.memory_limit_mb = v;
    } catch (...) {
    }
  }
  return true;
}

bool save_problem_conf(const fs::path &dir, const ProblemConf &conf,
                       std::string &error) {
  std::map<std::string, std::string> kv;
  kv["time_limit_ms"] = std::to_string(conf.time_limit_ms);
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%.2f", conf.memory_limit_mb);
  kv["memory_limit_mb"] = buf;
  return write_kv_file(dir / "problem.conf", kv, error);
}
