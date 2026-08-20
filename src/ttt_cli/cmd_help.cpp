#include "ttt_cli.hpp"

#include "helpfile.hpp"

#include <cstdio>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

int cmd_help(const CliArgs &, const CliConfig &) {
  fs::path hp = fp::find_nearby_file("ttt_cli_help.txt");
  if (hp.empty()) {
    std::fprintf(stderr,
                 "error: help file not found (ttt_cli_help.txt, expected in "
                 "docs/ or next to the executable)\n");
    return 1;
  }
  FILE *f = std::fopen(hp.string().c_str(), "rb");
  if (!f) {
    std::fprintf(stderr, "error: cannot open help file: %s\n",
                 hp.string().c_str());
    return 1;
  }
  char buf[4096];
  size_t n;
  while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) {
    std::fwrite(buf, 1, n, stdout);
  }
  std::fclose(f);
  return 0;
}
