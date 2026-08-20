// ttt-cli 配置模块单元测试（非交互）。
// 覆盖：kv 文件读写、全局配置默认路径与 TTT_CLI_CONFIG 覆盖、配置保存/加载往返、
// 缺省配置创建。
#include "cli_config.hpp"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>

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

static void test_kv_roundtrip() {
  fs::path base = make_dir("ttt_cli_kv");
  fs::path file = base / "kv.txt";
  std::map<std::string, std::string> kv = {
      {"bank_dir", "/home/user/my bank"},  // 值含空格
      {"default_time_limit_ms", "1000"},
      {"default_memory_limit_mb", "256.50"},
      {"", "ignored"},  // 空 key 不写入
  };
  std::string err;
  CHECK(write_kv_file(file, kv, err));
  std::map<std::string, std::string> out;
  CHECK(read_kv_file(file, out, err));
  CHECK(out.size() == 3);
  CHECK(out["bank_dir"] == "/home/user/my bank");
  CHECK(out["default_time_limit_ms"] == "1000");
  CHECK(out["default_memory_limit_mb"] == "256.50");
  // 不存在的文件
  CHECK(!read_kv_file(base / "nope.txt", out, err));
  fs::remove_all(base);
}

static void test_config_roundtrip() {
  fs::path base = make_dir("ttt_cli_cfg");
  fs::path file = base / "cfg.txt";
  CliConfig cfg;
  cfg.bank_dir = "/tmp/题库 with space";
  cfg.default_time_limit_ms = 500;
  cfg.default_memory_limit_mb = 128.25f;
  std::string err;
  CHECK(save_config(file, cfg, err));
  CliConfig out;
  CHECK(load_config(file, out, err));
  CHECK(out.bank_dir == cfg.bank_dir);
  CHECK(out.default_time_limit_ms == cfg.default_time_limit_ms);
  CHECK(out.default_memory_limit_mb == cfg.default_memory_limit_mb);
  fs::remove_all(base);
}

static void set_env(const char *key, const char *value) {
#if defined(_WIN32)
  _putenv_s(key, value ? value : "");
#else
  if (value)
    setenv(key, value, 1);
  else
    unsetenv(key);
#endif
}

static void test_default_path() {
  // 先清除环境变量，确保测的是默认路径
  std::string old = std::getenv("TTT_CLI_CONFIG") ? std::getenv("TTT_CLI_CONFIG") : "";
  set_env("TTT_CLI_CONFIG", nullptr);
  fs::path p1 = default_config_path();
  CHECK(p1.filename() == "config.txt");
  CHECK(p1.parent_path().filename() == "ttt-cli");
  // 设置环境变量后应覆盖
  fs::path tmp = fs::temp_directory_path() / "ttt_cli_env_cfg.txt";
  set_env("TTT_CLI_CONFIG", tmp.string().c_str());
  CHECK(default_config_path() == tmp);
  set_env("TTT_CLI_CONFIG", old.empty() ? nullptr : old.c_str());
}

static void test_ensure_config() {
  fs::path base = make_dir("ttt_cli_ensure");
  fs::path file = base / "cfg.txt";
  CliConfig cfg;
  std::string err;
  CHECK(ensure_config(file, cfg, err));  // 自动创建
  CHECK(fs::exists(file));
  CHECK(!cfg.bank_dir.empty());
  CHECK(cfg.default_time_limit_ms == 1000);
  CHECK(cfg.default_memory_limit_mb == 256.0f);
  // 再次读取保持一致
  CliConfig out;
  CHECK(load_config(file, out, err));
  CHECK(out.bank_dir == cfg.bank_dir);
  fs::remove_all(base);
}

int main() {
  test_kv_roundtrip();
  test_config_roundtrip();
  test_default_path();
  test_ensure_config();
  if (g_failures == 0) {
    std::printf("ALL TESTS PASSED\n");
    return 0;
  }
  std::printf("%d FAILURE(S)\n", g_failures);
  return 1;
}
