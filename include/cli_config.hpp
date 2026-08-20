#ifndef CLI_CONFIG_HPP
#define CLI_CONFIG_HPP

#include <filesystem>
#include <map>
#include <string>

// ttt-cli 全局配置：题库路径 + 默认时间/内存限制
struct CliConfig {
  std::filesystem::path bank_dir;
  int default_time_limit_ms = 1000;
  float default_memory_limit_mb = 256.0f;
};

// ---- 通用 key = value 文本配置（用于全局配置与每题 problem.conf）----
// 格式：每行 "key = value"，支持空行与以 '#' 开头的注释行

// 读取 kv 文件。文件不存在返回 false（error 为空）；打开失败 error 非空
bool read_kv_file(const std::filesystem::path &path,
                  std::map<std::string, std::string> &kv, std::string &error);

// 写入 kv 文件（自动创建父目录）
bool write_kv_file(const std::filesystem::path &path,
                   const std::map<std::string, std::string> &kv,
                   std::string &error);

// ---- 全局配置 ----

// 配置文件默认路径：
//   Linux: $XDG_CONFIG_HOME/ttt-cli/config.txt 或 ~/.config/ttt-cli/config.txt
//   Windows: %APPDATA%\ttt-cli\config.txt
// 环境变量 TTT_CLI_CONFIG 可显式指定完整路径
std::filesystem::path default_config_path();

// 读取全局配置。文件不存在返回 false 且 error 为空
bool load_config(const std::filesystem::path &path, CliConfig &cfg,
                 std::string &error);

// 写入全局配置
bool save_config(const std::filesystem::path &path, const CliConfig &cfg,
                 std::string &error);

// 确保配置存在：不存在则创建默认配置
// （题库 = ~/ttt-bank，时间1000ms，内存256MB）
bool ensure_config(const std::filesystem::path &path, CliConfig &cfg,
                   std::string &error);

#endif // CLI_CONFIG_HPP
