# ttt-cli：测评机命令行工具

在终端中管理题库、提交代码测评、查看测评记录。

```bash
ttt-cli add    [--name <name>] [--dir <dir_path>] [--time-limit <ms>] [--memory-limit <mb>]
ttt-cli submit [--name <name>] [--source <source_path>] [--non-o2]
ttt-cli view   [--name <name>] [--id <test_id>]
ttt-cli remove [--name <name>]
ttt-cli help
```

未指定的参数会交互式询问；提示处直接回车会打开全屏文件选择器（file_picker）。
`help` 无参数，直接打印 `docs/ttt_cli_help.txt` 的内容（中文，随构建复制到可执行文件旁；
查找顺序：可执行文件目录 → 其 docs/ → 当前目录 → 当前目录/docs）。

## 配置文件

- 位置：Linux `$XDG_CONFIG_HOME/ttt-cli/config.txt` 或 `~/.config/ttt-cli/config.txt`；
  Windows `%APPDATA%\ttt-cli\config.txt`；环境变量 `TTT_CLI_CONFIG` 可显式指定完整路径。
- 内容（`key = value` 文本格式，首次运行自动创建默认值）：
  ```
  bank_dir = /home/user/ttt-bank          # 题库文件夹路径（不存在时自动创建）
  default_time_limit_ms = 1000            # 默认时间限制（ms）
  default_memory_limit_mb = 256           # 默认空间限制（MB）
  ```
- 题库结构（每个题目一个文件夹）：
  ```
  <bank>/<name>/problem.conf   该题时间/内存限制（add 时写入，submit 时读取）
  <bank>/<name>/tests/         测试数据（.in/.out，add 时从 --dir 复制）
  <bank>/<name>/records/       submit 生成的结果文件（唯一随机文件名）
  ```

## add：添加题目

| 参数 | 说明 |
| --- | --- |
| `--name <name>` | 题库中的文件夹名称（支持中文）；未指定则询问 |
| `--dir <dir_path>` | 测试数据原文件夹（必须存在）；未指定则询问，回车用文件选择器单选文件夹 |
| `--time-limit <ms>` | 该题时间限制；未指定则询问（提示显示默认值，回车用全局默认，非法输入重新询问） |
| `--memory-limit <mb>` | 该题空间限制；未指定则询问（同上） |

- 名称已存在时先询问是否删除后重建（`y/N`）。
- 复制测试数据后立即用 `pack_tests` 校验（.in/.out 配对），失败只警告不回滚。
- 每题的 `time_limit_ms` / `memory_limit_mb` 写入 `problem.conf`。

## submit：提交代码测评

| 参数 | 说明 |
| --- | --- |
| `--name <name>` | 题目名称；未指定则询问，回车用文件选择器从题库选择（只保留末级文件夹名），随后校验是否存在于题库 |
| `--source <source_path>` | 源代码路径；未指定则询问，回车用文件选择器选择 |
| `--non-o2` | 编译时禁用 `-O2` 优化 |

流程：读取该题 `problem.conf`（缺失回退全局默认）→ `pack_tests` → `run_tests`
（`--non-o2` 时以 `-O0` 编译）→ 结果以唯一随机文件名保存到
`<bank>/<name>/records/result_<时间戳>_<随机hex>.txt` → 打印摘要与结果文件路径。

## view：查看测评记录

| 参数 | 说明 |
| --- | --- |
| `--name <name>` | 题目名称；逻辑同 submit |
| `--id <test_id>` | 结果文件（随机文件名，可传绝对路径、records 下相对路径或题目目录下相对路径）；未指定则询问，回车用文件选择器（初始目录为 records） |

打开后进入全屏结果查看器（`test_result_viewer`）：CE 视图或逐测试点视图，
`↑/↓/j/k`/滚轮滚动，CE 视图支持 `←/→` 横向滚动，`q`/`Esc` 退出。
无法解析的文件显示错误提示。

## remove：删除题目

| 参数 | 说明 |
| --- | --- |
| `--name <name>` | 要删除的题目名称；未指定则询问，回车用文件选择器从题库选择 |

删除整个题目文件夹（含 `tests/` 与 `records/`）；题目不存在时报错。

## help：显示帮助

无参数，打印 `docs/ttt_cli_help.txt` 的内容（含中文）。帮助文件随构建复制到
可执行文件旁，也可在源码树运行时从 `当前目录/docs` 找到；缺失时打印英文错误提示。

## 源码结构

- `include/ttt_cli.hpp`：各操作共享的类型（`CliArgs`/`ProblemConf`）与接口声明。
- `src/ttt_cli/`：操作的实现按文件拆分：
  - `main.cpp`：入口，按命令表分发（`kCommands[]`）；
  - `common.cpp`：交互输入、文件选择器封装、题目名校验、结果文件名、参数解析、`problem.conf` 读写等通用辅助；
  - `cmd_add.cpp` / `cmd_submit.cpp` / `cmd_view.cpp` / `cmd_remove.cpp` /
    `cmd_help.cpp`：各操作实现。

新增操作（如 `help`/`config`）只需：在 `ttt_cli.hpp` 声明 `int cmd_xxx(...)`、
新建 `src/ttt_cli/cmd_xxx.cpp` 实现、CMakeLists 的 `ttt-cli` 目标加一个源文件、
`main.cpp` 命令表加一行、更新 `print_usage`。

## 退出码

- `0`：成功（view 正常退出也为 0）
- `1`：运行错误（参数校验失败、权限问题、测评失败等）
- `2`：用法错误（未知命令/参数）

## 构建

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
# 产物：build/ttt-cli
```

## 说明

- 交互提示通过直接读 stdin 实现（不经过 iostream 缓冲），与 file_picker 的
  原始模式读取互不干扰。
- 控制台输出为英文；题目名称与路径支持中文（UTF-8）。
