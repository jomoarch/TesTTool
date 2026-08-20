#include "ttt_cli.hpp"

#include "test_result_viewer.hpp"

#include <cstdio>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

int cmd_view(const CliArgs &args, const CliConfig &cfg) {
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
  fs::path records_dir = problem_dir / "records";

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
