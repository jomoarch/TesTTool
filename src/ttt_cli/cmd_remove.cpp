#include "ttt_cli.hpp"

#include <cstdio>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

int cmd_remove(const CliArgs &args, const CliConfig &cfg) {
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
  if (!fs::exists(problem_dir)) {
    std::fprintf(stderr, "error: problem not found in bank: %s\n",
                 name.c_str());
    return 1;
  }

  std::error_code ec;
  fs::remove_all(problem_dir, ec);
  if (ec) {
    std::fprintf(stderr, "error: cannot remove problem: %s (%s)\n",
                 problem_dir.string().c_str(), ec.message().c_str());
    return 1;
  }
  std::printf("problem removed: %s\n", problem_dir.string().c_str());
  return 0;
}
