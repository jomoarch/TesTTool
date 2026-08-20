# TesTTool
一个简单的本地测评工具。

## 模块

- `run_program`：监控可执行文件运行时的耗时/内存/退出状态，返回信息包。
- `file_comparator`：比较两个文件（如输出与期望），返回匹配结果与提示。
- `compiler`：编译 C++ 源文件并指定输出路径，返回编译信息包。
- `test_packager`：扫描测试点目录（`N.in`/`N.out`），生成测试点信息包。
- `file_picker`：跨平台交互式三栏文件选择器（详见 [docs/file_picker.md](docs/file_picker.md)）。
- **测评机**（详见 [docs/test_tool.md](docs/test_tool.md)）：
  - `test_runner`：`run_tests(源码, TestPackage)` → 测评信息包。
  - `test_result_file`：测评信息包 ↔ 文本文件（易解析、无损、含 CE 标记）。
  - `test_result_viewer`：全屏查看结果文件（支持滚动与鼠标滚轮）。
- **ttt-cli**：测评机命令行工具（详见 [docs/ttt_cli.md](docs/ttt_cli.md)）：
  `add` 添加题目、`submit` 提交代码测评、`view` 查看记录、`remove` 删除题目、
  `help` 显示帮助（内容在 `docs/ttt_cli_help.txt`）；
  带配置文件与 file_picker 集成，操作实现拆分在 `src/ttt_cli/` 下便于扩展。

## 构建

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

产物：`build/ttt-cli`（CLI）、`build/libtettool.a`、`build/run_tests_demo`、
`build/view_result_demo`、`build/test_test_runner`、`build/test_ttt_cli`。