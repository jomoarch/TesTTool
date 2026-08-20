#ifndef TEST_RUNNER_HPP
#define TEST_RUNNER_HPP

#include "test_packager.hpp"

#include <filesystem>
#include <string>
#include <vector>

// 测试点判定状态
enum class Verdict {
  AC = 0,  // 答案正确（file_comparator matched）
  WA = 1,  // 答案错误（file_comparator 未匹配）
  TLE = 2, // 超时（run_program _TLE）
  MLE = 3, // 超内存（run_program _MLE）
  RE = 4,  // 运行错误（run_program 的信号状态或 _FAILED）
};

// 单个测试点的测评结果
struct CaseResult {
  int id = 0;
  Verdict verdict = Verdict::AC;
  int time_ms = 0;     // CPU 用时（ms），来自 run_program
  float memory_mb = 0; // 内存占用（MB），来自 run_program
  std::string message; // 提示信息；AC 时为空
};

// 测评信息包
struct AssessmentResult {
  bool compile_ok = false;
  std::string compile_message; // 编译成功时固定为 "Compilation succeeded"
  std::vector<CaseResult> cases;
};

// 返回状态短名："AC" / "WA" / "TLE" / "MLE" / "RE"
const char *verdict_name(Verdict v);

// 测评：先用 compiler 编译 source_path（enable_o2=false 时禁用 -O2 优化）
// 编译成功后再对 TestPackage 的每个测试点依次 run_program（应用时间/内存限制）
// 并用 file_comparator 比对输出。编译失败时 cases 为空，compile_message 为
// 编译器错误信息。可执行文件与运行输出使用临时文件，结束后清理
AssessmentResult run_tests(const std::filesystem::path &source_path,
                           const TestPackage &pkg, bool enable_o2 = true);

#endif // TEST_RUNNER_HPP
