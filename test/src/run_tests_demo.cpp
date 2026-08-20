// 测评演示：编译源文件 + 运行测试点 + 保存结果文件
// usage: run_tests_demo <source> <tests_dir> [time_limit_ms] [memory_limit_mb] [out_file]
#include "test_runner.hpp"
#include "test_packager.hpp"
#include "test_result_file.hpp"

#include <cstdio>
#include <cstdlib>

int main(int argc, char **argv) {
  if (argc < 3) {
    std::fprintf(stderr,
                 "usage: %s <source> <tests_dir> [time_ms] [mem_mb] [out_file]\n",
                 argv[0]);
    return 2;
  }
  std::filesystem::path source = argv[1];
  std::filesystem::path tests_dir = argv[2];
  int time_limit_ms = argc > 3 ? std::atoi(argv[3]) : 2000;
  float memory_limit_mb = argc > 4 ? std::atof(argv[4]) : 256.0f;
  std::filesystem::path out = argc > 5
                                  ? std::filesystem::path(argv[5])
                                  : std::filesystem::path("test_result.txt");

  TestPackage pkg = pack_tests("demo", tests_dir, time_limit_ms, memory_limit_mb);
  if (!pkg.success) {
    std::fprintf(stderr, "pack_tests failed: %s\n", pkg.message.c_str());
    return 1;
  }
  AssessmentResult r = run_tests(source, pkg);
  std::string err;
  if (!save_test_result(out, r, err)) {
    std::fprintf(stderr, "save_test_result failed: %s\n", err.c_str());
    return 1;
  }
  std::printf("compile_ok=%d compile_message=%s\n", r.compile_ok ? 1 : 0,
              r.compile_message.c_str());
  for (const auto &c : r.cases) {
    std::printf("  #%d %s %dms %.2fMB %s\n", c.id, verdict_name(c.verdict),
                c.time_ms, c.memory_mb, c.message.c_str());
  }
  std::printf("saved to %s\n", out.string().c_str());
  return 0;
}
