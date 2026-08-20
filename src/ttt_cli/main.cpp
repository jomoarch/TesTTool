#include "ttt_cli.hpp"

#include <cstdio>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

namespace {

struct CommandEntry {
  const char *name;
  int (*fn)(const CliArgs &, const CliConfig &);
};

const CommandEntry kCommands[] = {
    {"add", cmd_add},
    {"submit", cmd_submit},
    {"view", cmd_view},
    {"remove", cmd_remove},
    {"help", cmd_help},
};

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

  fs::path cfg_path = default_config_path();
  CliConfig cfg;
  std::string err;
  if (!ensure_config(cfg_path, cfg, err)) {
    std::fprintf(stderr, "error: cannot initialize config: %s\n", err.c_str());
    return 1;
  }

  for (const auto &e : kCommands) {
    if (cmd == e.name)
      return e.fn(args, cfg);
  }

  std::fprintf(stderr, "error: unknown command: %s\n", cmd.c_str());
  print_usage(argv[0]);
  return 2;
}
