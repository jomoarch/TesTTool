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
- **ttt**：交互式外壳（详见 [docs/ttt.md](docs/ttt.md)）：
  bash 风格提示符（绝对路径 + `$`），内置 `cd`/`ls`/`goto`（file_picker 跳转）/
  `quit`，其余命令转交 ttt-cli；位置为虚拟位置，不改变真实路径。

## 构建

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

产物：`build/ttt`（交互式外壳）、`build/ttt-cli`（CLI）、`build/libtettool.a`、
`build/run_tests_demo`、`build/view_result_demo`、`build/test_test_runner`、
`build/test_ttt_cli`。

## 预编译版本

默认动态链接时，exe 依赖 MinGW 运行库 `libgcc_s_seh-1.dll` 与 `libstdc++-6.dll`
（位于 `C:\msys64\ucrt64\bin`），拷到其他电脑直接运行会提示缺库。两种解决方式：

1. **一键打包脚本（推荐）**：在仓库根目录运行
   ```powershell
   powershell -ExecutionPolicy Bypass -File package.ps1
   ```
   脚本会以静态链接方式（`TTT_STATIC_RUNTIME=ON`）构建到独立的 `build-portable/`，
   汇总 `ttt.exe`、`ttt-cli.exe` 与帮助文件到 `dist\ttt-portable-win64\`，
   验证依赖后生成 `dist\ttt-portable-win64.zip`。静态版 exe 不再依赖任何
   MinGW DLL，只需 Win10/11（UCRT 系统自带），整个包可直接拷走运行。
   加 `-Dynamic` 参数则保持动态链接，改为把所需运行库 DLL 一并拷入包内分发。

2. **手工静态链接**：
   ```bash
   cmake -S . -B build-portable -DCMAKE_BUILD_TYPE=Release -DTTT_STATIC_RUNTIME=ON
   cmake --build build-portable -j
   ```

3. **安装到指定目录**（动态链接，需另拷运行库 DLL）：
   ```bash
   cmake --install build --prefix <安装目录>
   ```