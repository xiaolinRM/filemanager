[English Version](https://github.com/xiaolinRM/filemanager/blob/main/README.md)

# File Manager

一个 SourceMod 扩展，为 SourcePawn 插件提供快速的文件和文件夹管理 native：
复制、移动（重命名）和递归删除，支持通配符和详细的错误信息。

本扩展把 SourcePawn 常见的“读一块、写一块”复制循环替换为操作系统的快速复制
路径，因此大文件可以按磁盘速度复制，而不是经过 SourcePawn 虚拟机。

*本扩展使用 **DeepSeek Harness** 编写，使用模型 **DeepSeek-V4.1-Flash-1017**。*

```sourcepawn
#include <filemanager>

FileManager.Copy("./left4dead2/cfg/server.cfg", "./backup/server.cfg", true);
FileManager.Move("./left4dead2/addons/sourcemod/logs/*.log", "./backup/logs/");
FileManager.Delete("./backup/logs/*.log");

char error[256];
if (!FileManager.Copy("./left4dead2/addons/sourcemod/data", "./backup/data"))
{
    FileManager.GetLastError(error, sizeof(error));
    LogError("copy failed: %s", error);
}
```

## 功能

- 复制、移动、删除文件和整个目录树。
- 源路径支持 shell 风格通配符（`*`、`?`、`[abc]`、`[a-z]`、`[!a-z]`），
  包括中间路径组件中的通配符。
- 相对路径以 **服务端目录**（`srcds` 所在目录）为基准，因此 `./` 和 `../`
  的行为与在服务端目录中打开 shell 一致。
- 类似 `cp`/`mv` 的重命名：如果目标不是已存在的目录，源会被重命名到目标
  路径。
- 强制覆盖（`force = true`）。
- 目录操作递归执行，包含空目录。
- 提供详细的系统错误信息（`GetLastError`）。
- 两个平台均支持 UTF-8 路径。
- 安全保护：拒绝删除文件系统根、服务端目录本身或服务端目录的祖先目录。

## 环境要求

- SourceMod 1.11 源码树（只用到 `public/` 和 `sourcepawn/include`）。已在
  SourceMod 1.11.0.6962 上测试。
- AMBuild 2.2 或更新版本（`pip install git+https://github.com/alliedmodders/ambuild.git`）。
- Windows：Visual Studio 2015 或更新版本。AMBuild 会自动查找 x86 工具链，
  为 `srcds.exe` 生成 32 位二进制。
- Linux：支持 32 位编译的编译器（Debian/Ubuntu 上为 `gcc-multilib` /
  `g++-multilib`），为 `srcds_linux` 生成 32 位 `.so`。

不需要 HL2SDK，也不需要 Metamod:Source 头文件。本扩展是纯 SourceMod 扩展，
直接编译 `public/smsdk_ext.cpp`。

## 预编译二进制

`dist/` 中提供了可直接安装的二进制：

```
dist/addons/sourcemod/extensions/filemanager.ext.dll   (Windows x86)
dist/addons/sourcemod/extensions/filemanager.ext.so    (Linux x86)
dist/addons/sourcemod/scripting/include/filemanager.inc
```

可以直接复制到服务端，也可以按下面的说明从源码构建。

## 构建

### Windows

```bat
build_win.bat --sm-path=..\sourcemod
```

输出位于 `build\package\addons\sourcemod\extensions\filemanager.ext.dll`。

如果 AMBuild 找不到 Visual Studio，请在 “x86 Native Tools Command Prompt”
中运行脚本，而不是普通命令提示符。

### Linux

```sh
./build_linux.sh
```

输出位于 `build/package/addons/sourcemod/extensions/filemanager.ext.so`。

`build_linux.sh` 默认使用 `../sourcemod`。如果 SourceMod 源码树在别处，请传
`--sm-path=/path/to/sourcemod` 或设置 `SM_PATH`。

脚本默认强制使用 GCC（`CC=gcc`、`CXX=g++`）。Clang 也可以，但推荐 GCC，
因为发布二进制使用静态 libstdc++ 和 libgcc。

### Linux 兼容性构建（发布推荐）

`dist/` 中的 Linux 预编译二进制是在 Ubuntu 18.04（glibc 2.27）chroot 中用
GCC 7.5 构建的。如果直接在 Ubuntu 22.04 上构建，`stat`/`lstat` 会引用
`GLIBC_2.33`，导致扩展无法在较旧的服务端上加载。

源码本身也完全避免了 `stat`/`lstat`/`fstat`，改用 `readlink`、
`open(O_DIRECTORY)` 和 `open` 判断文件类型，因此在现代发行版上正常构建也
不会引入新的 glibc 符号。Ubuntu 18.04 构建还让 C++ 运行时的符号引用保持
较旧。

发布版 `filemanager.ext.so` 的属性：

- ELF 32 位 i386 共享库。
- `NEEDED`：仅 `libpthread.so.0`、`libc.so.6`、`ld-linux.so.2`。
- 最高要求的符号版本：`GLIBC_2.4`（可在 CentOS 5/6/7、Debian、
  Ubuntu 8.04 及更新的 32 位服务端上运行）。
- `libstdc++` 和 `libgcc` 静态链接，因此运行时不需要任何 `GLIBCXX_*`
  版本。

Windows 二进制使用静态 CRT（`/MT`），只依赖 `KERNEL32.dll`，不需要
Visual C++ 运行库。

## 安装

把文件复制到服务端：

```
addons/sourcemod/extensions/filemanager.ext.dll     (Windows)
addons/sourcemod/extensions/filemanager.ext.so      (Linux)
addons/sourcemod/scripting/include/filemanager.inc
```

SourceMod 会在启动时自动加载扩展，也可以手动执行
`sm exts load filemanager`。

## 行为说明

### 路径

- 绝对路径按原样使用。
- 相对路径以服务端目录（`srcds.exe` / `srcds_linux` 所在目录）为基准，
  而不是游戏/mod 目录。对于普通的 L4D2 服务端，`./left4dead2/addons`
  就是 SourceMod 的 addons 目录。
- `..` 和 `.` 组件会被规范化。目标路径以斜杠结尾表示“必须是已存在的
  目录”。
- 在 Windows 上，绝对路径必须以盘符（`C:\...`）或 UNC 前缀
  （`\\server\share`）开头。单个前导斜杠会被视为相对于服务端目录。
- 两个平台都接受 UTF-8。SourceMod 自带的文件 native 在 Windows 上使用
  ANSI 代码页，因此如果插件用这些 native 创建文件，再把同一路径传给本
  扩展，非 ASCII 字符可能得到不同的文件名；使用本扩展时请统一使用
  UTF-8（例如来自数据库或配置的路径）。

### 通配符

- `*` 匹配零个或多个字符，`?` 匹配一个字符（一个 UTF-8 码点），
  `[...]` 匹配字符类。
- 通配符可以出现在任意组件中，例如 `./logs/*/*.txt`。
- 前导点必须显式匹配（shell 规则）：`*` 不匹配 `.hidden`，`.*` 会匹配。
- Windows 上匹配不区分大小写，Linux 上区分大小写。
- 目标路径不能包含通配符。

### 目标路径语义（cp/mv 风格）

- 如果 `newpath` 是已存在的目录，源会被放入该目录并保留原名。
- 否则 `newpath` 就是源的新名称，因此可以在复制或移动时重命名。
- 如果最终目标已存在，`force = false` 会失败；`force = true` 会替换它。
  替换支持跨类型（文件覆盖目录、目录覆盖文件）。移动操作中，文件覆盖文件
  是原子替换；其他情况会先把旧目标移到临时名称，只有新数据就位后才删除。
  复制操作会先删除旧目标，再写入新数据。
- 多个源（通配符匹配到多个项）要求 `newpath` 是已存在的目录。

### 递归与链接

- 目录始终递归处理，包含文件、子目录和空目录。
- Linux 上符号链接按符号链接复制，删除时不会触碰其目标。
- Windows 上文件符号链接会被跟随并复制目标内容；目录重解析点
  （junction/目录符号链接）无法复制。
- 移动或删除符号链接时，移动/删除的是链接本身。

### 错误

- `GetLastError(error, maxlength)` 写入最近一次失败信息；如果存在错误
  消息则返回 true。
- 完全成功的操作会清除错误消息，因此请在 native 返回 false 后立即读取。
- 错误信息包含操作、路径和系统错误文本（`errno` 或 `Win32` 错误码）。

### 安全

`Delete` 会拒绝：

- 文件系统根（`/`、`C:\`、UNC 共享根），
- 服务端目录本身（`./`），
- 服务端目录的任意祖先目录（`..`、`..\..`、`G:\Games` 等）。

除此之外的路径（无论在服务端目录内还是外）都允许操作，调用方需要自行
确认路径正确。

## 性能

- Windows 使用 `CopyFileExW`，走操作系统复制引擎（SMB 共享上支持服务端
  复制），远比 SourcePawn 循环快。
- Linux 按顺序尝试：
  1. `FICLONE`（reflink；在 btrfs/XFS 上是瞬时的），
  2. `copy_file_range`（内核内复制），
  3. `sendfile`（零拷贝），
  4. 1 MiB 的 `read`/`write` 回退。
- 同卷移动使用 `rename`/`MoveFileExW`，不复制数据。
- 跨卷移动先复制，成功后才删除源。
- 目录遍历使用原生目录枚举；Linux 上使用 `d_type` 避免不必要的 `stat`
  调用。

在测试机器上，独立基准测试在 Windows 上复制 128 MB 文件（缓存命中）约
0.03 秒；在虚拟机的虚拟磁盘上复制 256 MB 文件约 1.9 秒（约 130 MB/s，
受磁盘限制）。具体数字取决于存储设备。

这些 native 是同步的。非常大的复制会阻塞服务端线程；请把大型维护操作
安排在换图时或空服时执行。

## 测试

仓库包含两套测试：

- `tools/fmtest.cpp` —— 针对核心逻辑的独立测试程序（不依赖 SourceMod）。
  覆盖复制/移动/删除、通配符、字符类、隐藏文件、UTF-8、绝对/相对路径、
  强制替换、跨类型替换、递归、空目录、跨文件系统移动（Linux）、受保护
  路径和大文件基准测试。
- `tests/filemanager_test.sp` —— SourceMod 测试插件，在真实服务端上调用
  native，写入 `addons/sourcemod/data/fmtest_result.txt`；如果存在
  `addons/sourcemod/data/fmtest_run` 标记文件，测试结束后会自动退出
  服务端。

已测试配置：

- Windows L4D2 服务端 2.1.5.5，SourceMod 1.11.0.6962：63/63 项检查通过。
- Linux L4D2 服务端（Ubuntu 22.04 虚拟机），SourceMod 1.11.0.6962：
  66/66 项检查通过。
- 独立测试程序：Windows 104/104，Linux 109/109。

## 目录结构

```
.
├── .gitignore
├── AMBuildScript          AMBuild 2.2 工程（x86，Windows + Linux）
├── AMBuilder
├── PackageScript
├── configure.py
├── build_win.bat
├── build_linux.sh
├── README.md
├── README_ZH.md
├── dist/
│   ├── README.md
│   ├── README_ZH.md
│   └── addons/sourcemod/
│       ├── extensions/
│       │   ├── filemanager.ext.dll
│       │   └── filemanager.ext.so
│       └── scripting/include/filemanager.inc
├── include/filemanager.inc
├── src/
│   ├── extension.cpp      SourceMod 入口和 native
│   ├── fm.h               核心接口
│   ├── fm_core.cpp        路径处理、通配符、规划、递归
│   ├── fm_win.cpp         Win32 后端
│   ├── fm_linux.cpp       Linux 后端
│   ├── fm_throw_stubs.cpp Linux C++ 运行时兼容补丁
│   └── smsdk_config.h
├── tests/filemanager_test.sp
└── tools/fmtest.cpp
```

## 限制

- 仅提供 32 位二进制，对应 L4D2 的 `srcds.exe` / `srcds_linux`。其他使用
  64 位 SourceMod 的游戏需要单独构建 x86_64 版本。
- Linux 上不保留文件权限、所有者和时间戳。
- Windows 上无法复制目录重解析点（会报告错误，并继续处理目录中的其他
  项）。
- 替换目标时使用的临时文件名采用 `.fm.<pid>.<counter>.tmp` 后缀，位于
  目标同目录。

## 作者

**作者：** DeepSeek

本扩展使用 **DeepSeek Harness** 编写，使用模型 **DeepSeek-V4.1-Flash-1017**。

## 许可证

GPL-3.0。`src/` 和 `include/` 中的源码文件头部包含完整的 GPLv3 声明。参见
<https://www.gnu.org/licenses/gpl-3.0.html>。
