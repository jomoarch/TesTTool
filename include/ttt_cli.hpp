#ifndef TTT_CLI_HPP
#define TTT_CLI_HPP

#include "cli_config.hpp"

#include <filesystem>
#include <map>
#include <string>

// =============================================================================
// ttt-cli 各操作共享的类型与接口。
//
// 每个操作实现为一个 cmd_xxx.cpp，放在 src/ttt_cli/ 下；新增操作：
//   1. 在 include/ttt_cli.hpp 声明
//       int cmd_xxx(const CliArgs&, const CliConfig&);
//   2. 新建 src/ttt_cli/cmd_xxx.cpp 实现
//   3. 在 CMakeLists.txt 的 ttt-cli 目标加一个源文件
//   4. 在 src/ttt_cli/main.cpp 的命令表中加一行，并更新 print_usage
// =============================================================================

struct CliArgs {
  std::map<std::string, std::string> values;
  bool non_o2 = false;
};

struct ProblemConf {
  int time_limit_ms = 0;
  float memory_limit_mb = 0.0f;
};

// 操作入口：返回 0 成功 / 1 运行错误 / 2 用法错误
int cmd_add(const CliArgs &args, const CliConfig &cfg);
int cmd_submit(const CliArgs &args, const CliConfig &cfg);
int cmd_view(const CliArgs &args, const CliConfig &cfg);
int cmd_remove(const CliArgs &args, const CliConfig &cfg);
int cmd_help(const CliArgs &args, const CliConfig &cfg);  // 打印 docs/ttt_cli_help.txt

// ---- 通用辅助（src/ttt_cli/common.cpp）----

// 去掉首尾空白（空格/制表）
std::string trim(const std::string &s);
// 从 stdin 读一行（去掉 \r\n）。EOF 时返回 false
bool read_line_stdin(std::string &out);
// 打印提示并读一行。返回 false 表示 EOF（用户中断）
bool prompt_line(const std::string &hint, std::string &out);
// 文件名合法性：不允许路径分隔符与 "." ".."（防止逃出题库目录）
bool valid_name(const std::string &name);
// 文件选择器封装：want_dir=true 选文件夹，否则选文件。取消返回空路径
std::filesystem::path pick_with_picker(const std::filesystem::path &initial,
                                       bool want_dir);
// 询问题目名称；回车时用 file_picker 从题库选择（只保留末级文件夹名）
std::filesystem::path ask_problem(const CliConfig &cfg);
// 递归创建目录；失败打印错误并返回 false
bool ensure_dir(const std::filesystem::path &dir, const char *what);
// 生成唯一结果文件名：result_<时间戳>_<8位随机hex>.txt
std::filesystem::path
make_result_path(const std::filesystem::path &records_dir);
// 参数解析（--key value / --key=value；--non-o2 开关）
bool parse_args(int argc, char **argv, CliArgs &out);
// 输出用法（含全部操作）
void print_usage(const char *prog);

// 读取 <dir>/problem.conf；文件不存在返回 false（调用方回退全局默认）
bool load_problem_conf(const std::filesystem::path &dir, ProblemConf &out);
// 写入 <dir>/problem.conf
bool save_problem_conf(const std::filesystem::path &dir,
                       const ProblemConf &conf, std::string &error);

#endif // TTT_CLI_HPP
