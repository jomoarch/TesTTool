#ifndef RUN_PROGRAM_HPP
#define RUN_PROGRAM_HPP

#include <string>
#include <filesystem>

enum RES_STATUS {
  _FAILED = -1,
  _SUCCESS = 0,

  _TLE = 100,
  _MLE = 101,

  _SIGSEGV = 11, // 段错误
  _SIGFPE = 8,   // 浮点异常
  _SIGABRT = 6,  // 主动中止
  _SIGBUS = 7,   // 总线错误
  _SIGILL = 4,   // 非法指令
  _SIGSYS = 31,  // 错误的系统调用
};

struct RunProgramResult {
  int time = 0;
  float memory = 0.0f;
  RES_STATUS status = _FAILED;
  std::string message = "";
};

std::string get_res_message(RES_STATUS status);

RunProgramResult run_program(const std::filesystem::path &exe_path,
                             const std::filesystem::path &input_path,
                             const std::filesystem::path &output_path,
                             int time_limit_ms, float memory_limit_mb);

#endif // RUN_PROGRAM_HPP