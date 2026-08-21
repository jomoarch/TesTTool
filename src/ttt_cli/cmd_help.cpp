#include "ttt_cli.hpp"

#include "helpfile.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace fs = std::filesystem;

namespace {

// Windows 控制台输出代码页临时切到 UTF-8，析构时恢复；Linux/macOS 为无操作
class ConsoleUtf8 {
public:
  ConsoleUtf8() {
#if defined(_WIN32)
    saved_cp_ = ::GetConsoleOutputCP();
    if (saved_cp_ != 0)
      ::SetConsoleOutputCP(CP_UTF8);
#endif
  }
  ~ConsoleUtf8() {
#if defined(_WIN32)
    if (saved_cp_ != 0)
      ::SetConsoleOutputCP(saved_cp_);
#endif
  }
  ConsoleUtf8(const ConsoleUtf8 &) = delete;
  ConsoleUtf8 &operator=(const ConsoleUtf8 &) = delete;

private:
#if defined(_WIN32)
  unsigned int saved_cp_ = 0;
#endif
};

} // namespace

int cmd_help(const CliArgs &, const CliConfig &) {
  fs::path hp = fp::find_nearby_file("ttt_cli_help.txt");
  if (hp.empty()) {
    std::fprintf(stderr,
                 "error: help file not found (ttt_cli_help.txt, expected in "
                 "docs/ or next to the executable)\n");
    return 1;
  }
  // 直接以 path 打开（Windows 下对宽字符路径安全），按字节流输出
  std::ifstream f(hp, std::ios::binary);
  if (!f) {
    std::fprintf(stderr, "error: cannot open help file: %s\n",
                 hp.string().c_str());
    return 1;
  }
  ConsoleUtf8 utf8; // Windows：输出代码页临时切 UTF-8，中文不乱码
  char buf[4096];
  while (f) {
    f.read(buf, sizeof(buf));
    std::streamsize n = f.gcount();
    if (n > 0)
      std::fwrite(buf, 1, static_cast<size_t>(n), stdout);
  }
  return 0;
}
