# 测评机：测评函数 / 结果文件 / 结果查看器

测评机的三个核心工具：`run_tests`（测评）、`save/load_test_result`（结果文件存取）、
`view_test_result`（全屏查看器）。

- 公共 API：`include/test_runner.hpp`、`include/test_result_file.hpp`、`include/test_result_viewer.hpp`
- 实现：`src/test_runner.cpp`、`src/test_result_file.cpp`、`src/test_result_viewer.cpp`
- 演示：`test/src/run_tests_demo.cpp`、`test/src/view_result_demo.cpp`
- 单元测试：`test/src/test_test_runner.cpp`（非交互；含真实编译运行 AC/WA/TLE/MLE/RE）

## 构建（手动）

```bash
g++ -std=c++17 -O2 -I include -I src/file_picker \
  src/compiler.cpp src/test_packager.cpp src/run_program.cpp src/file_comparator.cpp \
  src/test_runner.cpp src/test_result_file.cpp src/test_result_viewer.cpp \
  src/file_picker/*.cpp \
  test/src/run_tests_demo.cpp -o run_tests_demo
```

（`view_result_demo` / `test_test_runner` 同样方式，替换最后一个源文件即可。）

## 1. 测评函数 `run_tests(source_path, pkg)`

输入：源代码路径 + `TestPackage`（由 `pack_tests` 生成）。输出 `AssessmentResult`：

```cpp
enum class Verdict { AC, WA, TLE, MLE, RE };

struct CaseResult {
  int id;              // 测试点编号（1..N）
  Verdict verdict;
  int time_ms;         // CPU 用时（ms），来自 run_program
  float memory_mb;     // 内存占用（MB），来自 run_program
  std::string message; // 提示信息；AC 时为空
};

struct AssessmentResult {
  bool compile_ok;
  std::string compile_message;  // 无错误时固定为 "Compilation succeeded"
  std::vector<CaseResult> cases;
};
```

流程：`compile`（成功→`"Compilation succeeded"`，失败→编译器错误信息、无测试点）→
对每个测试点 `run_program`（应用时间/内存限制）→ 成功运行的用 `file_comparator` 判定。

状态映射：

| run_program / file_comparator | Verdict | message 来源 |
| --- | --- | --- |
| `_TLE` | TLE | run_program.message |
| `_MLE` | MLE | run_program.message |
| 信号 / `_FAILED` | RE | run_program.message（空则 `get_res_message`） |
| 运行成功且 `matched` | AC | 空 |
| 运行成功但未匹配 | WA | compare_files.message |

可执行文件与运行输出使用临时文件（进程内自增序号避免冲突），结束后清理。

## 2. 结果文件格式（易解析、无损）

```
TESTOOL_RESULT v1
CE 1                            <- CE 特殊标记，快速判断是否编译失败
<base64(compile_message)>       <- 编译错误信息（base64 单行，逐字节无损）
```
或
```
TESTOOL_RESULT v1
CE 0
<N>                             <- 测试点数量
CASE <id> <AC|WA|TLE|MLE|RE> <time_ms> <mem_mb>
<base64(case_message)>          <- 提示信息（AC 为空串）
... 共 N 组（id 必须为 1..N 升序）
```

- 消息用 base64 单行存储：多行/中文/任意字节内容都不会破坏文件的行结构，
  编译错误的换行可在查看器中严格还原。
- `load_test_result` 严格校验：魔数、CE 标记、数量、编号顺序、状态枚举、
  数字格式与 base64 合法性；任何不符（含文件末尾多余内容）都判定为
  “不是正确且无损的结果文件”并返回错误原因。查看器用它逆处理成
  `AssessmentResult` 后再渲染。

## 3. 查看器 `view_test_result(path)`

全屏渲染（进入备用屏幕，退出完全恢复；支持鼠标滚轮；纯文本，无颜色/样式）。

### CE 视图（编译失败文件）
- 第一行左侧为 `[CE]`，右侧留空。
- 第二行 `===` 全宽分割线。
- 下方完整打印编译错误信息：严格按行显示，**不因终端宽度换行**；
  横向用 `←/→` 滚动（最多到“最长行宽−1”，即至少留下一个字符），
  纵向用 `↑/↓`/`j/k`/滚轮滚动（最多到“总行数−1”）；
  `[CE]` 头固定不随滚动移动。
- `q`/`Esc` 退出。

### 逐测试点视图（编译成功文件）
- 每个测试点首行：`#编号#`（编号右对齐到最大编号宽度）空格 `[状态]` 空格
  `====...====` 空格 `用时ms` 空格 `内存MB`——`===` 分隔线并入首行，
  填满 `[状态]` 与用时/内存之间的空隙；**首行整行青色背景色（黑字，含补齐的空格）**。
- 非 AC 测试点：提示信息直接跟在首行下面，每行以 `| ` 前缀（`|` 后空一格，
  按终端宽度−2 折行，保证 `| ` + 行 = 终端宽度）；AC 无提示信息。
- `↑/↓`/`j/k`/滚轮 纵向滚动，首行（`#编号#` 行）随内容一起滚动。
- `q`/`Esc` 退出。

### 非法文件
打开无法无损解析的文件时显示全屏错误提示（路径 + 原因），任意键退出。

## 说明

- 查看器/选择器共用 `fp::Terminal`（`src/file_picker/terminal.hpp`），
  本次为支持滚轮在终端层新增了鼠标追踪（POSIX `\033[?1000h\033[?1006h` +
  SGR 鼠标序列解析；Windows `ENABLE_MOUSE_INPUT` + `MOUSE_WHEELED`），
  并修复了输入解析中“进入中间状态的字节未及时移出缓冲、跨 read 分批到达时
  被重复解析”的隐患（影响所有使用该终端的工具）。
- 内存占用按 `%.2f` 存入文件（MB 两位小数），数值型字段解析严格。
