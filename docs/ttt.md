# ttt：交互式测评机外壳

`./ttt` 启动后进入交互式命令行，bash 风格提示符显示**绝对路径**：

```
/home/user/project$ _
```

逐行输入命令；内置命令直接处理，其余命令（`add`/`submit`/`view`/`remove`/`help`）
原样转交给 `ttt-cli` 子进程执行（参数、交互提示、file_picker 行为与直接运行
ttt-cli 完全一致）。

## 内置命令

| 命令 | 说明 |
| --- | --- |
| `cd [路径]` | 切换虚拟当前目录；无参数回到 HOME；支持绝对/相对路径与 `..` |
| `ls [路径]` | 列出虚拟当前目录（或指定目录）的条目，目录以 `/` 结尾，隐藏文件不显示 |
| `goto` | 无参数，调用 file_picker 单选文件夹，选中后跳到该位置 |
| `quit` / `exit` | 退出 ttt（`Ctrl-D`/EOF 同样退出） |

其他命令（`add ...` / `submit ...` / `quit` 之外的任意以 `--` 开头的 ttt-cli
命令）直接转交 ttt-cli 执行。

## 虚拟位置

- `cd`/`ls`/`goto` 只改变 ttt **内部**的虚拟当前目录，ttt 进程自身的真实
  工作目录始终不变，启动 ttt 的外层 shell 的路径不受任何影响。
- 转交 ttt-cli 时，由子进程 `chdir` 到虚拟位置再执行，因此命令中的相对路径
  （如 `add --dir .`、`--source ./main.cpp`）都相对虚拟位置解析。
- ttt 本身不提供直接修改文件系统的操作；文件变更只经由 `add`/`remove`
  等 ttt-cli 命令。

## 命令行分词

与 shell 类似：空白分隔，支持双引号 `"..."`、单引号 `'...'` 与反斜杠转义，
可传入含空格/中文的路径。

## 依赖

- 转交命令需要能找到 `ttt-cli`：优先可执行文件同目录，其次 `PATH`。
- 配置文件、题库等均复用 ttt-cli 的机制（`TTT_CLI_CONFIG` 环境变量等，
  见 [ttt_cli.md](ttt_cli.md)）。

## 构建

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
# 产物：build/ttt（与 build/ttt-cli 同目录）
```
