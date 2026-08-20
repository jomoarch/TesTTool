#include "ttt_cli.hpp"

#include "test_packager.hpp"

#include <cstdio>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

namespace {

std::string format_number(int v) { return std::to_string(v); }

std::string format_number(float v) {
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%.2f", v);
  return buf;
}

bool parse_positive_int(const std::string &s, int &v) {
  try {
    int x = std::stoi(s);
    if (x <= 0)
      return false;
    v = x;
    return true;
  } catch (...) {
    return false;
  }
}

bool parse_positive_float(const std::string &s, float &v) {
  try {
    float x = std::stof(s);
    if (x <= 0)
      return false;
    v = x;
    return true;
  } catch (...) {
    return false;
  }
}

// 交互询问一个正数：提示中显示默认值，回车（空输入）用默认
// 输入非法时打印错误并重新询问；EOF（Ctrl-D）返回 false
// parse 把字符串解析到 out，非法返回 false
template <typename T>
bool ask_positive(const char *name, const char *unit, const T &def, T &out,
                  bool (*parse)(const std::string &, T &)) {
  for (;;) {
    std::string hint = std::string(name) + " (" + unit + ", default " +
                       format_number(def) + "): ";
    std::string line;
    if (!prompt_line(hint, line))
      return false;
    line = trim(line);
    if (line.empty()) {
      out = def;
      return true;
    }
    if (parse(line, out))
      return true;
    std::fprintf(stderr, "error: invalid %s: %s\n", name, line.c_str());
  }
}

} // namespace

int cmd_add(const CliArgs &args, const CliConfig &cfg) {
  if (!ensure_dir(cfg.bank_dir, "problem bank directory"))
    return 1;

  std::string name = args.values.count("name") ? args.values.at("name") : "";
  if (name.empty()) {
    std::string line;
    if (!prompt_line("Problem name: ", line))
      return 1;
    name = trim(line);
  }
  if (!valid_name(name)) {
    std::fprintf(
        stderr,
        "error: invalid problem name (must not contain '/' or '\\\\'): %s\n",
        name.c_str());
    return 1;
  }

  fs::path problem_dir = cfg.bank_dir / name;

  if (fs::exists(problem_dir)) {
    std::string line;
    std::printf("problem '%s' already exists.\n", name.c_str());
    if (!prompt_line("delete it and re-add? [y/N]: ", line))
      return 1;
    line = trim(line);
    if (line != "y" && line != "Y") {
      std::printf("cancelled.\n");
      return 1;
    }
    std::error_code ec;
    fs::remove_all(problem_dir, ec);
    if (ec) {
      std::fprintf(stderr, "error: cannot remove existing problem: %s (%s)\n",
                   problem_dir.string().c_str(), ec.message().c_str());
      return 1;
    }
  }

  fs::path data_dir;
  std::string dir_arg = args.values.count("dir") ? args.values.at("dir") : "";
  if (!dir_arg.empty()) {
    data_dir = fs::path(dir_arg);
  } else {
    std::string line;
    if (!prompt_line("Test data folder (Enter to pick with file picker): ",
                     line))
      return 1;
    line = trim(line);
    if (line.empty()) {
      data_dir = pick_with_picker(fs::current_path(), true);
      if (data_dir.empty()) {
        std::printf("cancelled.\n");
        return 1;
      }
    } else {
      data_dir = fs::path(line);
    }
  }
  std::error_code ec;
  if (!fs::is_directory(data_dir, ec)) {
    std::fprintf(
        stderr,
        "error: test data folder does not exist or is not a directory: %s\n",
        data_dir.string().c_str());
    return 1;
  }

  int time_limit = cfg.default_time_limit_ms;
  float memory_limit = cfg.default_memory_limit_mb;
  if (args.values.count("time-limit")) {
    try {
      int v = std::stoi(args.values.at("time-limit"));
      if (v <= 0)
        throw std::out_of_range("non-positive");
      time_limit = v;
    } catch (...) {
      std::fprintf(stderr, "error: invalid --time-limit: %s\n",
                   args.values.at("time-limit").c_str());
      return 1;
    }
  } else {
    int v = 0;
    if (!ask_positive("Time limit", "ms", time_limit, v, parse_positive_int))
      return 1;
    time_limit = v;
  }
  if (args.values.count("memory-limit")) {
    try {
      float v = std::stof(args.values.at("memory-limit"));
      if (v <= 0)
        throw std::out_of_range("non-positive");
      memory_limit = v;
    } catch (...) {
      std::fprintf(stderr, "error: invalid --memory-limit: %s\n",
                   args.values.at("memory-limit").c_str());
      return 1;
    }
  } else {
    float v = 0.0f;
    if (!ask_positive("Memory limit", "MB", memory_limit, v,
                      parse_positive_float))
      return 1;
    memory_limit = v;
  }

  if (!ensure_dir(problem_dir, "problem directory"))
    return 1;
  fs::path tests_dir = problem_dir / "tests";
  if (!ensure_dir(tests_dir, "tests directory"))
    return 1;

  std::string warnings;
  for (const auto &de : fs::directory_iterator(data_dir, ec)) {
    if (ec) {
      warnings += "read error: " + ec.message() + "\n";
      ec.clear();
      continue;
    }
    if (de.is_directory()) {
      warnings +=
          "skipped sub-directory: " + de.path().filename().string() + "\n";
      continue;
    }
    if (!de.is_regular_file())
      continue;
    fs::copy_file(de.path(), tests_dir / de.path().filename(),
                  fs::copy_options::overwrite_existing, ec);
    if (ec) {
      warnings += "copy failed: " + de.path().filename().string() + ": " +
                  ec.message() + "\n";
      ec.clear();
    }
  }

  ProblemConf conf{time_limit, memory_limit};
  std::string err;
  if (!save_problem_conf(problem_dir, conf, err)) {
    std::fprintf(stderr, "error: cannot write problem.conf: %s\n", err.c_str());
    return 1;
  }

  TestPackage pkg = pack_tests(name, tests_dir, time_limit, memory_limit);
  if (!pkg.success) {
    warnings += "test data validation failed: " + pkg.message + "\n";
  }

  if (!warnings.empty()) {
    std::fprintf(stderr, "warnings:\n%s", warnings.c_str());
  }

  std::printf("problem added: %s\n", problem_dir.string().c_str());
  std::printf("  tests: %d  time-limit: %d ms  memory-limit: %.2f MB\n",
              pkg.success ? pkg.test_count : 0, time_limit, memory_limit);
  return 0;
}
