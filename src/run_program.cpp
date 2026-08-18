#include "run_program.hpp"
#include <chrono>
#include <cstring>
#include <fstream>
#include <sstream>
#include <thread>
#include <vector>

#if defined(_WIN32) || defined(_WIN64)
#define PLATFORM_WINDOWS 1
#include <windows.h>
#include <psapi.h>
#ifdef _MSC_VER
#pragma comment(lib, "psapi.lib")
#endif
#elif defined(__linux__)
#define PLATFORM_LINUX 1
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#else
#error "Unsupported platform"
#endif

std::string get_res_message(RES_STATUS status) {
  switch (status) {
  case _SUCCESS:
    return "Success";
  case _FAILED:
    return "Failed";
  case _TLE:
    return "Time Limit Exceeded";
  case _MLE:
    return "Memory Limit Exceeded";
  case _SIGSEGV:
    return "Segmentation fault";
  case _SIGFPE:
    return "Floating point exception";
  case _SIGABRT:
    return "Aborted";
  case _SIGBUS:
    return "Bus error";
  case _SIGILL:
    return "Illegal instruction";
  case _SIGSYS:
    return "Bad system call";
  default:
    return "Unknown";
  }
}

#ifdef PLATFORM_WINDOWS

RunProgramResult run_program(const std::filesystem::path &exe_path,
                             const std::filesystem::path &input_path,
                             const std::filesystem::path &output_path,
                             int time_limit_ms, float memory_limit_mb) {
  RunProgramResult result;

  if (!std::filesystem::exists(exe_path)) {
    result.status = _FAILED;
    result.message = "Executable not found: " + exe_path.string();
    return result;
  }
  if (!std::filesystem::exists(input_path)) {
    result.status = _FAILED;
    result.message = "Input file not found: " + input_path.string();
    return result;
  }

  SECURITY_ATTRIBUTES sa{};
  sa.nLength = sizeof(sa);
  sa.bInheritHandle = TRUE;

  HANDLE hIn =
      CreateFileW(input_path.wstring().c_str(), GENERIC_READ, FILE_SHARE_READ,
                  &sa, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (hIn == INVALID_HANDLE_VALUE) {
    result.status = _FAILED;
    result.message = "Failed to open input file";
    return result;
  }

  HANDLE hOut =
      CreateFileW(output_path.wstring().c_str(), GENERIC_WRITE, FILE_SHARE_READ,
                  &sa, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (hOut == INVALID_HANDLE_VALUE) {
    CloseHandle(hIn);
    result.status = _FAILED;
    result.message = "Failed to open output file";
    return result;
  }

  STARTUPINFOW si{};
  si.cb = sizeof(si);
  si.dwFlags = STARTF_USESTDHANDLES;
  si.hStdInput = hIn;
  si.hStdOutput = hOut;
  si.hStdError = hOut;

  PROCESS_INFORMATION pi{};

  std::wstring cmd = L"\"" + exe_path.wstring() + L"\"";
  std::vector<wchar_t> cmdBuf(cmd.begin(), cmd.end());
  cmdBuf.push_back(L'\0');

  if (!CreateProcessW(nullptr, cmdBuf.data(), nullptr, nullptr, TRUE, 0,
                      nullptr, nullptr, &si, &pi)) {
    CloseHandle(hIn);
    CloseHandle(hOut);
    result.status = _FAILED;
    result.message = "CreateProcess failed";
    return result;
  }

  CloseHandle(hIn);
  CloseHandle(hOut);

  if (time_limit_ms > 0 || memory_limit_mb > 0) {
    HANDLE hJob = CreateJobObjectW(nullptr, nullptr);
    if (hJob) {
      JOBOBJECT_EXTENDED_LIMIT_INFORMATION jeli{};

      if (time_limit_ms > 0) {
        jeli.BasicLimitInformation.LimitFlags |= JOB_OBJECT_LIMIT_PROCESS_TIME;
        jeli.BasicLimitInformation.PerProcessUserTimeLimit.QuadPart =
            static_cast<LONGLONG>(time_limit_ms) * 10000;
      }
      if (memory_limit_mb > 0) {
        jeli.BasicLimitInformation.LimitFlags |=
            JOB_OBJECT_LIMIT_PROCESS_MEMORY;
        jeli.ProcessMemoryLimit =
            static_cast<SIZE_T>(memory_limit_mb * 1024.0f * 1024.0f);
      }

      if (SetInformationJobObject(hJob, JobObjectExtendedLimitInformation,
                                  &jeli, sizeof(jeli))) {
        AssignProcessToJobObject(hJob, pi.hProcess);
      }
      CloseHandle(hJob);
    }
  }

  bool time_out = false;
  DWORD waitResult = WaitForSingleObject(
      pi.hProcess,
      time_limit_ms > 0 ? static_cast<DWORD>(time_limit_ms * 2) : INFINITE);

  if (waitResult == WAIT_TIMEOUT) {
    time_out = true;
    TerminateProcess(pi.hProcess, 1);
    WaitForSingleObject(pi.hProcess, INFINITE);
  }

  DWORD exit_code = 0;
  GetExitCodeProcess(pi.hProcess, &exit_code);

  FILETIME creationTime, exitTime, kernelTime, userTime;
  ULONGLONG cpu100ns = 0;
  if (GetProcessTimes(pi.hProcess, &creationTime, &exitTime, &kernelTime,
                      &userTime)) {
    ULARGE_INTEGER k{}, u{};
    k.LowPart = kernelTime.dwLowDateTime;
    k.HighPart = kernelTime.dwHighDateTime;
    u.LowPart = userTime.dwLowDateTime;
    u.HighPart = userTime.dwHighDateTime;
    cpu100ns = k.QuadPart + u.QuadPart;
  }
  result.time = static_cast<int>(cpu100ns / 10000);

  PROCESS_MEMORY_COUNTERS pmc{};
  if (GetProcessMemoryInfo(pi.hProcess, &pmc, sizeof(pmc))) {
    result.memory = pmc.PeakWorkingSetSize / 1024.0f / 1024.0f;
  }

  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);

  if (time_out) {
    result.status = _FAILED;
    result.message = "Time Limit Exceeded";
    return result;
  }

  if (exit_code == 0) {
    result.status = _SUCCESS;
    result.message = get_res_message(_SUCCESS);
  } else {
    switch (exit_code) {
    case EXCEPTION_ACCESS_VIOLATION:
      result.status = _SIGSEGV;
      break;
    case EXCEPTION_FLT_DIVIDE_BY_ZERO:
    case EXCEPTION_INT_DIVIDE_BY_ZERO:
    case EXCEPTION_INT_OVERFLOW:
      result.status = _SIGFPE;
      break;
    case EXCEPTION_ILLEGAL_INSTRUCTION:
      result.status = _SIGILL;
      break;
    case EXCEPTION_STACK_OVERFLOW:
      result.status = _SIGSEGV;
      break;
    default:
      result.status = _FAILED;
      result.message = "Exit code: " + std::to_string(exit_code);
      break;
    }

    if (result.status != _FAILED) {
      result.message = get_res_message(result.status);
    }
  }

  return result;
}

#elif defined(PLATFORM_LINUX)

namespace {

const long kClkTck = sysconf(_SC_CLK_TCK);
const unsigned long long kMemSlackBytes = 32ULL * 1024 * 1024;

long get_status_field_kb(pid_t pid, const char *prefix) {
  std::ifstream file("/proc/" + std::to_string(pid) + "/status");
  if (!file.is_open())
    return -1;
  std::string line;
  while (std::getline(file, line)) {
    if (line.rfind(prefix, 0) == 0) {
      std::istringstream iss(line);
      std::string label, value, unit;
      iss >> label >> value >> unit;
      try {
        return std::stol(value);
      } catch (...) {
        return -1;
      }
    }
  }
  return -1;
}

long get_process_rss_kb(pid_t pid) {
  return get_status_field_kb(pid, "VmRSS:");
}

long get_process_vsz_kb(pid_t pid) {
  return get_status_field_kb(pid, "VmSize:");
}

long get_process_cpu_ms(pid_t pid) {
  std::ifstream file("/proc/" + std::to_string(pid) + "/stat");
  if (!file.is_open())
    return -1;
  std::string line;
  if (!std::getline(file, line))
    return -1;
  size_t close_paren = line.rfind(')');
  if (close_paren == std::string::npos)
    return -1;
  std::istringstream iss(line.substr(close_paren + 1));
  std::string state;
  if (!(iss >> state))
    return -1;
  long long skip;
  for (int i = 0; i < 10; ++i) {
    if (!(iss >> skip))
      return -1;
  }
  long long utime = 0, stime = 0;
  if (!(iss >> utime >> stime))
    return -1;
  return static_cast<long>((utime + stime) * 1000LL / kClkTck);
}

} // namespace

RunProgramResult run_program(const std::filesystem::path &exe_path,
                             const std::filesystem::path &input_path,
                             const std::filesystem::path &output_path,
                             int time_limit_ms, float memory_limit_mb) {
  RunProgramResult result;

  if (!std::filesystem::exists(exe_path)) {
    result.status = _FAILED;
    result.message = "Executable not found: " + exe_path.string();
    return result;
  }
  if (!std::filesystem::exists(input_path)) {
    result.status = _FAILED;
    result.message = "Input file not found: " + input_path.string();
    return result;
  }

  pid_t pid = fork();
  if (pid < 0) {
    result.status = _FAILED;
    result.message = "fork failed";
    return result;
  }

  if (pid == 0) {
    int in_fd = open(input_path.c_str(), O_RDONLY);
    if (in_fd < 0)
      _exit(127);
    if (dup2(in_fd, STDIN_FILENO) < 0)
      _exit(127);
    close(in_fd);

    int out_fd = open(output_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (out_fd < 0)
      _exit(127);
    if (dup2(out_fd, STDOUT_FILENO) < 0)
      _exit(127);
    close(out_fd);

    int err_fd = open("/dev/null", O_WRONLY);
    if (err_fd >= 0) {
      dup2(err_fd, STDERR_FILENO);
      close(err_fd);
    }

    if (memory_limit_mb > 0) {
      struct rlimit rl;
      rl.rlim_cur = rl.rlim_max =
          static_cast<rlim_t>(memory_limit_mb * 1024.0f * 1024.0f) +
          kMemSlackBytes;
      setrlimit(RLIMIT_AS, &rl);
    }

    const char *cpath = exe_path.c_str();
    char *argv[] = {const_cast<char *>(cpath), nullptr};
    execvp(cpath, argv);
    _exit(127);
  }

  auto start = std::chrono::steady_clock::now();
  int status = 0;
  struct rusage usage{};
  bool timed_out = false;
  bool memory_out = false;
  const bool has_limits = time_limit_ms > 0 || memory_limit_mb > 0;
  const long long wall_watchdog_ms =
      time_limit_ms > 0 ? 2LL * time_limit_ms : 0;
  const float vsz_slack_mb =
      static_cast<float>(kMemSlackBytes) / (1024.0f * 1024.0f);

  while (true) {
    pid_t w = wait4(pid, &status, WNOHANG, &usage);
    if (w == pid)
      break;
    if (w < 0) {
      if (errno == EINTR)
        continue;
      result.status = _FAILED;
      result.message = "wait4 failed: " + std::string(strerror(errno));
      return result;
    }

    if (time_limit_ms > 0) {
      long cpu_ms = get_process_cpu_ms(pid);
      if (cpu_ms > time_limit_ms) {
        timed_out = true;
        kill(pid, SIGKILL);
        while (wait4(pid, &status, 0, &usage) < 0 && errno == EINTR) {
        }
        break;
      }
      auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now() - start)
                            .count();
      if (elapsed_ms > wall_watchdog_ms) {
        timed_out = true;
        kill(pid, SIGKILL);
        while (wait4(pid, &status, 0, &usage) < 0 && errno == EINTR) {
        }
        break;
      }
    }

    if (memory_limit_mb > 0) {
      long rss_kb = get_process_rss_kb(pid);
      if (rss_kb > 0 && rss_kb / 1024.0f > memory_limit_mb) {
        memory_out = true;
        kill(pid, SIGKILL);
        while (wait4(pid, &status, 0, &usage) < 0 && errno == EINTR) {
        }
        break;
      }

      long vsz_kb = get_process_vsz_kb(pid);
      if (vsz_kb > 0 && vsz_kb / 1024.0f > memory_limit_mb + vsz_slack_mb) {
        memory_out = true;
        kill(pid, SIGKILL);
        while (wait4(pid, &status, 0, &usage) < 0 && errno == EINTR) {
        }
        break;
      }
    }

    std::this_thread::sleep_for(
        std::chrono::milliseconds(has_limits ? 10 : 50));
  }

  double cpu_sec =
      usage.ru_utime.tv_sec + usage.ru_stime.tv_sec +
      (usage.ru_utime.tv_usec + usage.ru_stime.tv_usec) / 1000000.0;
  result.time = static_cast<int>(cpu_sec * 1000.0);
  result.memory = usage.ru_maxrss / 1024.0f;

  if (memory_limit_mb > 0 && result.memory > memory_limit_mb) {
    memory_out = true;
  }

  if (timed_out) {
    result.status = _TLE;
    result.message = get_res_message(_TLE);
    return result;
  }
  if (memory_out) {
    result.status = _MLE;
    result.message = get_res_message(_MLE);
    return result;
  }

  if (WIFEXITED(status)) {
    int code = WEXITSTATUS(status);
    if (code == 0) {
      result.status = _SUCCESS;
      result.message = get_res_message(_SUCCESS);
    } else {
      result.status = _FAILED;
      result.message = "Exit code: " + std::to_string(code);
    }
  } else if (WIFSIGNALED(status)) {
    int sig = WTERMSIG(status);
    switch (sig) {
    case SIGSEGV:
      result.status = _SIGSEGV;
      break;
    case SIGFPE:
      result.status = _SIGFPE;
      break;
    case SIGABRT:
      result.status = _SIGABRT;
      break;
    case SIGBUS:
      result.status = _SIGBUS;
      break;
    case SIGILL:
      result.status = _SIGILL;
      break;
    case SIGSYS:
      result.status = _SIGSYS;
      break;
    default:
      result.status = _FAILED;
      result.message = "Killed by signal " + std::to_string(sig);
      break;
    }
    if (result.status != _FAILED) {
      result.message = get_res_message(result.status);
    }
  } else {
    result.status = _FAILED;
    result.message = "Unknown process status";
  }
  return result;
}

#endif