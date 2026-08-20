// ttt-cli：测评机命令行工具
//
//   ttt-cli add    [--name <name>] [--dir <dir_path>] [--time-limit <ms>]
//   [--memory-limit <mb>] ttt-cli submit [--name <name>] [--source
//   <source_path>] [--non-o2] ttt-cli view   [--name <name>] [--id <test_id>]
//
// 题库结构（配置中 bank_dir）：
//   <bank>/<name>/problem.conf   该题时间/内存限制（key = value）
//   <bank>/<name>/tests/         测试数据（.in/.out，add 时从 --dir 复制）
//   <bank>/<name>/records/       submit 生成的结果文件（随机文件名）
//
// 未指定参数时交互式询问；提示处直接回车会调用 file_picker（全屏文件选择器）
// 配置文件：见 cli_config.hpp（默认
// ~/.config/ttt-cli/config.txt，TTT_CLI_CONFIG 可覆盖）
#include "cli_config.hpp"
#include "file_picker.hpp"
#include "test_packager.hpp"
#include "test_result_file.hpp"
#include "test_result_viewer.hpp"
#include "test_runner.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <random>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <io.h>
#include <process.h>
#define READ_FD _read
#else
#include <unistd.h>
#define READ_FD ::read
#endif

namespace fs = std::filesystem;

namespace {

// ---- 小工具 ----

std::string trim(const std::string &s) {
  size_t b = 0, e = s.size();
  while (b < e && (s[b] == ' ' || s[b] == '\t'))
    ++b;
  while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t'))
    --e;
  return s.substr(b, e - b);
}

// 从 stdin 读一行（去掉 \r\n）。EOF 时返回 false
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

// 打印提示并读一行。返回 false 表示 EOF（用户中断）
bool prompt_line(const std::string &hint, std::string &out) {
  std::printf("%s", hint.c_str());
  std::fflush(stdout);
  return read_line_stdin(out);
}

// 文件名合法性：不允许路径分隔符与 "." ".."，防止逃出题库目录
bool valid_name(const std::string &name) {
  if (name.empty() || name == "." || name == "..")
    return false;
  return name.find('/') == std::string::npos &&
         name.find('\\') == std::string::npos;
}

// 文件选择器封装：want_dir=true 选文件夹，否则选文件。取消返回空路径
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

// 询问题目名称；回车时用 file_picker 从题库选择（只保留末级文件夹名）
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

// 递归创建目录；失败打印错误并返回 false
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

// 生成唯一结果文件名：result_<时间戳>_<8位随机hex>.txt
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
  // 极小概率冲突：重试
  int tries = 0;
  while (fs::exists(p) && tries++ < 8) {
    rand.clear();
    for (int i = 0; i < 8; ++i)
      rand.push_back(kHex[rd() % 16]);
    p = records_dir / ("result_" + std::string(ts) + "_" + rand + ".txt");
  }
  return p;
}

// ---- 参数解析 ----

struct CliArgs {
  std::map<std::string, std::string> values;
  bool non_o2 = false;
};

// 支持 "--key value" 与 "--key=value"；"--non-o2" 为无值开关
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

// ---- 每题配置（problem.conf）----

struct ProblemConf {
  int time_limit_ms = 0;
  float memory_limit_mb = 0.0f;
};

// 读取 <dir>/problem.conf；文件不存在返回 false（调用方回退全局默认）
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

// ---- add ----

int cmd_add(const CliArgs &args, const CliConfig &cfg) {
  // 题库目录（不存在则创建）
  if (!ensure_dir(cfg.bank_dir, "problem bank directory"))
    return 1;

  // 名称
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

  // 已存在：询问是否删除
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

  // 测试数据目录
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

  // 限制（未指定用全局默认）
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
  }

  // 建目录并复制测试数据
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

  // 写入该题限制
  ProblemConf conf{time_limit, memory_limit};
  std::string err;
  if (!save_problem_conf(problem_dir, conf, err)) {
    std::fprintf(stderr, "error: cannot write problem.conf: %s\n", err.c_str());
    return 1;
  }

  // 校验测试数据（失败只警告，不回滚）
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

// ---- submit ----

int cmd_submit(const CliArgs &args, const CliConfig &cfg) {
  if (!ensure_dir(cfg.bank_dir, "problem bank directory"))
    return 1;

  // 题目名称
  std::string name = args.values.count("name") ? args.values.at("name") : "";
  if (name.empty()) {
    fs::path p = ask_problem(cfg);
    if (p.empty()) {
      std::printf("cancelled.\n");
      return 1;
    }
    name = p.string();
  }
  if (!valid_name(name)) {
    std::fprintf(stderr, "error: invalid problem name: %s\n", name.c_str());
    return 1;
  }
  fs::path problem_dir = cfg.bank_dir / name;
  if (!fs::is_directory(problem_dir)) {
    std::fprintf(stderr, "error: problem not found in bank: %s\n",
                 name.c_str());
    return 1;
  }

  // 该题限制（problem.conf 缺失时回退全局默认）
  ProblemConf conf;
  if (load_problem_conf(problem_dir, conf)) {
    if (conf.time_limit_ms <= 0)
      conf.time_limit_ms = cfg.default_time_limit_ms;
    if (conf.memory_limit_mb <= 0)
      conf.memory_limit_mb = cfg.default_memory_limit_mb;
  } else {
    conf.time_limit_ms = cfg.default_time_limit_ms;
    conf.memory_limit_mb = cfg.default_memory_limit_mb;
  }

  // 源代码
  fs::path source;
  std::string src_arg =
      args.values.count("source") ? args.values.at("source") : "";
  if (!src_arg.empty()) {
    source = fs::path(src_arg);
  } else {
    std::string line;
    if (!prompt_line("Source file (Enter to pick with file picker): ", line))
      return 1;
    line = trim(line);
    if (line.empty()) {
      source = pick_with_picker(fs::current_path(), false);
      if (source.empty()) {
        std::printf("cancelled.\n");
        return 1;
      }
    } else {
      source = fs::path(line);
    }
  }
  std::error_code ec;
  if (!fs::is_regular_file(source, ec)) {
    std::fprintf(
        stderr,
        "error: source file does not exist or is not a regular file: %s\n",
        source.string().c_str());
    return 1;
  }

  // 测试点
  fs::path tests_dir = problem_dir / "tests";
  if (!fs::is_directory(tests_dir, ec)) {
    std::fprintf(stderr, "error: problem has no tests directory: %s\n",
                 tests_dir.string().c_str());
    return 1;
  }
  TestPackage pkg =
      pack_tests(name, tests_dir, conf.time_limit_ms, conf.memory_limit_mb);
  if (!pkg.success) {
    std::fprintf(stderr, "error: cannot pack tests: %s\n", pkg.message.c_str());
    return 1;
  }

  // 测评（--non-o2 禁用优化编译）
  std::printf("testing '%s' (%d cases, %d ms, %.2f MB, %s)...\n", name.c_str(),
              pkg.test_count, conf.time_limit_ms, conf.memory_limit_mb,
              args.non_o2 ? "-O0" : "-O2");
  AssessmentResult result = run_tests(source, pkg, !args.non_o2);

  // 保存结果（随机文件名）
  fs::path records_dir = problem_dir / "records";
  if (!ensure_dir(records_dir, "records directory"))
    return 1;
  fs::path result_path = make_result_path(records_dir);
  std::string err;
  if (!save_test_result(result_path, result, err)) {
    std::fprintf(stderr, "error: cannot save result: %s\n", err.c_str());
    return 1;
  }

  // 摘要
  std::printf("compile: %s\n", result.compile_ok ? "OK" : "FAILED");
  if (!result.compile_ok) {
    std::printf("  %s\n", result.compile_message.c_str());
  }
  for (const auto &c : result.cases) {
    std::printf("  #%d %s %dms %.2fMB", c.id, verdict_name(c.verdict),
                c.time_ms, c.memory_mb);
    if (!c.message.empty())
      std::printf(" %s", c.message.c_str());
    std::printf("\n");
  }
  std::printf("result saved: %s\n", result_path.string().c_str());
  return 0;
}

// ---- view ----

int cmd_view(const CliArgs &args, const CliConfig &cfg) {
  if (!ensure_dir(cfg.bank_dir, "problem bank directory"))
    return 1;

  // 题目名称
  std::string name = args.values.count("name") ? args.values.at("name") : "";
  if (name.empty()) {
    fs::path p = ask_problem(cfg);
    if (p.empty()) {
      std::printf("cancelled.\n");
      return 1;
    }
    name = p.string();
  }
  if (!valid_name(name)) {
    std::fprintf(stderr, "error: invalid problem name: %s\n", name.c_str());
    return 1;
  }
  fs::path problem_dir = cfg.bank_dir / name;
  if (!fs::is_directory(problem_dir)) {
    std::fprintf(stderr, "error: problem not found in bank: %s\n",
                 name.c_str());
    return 1;
  }
  fs::path records_dir = problem_dir / "records";

  // 结果文件
  fs::path result_file;
  std::string id_arg = args.values.count("id") ? args.values.at("id") : "";
  if (!id_arg.empty()) {
    result_file = fs::path(id_arg);
  } else {
    std::string line;
    if (!prompt_line("Result file (Enter to pick with file picker): ", line))
      return 1;
    line = trim(line);
    if (line.empty()) {
      fs::path initial =
          fs::is_directory(records_dir) ? records_dir : problem_dir;
      result_file = pick_with_picker(initial, false);
      if (result_file.empty()) {
        std::printf("cancelled.\n");
        return 1;
      }
    } else {
      result_file = fs::path(line);
    }
  }

  // 解析：绝对路径直接使用；否则依次尝试 records/、题目目录下的相对路径
  std::error_code ec;
  if (!fs::is_regular_file(result_file, ec)) {
    fs::path rel = result_file;
    if (fs::is_regular_file(records_dir / rel)) {
      result_file = records_dir / rel;
    } else if (fs::is_regular_file(problem_dir / rel)) {
      result_file = problem_dir / rel;
    } else {
      std::fprintf(stderr, "error: result file not found: %s\n",
                   result_file.string().c_str());
      return 1;
    }
  }

  return view_test_result(result_file);
}

void print_usage(const char *prog) {
  std::printf(
      "Usage:\n"
      "  %s add    [--name <name>] [--dir <dir_path>] [--time-limit <ms>] "
      "[--memory-limit <mb>]\n"
      "  %s submit [--name <name>] [--source <source_path>] [--non-o2]\n"
      "  %s view   [--name <name>] [--id <test_id>]\n"
      "\n"
      "Unspecified options are asked interactively; pressing Enter at a "
      "prompt\n"
      "opens the full-screen file picker where applicable.\n",
      prog, prog, prog);
}

} // namespace

int main(int argc, char **argv) {
  if (argc < 2) {
    print_usage(argv[0]);
    return 2;
  }
  std::string cmd = argv[1];
  CliArgs args;
  if (!parse_args(argc - 2, argv + 2, args)) {
    print_usage(argv[0]);
    return 2;
  }

  // 配置文件（不存在则创建默认）
  fs::path cfg_path = default_config_path();
  CliConfig cfg;
  std::string err;
  if (!ensure_config(cfg_path, cfg, err)) {
    std::fprintf(stderr, "error: cannot initialize config: %s\n", err.c_str());
    return 1;
  }

  if (cmd == "add")
    return cmd_add(args, cfg);
  if (cmd == "submit")
    return cmd_submit(args, cfg);
  if (cmd == "view")
    return cmd_view(args, cfg);

  std::fprintf(stderr, "error: unknown command: %s\n", cmd.c_str());
  print_usage(argv[0]);
  return 2;
}
