#include "test_runner.hpp"

#include "compiler.hpp"
#include "file_comparator.hpp"
#include "run_program.hpp"

#include <atomic>
#include <cstdio>
#include <stdexcept>
#include <system_error>

namespace {

std::atomic<unsigned long long> g_counter{0};

// 临时可执行文件路径（进程内自增序号保证唯一，避免并发冲突）
std::filesystem::path make_exe_path(const std::filesystem::path &source) {
  std::error_code ec;
  std::filesystem::path tmp = std::filesystem::temp_directory_path(ec);
  if (ec || tmp.empty()) tmp = source.parent_path();
  std::string name = "tettool_exe_" + source.stem().string() + "_" +
                     std::to_string(g_counter.fetch_add(1));
  return tmp / (name + ".exe");
}

// 临时运行输出文件
std::filesystem::path make_out_path() {
  std::error_code ec;
  std::filesystem::path tmp = std::filesystem::temp_directory_path(ec);
  if (ec || tmp.empty()) tmp = std::filesystem::current_path();
  return tmp / ("tettool_run_out_" + std::to_string(g_counter.load()) + ".txt");
}

} // namespace

const char *verdict_name(Verdict v) {
  switch (v) {
    case Verdict::AC: return "AC";
    case Verdict::WA: return "WA";
    case Verdict::TLE: return "TLE";
    case Verdict::MLE: return "MLE";
    case Verdict::RE: return "RE";
  }
  return "?";
}

AssessmentResult run_tests(const std::filesystem::path &source_path,
                           const TestPackage &pkg, bool enable_o2) {
  AssessmentResult result;
  std::filesystem::path exe = make_exe_path(source_path);
  std::filesystem::path out_tmp = make_out_path();
  try {
    CompileResult cr = compile(source_path, exe, enable_o2);
    result.compile_ok = cr.success;
    result.compile_message = cr.message;  // 成功时即 "Compilation succeeded"
    if (!cr.success) return result;       // 编译失败：无测试点

    for (const auto &tp : pkg.tests) {
      CaseResult cs;
      cs.id = tp.id;
      RunProgramResult rp = run_program(exe, tp.input_path, out_tmp,
                                        tp.time_limit_ms, tp.memory_limit_mb);
      cs.time_ms = rp.time;
      cs.memory_mb = rp.memory;
      switch (rp.status) {
        case _TLE:
          cs.verdict = Verdict::TLE;
          cs.message = rp.message;
          break;
        case _MLE:
          cs.verdict = Verdict::MLE;
          cs.message = rp.message;
          break;
        case _SUCCESS: {
          CompareResult cmp = compare_files(out_tmp, tp.expected_path);
          if (cmp.matched) {
            cs.verdict = Verdict::AC;  // AC 无提示信息
          } else {
            cs.verdict = Verdict::WA;
            cs.message = cmp.message;
          }
          break;
        }
        default:  // _FAILED 与各种信号 -> RE
          cs.verdict = Verdict::RE;
          cs.message = rp.message.empty() ? get_res_message(rp.status) : rp.message;
          break;
      }
      result.cases.push_back(std::move(cs));
    }
  } catch (const std::exception &e) {
    // 防御：任何异常都归为“测评未完成”，保留编译信息为空
    result.compile_ok = false;
    result.compile_message = std::string("exception: ") + e.what();
    result.cases.clear();
  }
  std::error_code ec;
  std::filesystem::remove(exe, ec);
  std::filesystem::remove(out_tmp, ec);
  return result;
}
