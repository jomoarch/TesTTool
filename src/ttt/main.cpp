// ttt：交互式测评机外壳。
//
// 运行后逐行读取命令，bash 风格提示符：<绝对路径>$ 。
// 内置命令：cd / ls / goto / quit（exit 同义）；其余命令（add/submit/view/
// remove/help）原样转交给 ttt-cli 子进程执行。
//
// 位置是虚拟的：cd/ls/goto 只改变 ttt 内部的虚拟当前目录，ttt 进程自身的
// 真实工作目录不变；转交 ttt-cli 时由子进程 chdir 到虚拟位置，因此命令中的
// 相对路径都相对虚拟位置解析。ttt 本身不提供直接修改文件系统的操作。
#include "file_picker.hpp"
#include "helpfile.hpp" // fp::exe_directory

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <io.h>
#include <windows.h>
#define READ_FD _read
#define PATH_SEP ';'
#else
#include <errno.h>
#include <sys/wait.h>
#include <unistd.h>
#define READ_FD ::read
#define PATH_SEP ':'
#endif

namespace fs = std::filesystem;

namespace {

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

std::vector<std::string> tokenize(const std::string &line) {
  std::vector<std::string> out;
  std::string cur;
  bool in_single = false, in_double = false, esc = false;
  for (char c : line) {
    if (esc) {
      cur.push_back(c);
      esc = false;
      continue;
    }
    if (c == '\\' && !in_single) {
      esc = true;
      continue;
    }
    if (in_single) {
      if (c == '\'')
        in_single = false;
      else
        cur.push_back(c);
      continue;
    }
    if (in_double) {
      if (c == '"')
        in_double = false;
      else
        cur.push_back(c);
      continue;
    }
    if (c == '\'') {
      in_single = true;
      continue;
    }
    if (c == '"') {
      in_double = true;
      continue;
    }
    if (c == ' ' || c == '\t') {
      if (!cur.empty()) {
        out.push_back(cur);
        cur.clear();
      }
      continue;
    }
    cur.push_back(c);
  }
  if (esc)
    cur.push_back('\\');
  if (!cur.empty())
    out.push_back(cur);
  return out;
}

fs::path resolve(const fs::path &base, const std::string &p) {
  return fs::absolute(base / p).lexically_normal();
}

int builtin_cd(fs::path &vcwd, const std::vector<std::string> &args) {
  if (args.size() > 1) {
    std::fprintf(stderr, "cd: too many arguments\n");
    return 1;
  }
  fs::path target;
  if (args.empty()) {
    const char *h = std::getenv("HOME");
    if (!h || !*h) {
      std::fprintf(stderr, "cd: HOME is not set\n");
      return 1;
    }
    target = fs::path(h);
  } else {
    target = resolve(vcwd, args[0]);
  }
  std::error_code ec;
  if (!fs::is_directory(target, ec)) {
    std::fprintf(stderr, "cd: no such directory: %s\n",
                 args.empty() ? "HOME" : args[0].c_str());
    return 1;
  }
  vcwd = target;
  return 0;
}

int builtin_ls(const fs::path &vcwd, const std::vector<std::string> &args) {
  if (args.size() > 1) {
    std::fprintf(stderr, "ls: too many arguments\n");
    return 1;
  }
  fs::path dir = args.empty() ? vcwd : resolve(vcwd, args[0]);
  std::error_code ec;
  if (!fs::is_directory(dir, ec)) {
    std::fprintf(stderr, "ls: no such directory: %s\n",
                 args.empty() ? vcwd.string().c_str() : args[0].c_str());
    return 1;
  }
  std::vector<std::string> names;
  for (fs::directory_iterator it(dir, ec), end; it != end; it.increment(ec)) {
    if (ec) {
      std::fprintf(stderr, "ls: read error: %s\n", ec.message().c_str());
      ec.clear();
      continue;
    }
    std::string name = it->path().filename().string();
    if (!name.empty() && name[0] == '.')
      continue;
    if (it->is_directory())
      name += "/";
    names.push_back(std::move(name));
  }
  std::sort(names.begin(), names.end());
  for (const auto &n : names)
    std::printf("%s\n", n.c_str());
  return 0;
}

int builtin_goto(fs::path &vcwd) {
  SelectionOptions opts;
  opts.initial_path = vcwd;
  opts.mode = SelectionMode::SINGLE_DIRECTORY;
  opts.allow_files = false;
  opts.allow_directories = true;
  SelectionResult r = pick_files(opts);
  if (!r.ok || r.paths.empty()) {
    std::printf("cancelled.\n");
    return 1;
  }
  vcwd = r.paths[0];
  return 0;
}

fs::path find_cli() {
  fs::path exe = fp::exe_directory();
  if (!exe.empty()) {
    fs::path cand = exe / "ttt-cli";
#if defined(_WIN32)
    cand += ".exe";
#endif
    if (fs::exists(cand))
      return cand;
  }
  const char *path_env = std::getenv("PATH");
  if (path_env) {
    std::string path(path_env);
    size_t pos = 0;
    while (pos <= path.size()) {
      size_t sep = path.find(PATH_SEP, pos);
      std::string dir = path.substr(
          pos, sep == std::string::npos ? std::string::npos : sep - pos);
      if (!dir.empty()) {
        fs::path cand = fs::path(dir) / "ttt-cli";
#if defined(_WIN32)
        cand += ".exe";
#endif
        if (fs::exists(cand))
          return cand;
      }
      if (sep == std::string::npos)
        break;
      pos = sep + 1;
    }
  }
  return {};
}

#if defined(_WIN32)
std::wstring utf8_to_wide(const std::string &s) {
  if (s.empty())
    return {};
  int n = ::MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
  std::wstring w(n > 0 ? n - 1 : 0, L'\0');
  if (n > 0)
    ::MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], n);
  return w;
}

std::string win_quote(const std::string &s) {
  if (s.find_first_of(" \t\"") == std::string::npos)
    return s;
  std::string out = "\"";
  for (char c : s) {
    if (c == '"')
      out += "\\\"";
    else
      out.push_back(c);
  }
  out += "\"";
  return out;
}
#endif

int run_cli_command(const fs::path &exe, const std::vector<std::string> &args,
                    const fs::path &cwd) {
#if defined(_WIN32)
  std::string cmdline = win_quote(exe.string());
  for (const auto &a : args)
    cmdline += " " + win_quote(a);
  STARTUPINFOW si{};
  si.cb = sizeof(si);
  PROCESS_INFORMATION pi{};
  std::wstring wcmd = utf8_to_wide(cmdline);
  std::wstring wcwd = utf8_to_wide(cwd.string());
  std::wstring wexe = utf8_to_wide(exe.string());
  if (!::CreateProcessW(wexe.c_str(), &wcmd[0], nullptr, nullptr, TRUE, 0,
                        nullptr, wcwd.c_str(), &si, &pi)) {
    std::fprintf(stderr, "error: cannot start ttt-cli (error %lu)\n",
                 static_cast<unsigned long>(::GetLastError()));
    return -1;
  }
  ::WaitForSingleObject(pi.hProcess, INFINITE);
  DWORD code = 1;
  ::GetExitCodeProcess(pi.hProcess, &code);
  ::CloseHandle(pi.hProcess);
  ::CloseHandle(pi.hThread);
  return static_cast<int>(code);
#else
  std::vector<char *> argv;
  std::string exe_str = exe.string();
  argv.push_back(const_cast<char *>(exe_str.c_str()));
  for (const auto &a : args)
    argv.push_back(const_cast<char *>(a.c_str()));
  argv.push_back(nullptr);

  pid_t pid = ::fork();
  if (pid < 0) {
    std::fprintf(stderr, "error: fork failed\n");
    return -1;
  }
  if (pid == 0) {
    if (::chdir(cwd.string().c_str()) != 0)
      _exit(126);
    ::execv(argv[0], argv.data());
    _exit(127);
  }
  int status = 0;
  while (::waitpid(pid, &status, 0) < 0 && errno == EINTR) {
  }
  return status;
#endif
}

} // namespace

int main() {
  fs::path vcwd = fs::absolute(fs::current_path()).lexically_normal();
  std::printf("ttt: interactive shell. Built-in: cd, ls, goto, quit (exit).\n"
              "Other commands (add/submit/view/remove/help) run as ttt-cli.\n");

  for (;;) {
    std::printf("[%s]$ ", vcwd.string().c_str());
    std::fflush(stdout);

    std::string line;
    if (!read_line_stdin(line)) {
      std::printf("\n");
      return 0;
    }
    std::vector<std::string> words = tokenize(line);
    if (words.empty())
      continue;

    const std::string &cmd = words[0];
    std::vector<std::string> rest(words.begin() + 1, words.end());

    if (cmd == "quit" || cmd == "exit")
      return 0;
    if (cmd == "cd") {
      builtin_cd(vcwd, rest);
      continue;
    }
    if (cmd == "ls") {
      builtin_ls(vcwd, rest);
      continue;
    }
    if (cmd == "goto") {
      builtin_goto(vcwd);
      continue;
    }

    fs::path cli = find_cli();
    if (cli.empty()) {
      std::fprintf(
          stderr,
          "error: ttt-cli not found (expected next to ttt or in PATH)\n");
      continue;
    }
    run_cli_command(cli, words, vcwd);
  }
}
