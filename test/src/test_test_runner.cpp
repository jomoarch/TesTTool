// test_runner / test_result_file 单元测试（非交互）。
// 覆盖：结果文件保存/加载往返（含 CE 与全部五种状态、中文与多行消息）、
// 非法文件判定（魔数/CE 标记/数量/编号/状态/数字/base64/多余内容）、
// 以及 run_tests 端到端（真实编译 + 运行：AC/WA/TLE/MLE/RE、编译失败）。
#include "test_runner.hpp"
#include "test_packager.hpp"
#include "test_result_file.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

static int g_failures = 0;
#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      ++g_failures;                                                          \
      std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);            \
    }                                                                        \
  } while (0)

static fs::path make_dir(const std::string &name) {
  fs::path base = fs::temp_directory_path() / name;
  fs::remove_all(base);
  fs::create_directories(base);
  return base;
}

static void write_file(const fs::path &p, const std::string &content) {
  std::ofstream f(p, std::ios::binary);
  f << content;
}

static bool same_case(const CaseResult &a, const CaseResult &b) {
  return a.id == b.id && a.verdict == b.verdict && a.time_ms == b.time_ms &&
         a.memory_mb == b.memory_mb && a.message == b.message;
}

// ---- 正常结果文件往返 ----
static void test_roundtrip_normal() {
  fs::path base = make_dir("fp_rt_normal");
  fs::path file = base / "result.txt";
  AssessmentResult r;
  r.compile_ok = true;
  r.compile_message = "Compilation succeeded";
  r.cases = {
      {1, Verdict::AC, 12, 2.5f, ""},
      {2, Verdict::WA, 15, 2.75f, "line1\n中文行2\nmismatch at line 3"},
      {3, Verdict::TLE, 200, 3.0f, "Time Limit Exceeded"},
      {4, Verdict::MLE, 300, 100.5f, "Memory Limit Exceeded"},
      {5, Verdict::RE, 10, 1.25f, "Segmentation fault"},
  };
  std::string err;
  CHECK(save_test_result(file, r, err));
  AssessmentResult l;
  CHECK(load_test_result(file, l, err));
  CHECK(l.compile_ok == r.compile_ok);
  CHECK(l.compile_message == r.compile_message);
  CHECK(l.cases.size() == r.cases.size());
  for (size_t i = 0; i < r.cases.size(); ++i) CHECK(same_case(r.cases[i], l.cases[i]));
  fs::remove_all(base);
}

// ---- CE 文件往返（编译信息逐字节无损，含中文与 \r\n）----
static void test_roundtrip_ce() {
  fs::path base = make_dir("fp_rt_ce");
  fs::path file = base / "result.txt";
  AssessmentResult r;
  r.compile_ok = false;
  r.compile_message =
      "error: 'x' was not declared in this scope\n"
      "  5 |   x = 1;\n"
      "    |   ^\n"
      "中文错误信息行\r\n"
      "more compiler output";
  std::string err;
  CHECK(save_test_result(file, r, err));
  AssessmentResult l;
  CHECK(load_test_result(file, l, err));
  CHECK(!l.compile_ok);
  CHECK(l.compile_message == r.compile_message);  // 逐字节相等
  CHECK(l.cases.empty());
  fs::remove_all(base);
}

// ---- 空测试点（CE 0 且 0 个测试点）应能正常加载 ----
static void test_empty_cases() {
  fs::path base = make_dir("fp_rt_empty");
  fs::path file = base / "result.txt";
  AssessmentResult r;
  r.compile_ok = true;
  std::string err;
  CHECK(save_test_result(file, r, err));
  AssessmentResult l;
  CHECK(load_test_result(file, l, err));
  CHECK(l.compile_ok);
  CHECK(l.cases.empty());
  fs::remove_all(base);
}

// ---- 非法 / 损坏文件判定 ----
static void test_invalid_files() {
  fs::path base = make_dir("fp_invalid");
  std::string err;
  AssessmentResult r;
  auto bad = [&](const std::string &content) {
    fs::path f = base / "bad.txt";
    write_file(f, content);
    return !load_test_result(f, r, err) && !err.empty();
  };
  CHECK(bad(""));                                      // 空文件
  CHECK(bad("garbage\nstuff\n"));                      // 垃圾内容
  CHECK(bad("TESTOOL_RESULT v1\n"));                   // 缺 CE 标记
  CHECK(bad("WRONG v1\nCE 0\n0\n"));                   // 魔数错误
  CHECK(bad("TESTOOL_RESULT v1\nCE 2\n"));             // CE 标记非法
  CHECK(bad("TESTOOL_RESULT v1\nCE 1\n"));             // CE 缺编译信息
  CHECK(bad("TESTOOL_RESULT v1\nCE 1\n!!notb64!!\n")); // base64 非法
  CHECK(bad("TESTOOL_RESULT v1\nCE 1\nYQ==\nextra\n")); // CE 后有多余内容
  CHECK(bad("TESTOOL_RESULT v1\nCE 0\nabc\n"));        // 数量非数字
  CHECK(bad("TESTOOL_RESULT v1\nCE 0\n1\nCASE 1 XX 1 2.0\nYQ==\n")); // 状态非法
  CHECK(bad("TESTOOL_RESULT v1\nCE 0\n1\nCASE 2 AC 1 2.0\nYQ==\n")); // 编号顺序错
  CHECK(bad("TESTOOL_RESULT v1\nCE 0\n1\nCASE 1 AC 1 -2.0\nYQ==\n")); // 内存为负
  CHECK(bad("TESTOOL_RESULT v1\nCE 0\n1\nCASE 1 AC x 2.0\nYQ==\n"));  // 时间为非数字
  CHECK(bad("TESTOOL_RESULT v1\nCE 0\n1\nCASE 1 AC 1 2.0\nYQ==\n"
            "CASE 2 WA 1 2.0\nYg==\n"));                // 数量不符（多出 CASE）
  CHECK(bad("TESTOOL_RESULT v1\nCE 0\n0\ntrailing\n")); // 结尾多余内容
  // 不存在的文件
  CHECK(!load_test_result(base / "nope.txt", r, err));
  fs::remove_all(base);
}

// ---- run_tests 端到端：AC + WA + 编译失败 ----
static void test_run_tests_ac_wa() {
  fs::path base = make_dir("fp_rt_acwa");
  fs::path src = base / "main.cpp";
  write_file(src, "#include <cstdio>\n"
                  "int main() {\n"
                  "  int n;\n"
                  "  if (std::scanf(\"%d\", &n) != 1) return 1;\n"
                  "  std::printf(\"%d\\n\", n * 2);\n"
                  "  return 0;\n"
                  "}\n");
  fs::create_directories(base / "tests");
  write_file(base / "tests/1.in", "21\n");
  write_file(base / "tests/1.out", "42\n");
  write_file(base / "tests/2.in", "10\n");
  write_file(base / "tests/2.out", "999\n");  // 期望错误 -> WA
  TestPackage pkg = pack_tests("t", base / "tests", 1000, 256.0f);
  CHECK(pkg.success);
  AssessmentResult r = run_tests(src, pkg);
  CHECK(r.compile_ok);
  CHECK(r.compile_message == "Compilation succeeded");
  CHECK(r.cases.size() == 2);
  CHECK(r.cases[0].id == 1);
  CHECK(r.cases[0].verdict == Verdict::AC);
  CHECK(r.cases[0].message.empty());
  CHECK(r.cases[1].id == 2);
  CHECK(r.cases[1].verdict == Verdict::WA);
  CHECK(!r.cases[1].message.empty());

  // 编译失败：cases 为空，compile_message 非空
  write_file(base / "bad.cpp", "int main() { this is not valid c++ }\n");
  AssessmentResult rb = run_tests(base / "bad.cpp", pkg);
  CHECK(!rb.compile_ok);
  CHECK(!rb.compile_message.empty());
  CHECK(rb.cases.empty());
  fs::remove_all(base);
}

// ---- run_tests 端到端：TLE / MLE / RE ----
static void test_run_tests_limits() {
  fs::path base = make_dir("fp_rt_limits");
  fs::create_directories(base / "tests");
  write_file(base / "tests/1.in", "1\n");
  write_file(base / "tests/1.out", "1\n");

  // TLE：死循环，时间限制 200ms
  fs::path tle_src = base / "tle.cpp";
  write_file(tle_src, "int main() { for (;;) {} return 0; }\n");
  TestPackage pkg1 = pack_tests("t", base / "tests", 200, 256.0f);
  CHECK(pkg1.success);
  AssessmentResult rt = run_tests(tle_src, pkg1);
  CHECK(rt.compile_ok);
  CHECK(rt.cases.size() == 1);
  CHECK(rt.cases[0].verdict == Verdict::TLE);
  CHECK(!rt.cases[0].message.empty());

  // MLE：分配并逐页触碰 80MB，内存限制 64MB（RLIMIT_AS = 64+32MB，RSS 超限被杀）
  fs::path mle_src = base / "mle.cpp";
  write_file(mle_src,
             "#include <cstdlib>\n"
             "#include <unistd.h>\n"
             "int main() {\n"
             "  const size_t need = 80u << 20;\n"
             "  char *p = (char *)std::malloc(need);\n"
             "  if (!p) return 1;\n"
             "  volatile char *vp = p;\n"
             "  for (size_t i = 0; i < need; i += 4096) vp[i] = 1;\n"  // 逐页触碰，防优化
             "  for (;;) sleep(1);\n"
             "  return 0;\n"
             "}\n");
  TestPackage pkg2 = pack_tests("m", base / "tests", 3000, 64.0f);
  CHECK(pkg2.success);
  AssessmentResult rm = run_tests(mle_src, pkg2);
  CHECK(rm.compile_ok);
  CHECK(rm.cases.size() == 1);
  CHECK(rm.cases[0].verdict == Verdict::MLE);

  // RE：主动触发段错误
  fs::path re_src = base / "re.cpp";
  write_file(re_src,
             "#include <csignal>\n"
             "int main() { std::raise(SIGSEGV); return 0; }\n");
  TestPackage pkg3 = pack_tests("r", base / "tests", 3000, 256.0f);
  CHECK(pkg3.success);
  AssessmentResult rr = run_tests(re_src, pkg3);
  CHECK(rr.compile_ok);
  CHECK(rr.cases.size() == 1);
  CHECK(rr.cases[0].verdict == Verdict::RE);
  CHECK(!rr.cases[0].message.empty());
  fs::remove_all(base);
}

int main() {
  test_roundtrip_normal();
  test_roundtrip_ce();
  test_empty_cases();
  test_invalid_files();
  test_run_tests_ac_wa();
  test_run_tests_limits();
  if (g_failures == 0) {
    std::printf("ALL TESTS PASSED\n");
    return 0;
  }
  std::printf("%d FAILURE(S)\n", g_failures);
  return 1;
}
