#ifndef TEST_RESULT_FILE_HPP
#define TEST_RESULT_FILE_HPP

#include "test_runner.hpp"

#include <filesystem>
#include <string>

// =============================================================================
// 测评结果文件（文本格式，易解析、可无损逆处理回测评信息包）：
//
//   编译失败（CE）：
//     TESTOOL_RESULT v1
//     CE 1                                  <- CE 特殊标记，便于快速判断
//     <base64(compile_message)>             <- 编译错误信息，base64 单行
//
//   编译成功：
//     TESTOOL_RESULT v1
//     CE 0
//     <N>                                   <- 测试点数量
//     CASE <id> <AC|WA|TLE|MLE|RE> <time_ms> <mem_mb>
//     <base64(case_message)>                <- 提示信息，AC 为空串
//     ... （共 N 组，id 必须为 1..N 升序）
//
// base64 保证多行 / 中文 / 任意字节内容无损存储且不破坏文件的行结构
// 因此 CE 的编译错误换行在查看器中可以严格还原
// =============================================================================

// 保存测评信息包到文件，失败返回 false 并填充 error（英文）
bool save_test_result(const std::filesystem::path &file_path,
                      const AssessmentResult &result, std::string &error);

// 从文件逆处理回测评信息包（查看器渲染的基础）
// 严格校验：魔数、CE 标记、数量、编号顺序、状态枚举、数字与 base64 等
// 无法解析（不是正确且无损的结果文件）时返回 false 并填充 error
bool load_test_result(const std::filesystem::path &file_path,
                      AssessmentResult &result, std::string &error);

#endif // TEST_RESULT_FILE_HPP
