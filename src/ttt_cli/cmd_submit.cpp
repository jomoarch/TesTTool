#include "ttt_cli.hpp"

#include "test_packager.hpp"
#include "test_result_file.hpp"
#include "test_runner.hpp"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

int cmd_submit(const CliArgs &args, const CliConfig &cfg) {
  if (!ensure_dir(cfg.bank_dir, "problem bank directory"))
    return 1;

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

  std::printf("testing '%s' (%d cases, %d ms, %.2f MB, %s)...\n", name.c_str(),
              pkg.test_count, conf.time_limit_ms, conf.memory_limit_mb,
              args.non_o2 ? "-O0" : "-O2");
  AssessmentResult result = run_tests(source, pkg, !args.non_o2);

  fs::path records_dir = problem_dir / "records";
  if (!ensure_dir(records_dir, "records directory"))
    return 1;
  fs::path result_path = make_result_path(records_dir);
  std::string err;
  if (!save_test_result(result_path, result, err)) {
    std::fprintf(stderr, "error: cannot save result: %s\n", err.c_str());
    return 1;
  }

  int id_w = 1, v_w = 1, time_w = 1, mem_w = 0;
  char mem_buf[32];
  for (const auto &c : result.cases) {
    id_w = std::max(id_w, static_cast<int>(std::to_string(c.id).size()));
    v_w = std::max(
        v_w, static_cast<int>(std::string(verdict_name(c.verdict)).size()));
    time_w =
        std::max(time_w, static_cast<int>(std::to_string(c.time_ms).size()));
    std::snprintf(mem_buf, sizeof(mem_buf), "%.2f", c.memory_mb);
    mem_w = std::max(mem_w, static_cast<int>(std::strlen(mem_buf)));
  }
  std::printf("compile: %s\n", result.compile_ok ? "OK" : "FAILED");
  if (!result.compile_ok) {
    std::printf("  %s\n", result.compile_message.c_str());
  }
  for (const auto &c : result.cases) {
    std::printf("  #%*d %-*s %*dms %*.2fMB", id_w, c.id, v_w,
                verdict_name(c.verdict), time_w, c.time_ms, mem_w, c.memory_mb);
    if (!c.message.empty())
      std::printf(" %s", c.message.c_str());
    std::printf("\n");
  }
  std::printf("result saved: %s\n", result_path.string().c_str());
  return 0;
}
