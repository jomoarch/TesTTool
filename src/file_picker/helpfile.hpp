#ifndef FILE_PICKER_HELPFILE_HPP
#define FILE_PICKER_HELPFILE_HPP

#include <filesystem>
#include <string>
#include <vector>

namespace fp {

std::filesystem::path exe_directory();

// 在可执行文件目录、其 docs/ 子目录、当前目录、当前目录/docs 下查找指定文件
// （供 file_picker 帮助、ttt-cli help 等复用）
// 找到返回完整路径，否则返回空
std::filesystem::path find_nearby_file(const std::string &name);

// 定位帮助文件（默认名 file_picker_help.txt）
// explicit_path 非空时优先使用（不存在则返回空）
// 否则依次尝试：可执行文件目录、其 docs/ 子目录、当前目录、当前目录/docs
// 找到返回完整路径，找不到返回空路径
std::filesystem::path
find_help_file(const std::filesystem::path &explicit_path);

// 以 UTF-8 读取文本文件并拆成行
// 成功返回 true
// 失败时 error 填入英文原因，lines 被清空
bool load_help_lines(const std::filesystem::path &path,
                     std::vector<std::string> &lines, std::string &error);

} // namespace fp

#endif // FILE_PICKER_HELPFILE_HPP