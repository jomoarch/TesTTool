#ifndef TEST_RESULT_VIEWER_HPP
#define TEST_RESULT_VIEWER_HPP

#include <filesystem>

// 全屏查看测评结果文件（纯文本渲染，无任何颜色/样式）
//   - CE 文件：第一行 [CE] + "===" 分割线 + 完整编译错误信息
//     支持 ←/→ 横向滚动（最多到最长行宽-1）与 ↑/↓/j/k/滚轮 纵向滚动
//     [CE] 头固定不随滚动移动
//   - 正常文件：每个测试点首行显示 "#编号# [状态] ====...==== 用时ms 内存MB"
//     （=== 分隔线并入首行，填满状态与用时/内存之间的空隙），首行整行青色背景
//     （黑字）；非 AC 测试点的提示信息直接跟在首行下面，每行以 "| " 前缀
//     （按宽度-2 折行）；支持 ↑/↓/j/k/滚轮 纵向滚动
//   - 无法无损解析的文件：全屏错误提示，按任意键退出
// 阻塞直到用户退出；返回 0 表示正常退出
int view_test_result(const std::filesystem::path &file_path);

#endif // TEST_RESULT_VIEWER_HPP
