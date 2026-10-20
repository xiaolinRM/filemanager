[中文版本](https://github.com/xiaolinRM/filemanager/blob/main/README_ZH.md)

# File Manager

A SourceMod extension that provides fast file and directory management natives
for SourcePawn plugins: copy, move (rename) and recursive delete, with wildcard
support and useful error reporting.

The extension replaces the usual SourcePawn "read a block, write a block" copy
loop with the operating system's fast copy paths, so large files are copied at
disk speed instead of through the SourcePawn VM.

*Written with **DeepSeek Harness** using the **DeepSeek-V4.1-Flash-1017** model.*

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

## Features

- Copy, move and delete files and whole directory trees.
- Shell-style wildcards in the source path (`*`, `?`, `[abc]`, `[a-z]`,
  `[!a-z]`), including wildcards in intermediate path components.
- Relative paths are resolved against the **server directory** (the directory
  that contains `srcds`), so `./` and `../` behave like they do in a shell
  started in the server directory.
- `cp`/`mv`-style rename: if the destination is not an existing directory, the
  source is renamed to it.
- Force overwrite (`force = true`).
- Recursive directory operations, including empty directories.
- Detailed error messages from the OS (`GetLastError`).
- UTF-8 path support on both platforms.
- Safety guard: deleting a filesystem root, the server directory itself, or an
  ancestor of the server directory is refused.

## Requirements

- A SourceMod 1.11 source tree (only `public/` and `sourcepawn/include` are
  used). Tested with SourceMod 1.11.0.6962.
- AMBuild 2.2 or newer (`pip install git+https://github.com/alliedmodders/ambuild.git`).
- Windows: Visual Studio 2015 or newer. AMBuild finds the x86 toolchain
  automatically; a 32-bit binary is produced for `srcds.exe`.
- Linux: a compiler with 32-bit support (`gcc-multilib` / `g++-multilib` on
  Debian/Ubuntu). A 32-bit `.so` is produced for `srcds_linux`.

No HL2SDK and no Metamod:Source headers are required. The extension is a pure
SourceMod extension and compiles against `public/smsdk_ext.cpp`.

## Prebuilt binaries

Ready-to-install binaries are provided in `dist/`:

```
dist/addons/sourcemod/extensions/filemanager.ext.dll   (Windows x86)
dist/addons/sourcemod/extensions/filemanager.ext.so    (Linux x86)
dist/addons/sourcemod/scripting/include/filemanager.inc
```

You can copy them directly to a server, or build from source using the
instructions below.

## Building

### Windows

```bat
build_win.bat --sm-path=..\sourcemod
```

The output is `build\package\addons\sourcemod\extensions\filemanager.ext.dll`.

If AMBuild cannot find Visual Studio, run the script from an "x86 Native Tools
Command Prompt" instead of a normal command prompt.

### Linux

```sh
./build_linux.sh
```

The output is `build/package/addons/sourcemod/extensions/filemanager.ext.so`.

`build_linux.sh` uses `../sourcemod` by default. Pass `--sm-path=/path/to/sourcemod`
or set `SM_PATH` if the SourceMod source tree is somewhere else.

The script forces GCC by default (`CC=gcc`, `CXX=g++`). Clang also works, but GCC
is recommended because the release binary is built with a static libstdc++ and
libgcc.

### Linux compatibility build (recommended for release)

The prebuilt Linux binary in `dist/` is built inside an Ubuntu 18.04
(glibc 2.27) chroot with GCC 7.5. Building on Ubuntu 22.04 directly would
version `stat`/`lstat` as `GLIBC_2.33` and prevent the extension from loading on
older servers.

The source itself also avoids `stat`/`lstat`/`fstat` entirely and determines
file types with `readlink`, `open(O_DIRECTORY)` and `open`, so a normal build on
a modern distribution does not require new glibc symbols either. The Ubuntu
18.04 build additionally keeps the C++ runtime references old.

Resulting properties of the prebuilt `filemanager.ext.so`:

- ELF 32-bit i386 shared object.
- `NEEDED`: `libpthread.so.0`, `libc.so.6`, `ld-linux.so.2` only.
- Highest required symbol: `GLIBC_2.4` (works on CentOS 5/6/7, Debian,
  Ubuntu 8.04 and newer 32-bit servers).
- `libstdc++` and `libgcc` are linked statically, so no `GLIBCXX_*` version is
  required at runtime.

The Windows binary is linked against the static CRT (`/MT`) and depends only on
`KERNEL32.dll`, so no Visual C++ redistributable is needed.

## Installation

Copy the files to the server:

```
addons/sourcemod/extensions/filemanager.ext.dll     (Windows)
addons/sourcemod/extensions/filemanager.ext.so      (Linux)
addons/sourcemod/scripting/include/filemanager.inc
```

SourceMod loads the extension automatically at startup. It can also be loaded
manually with `sm exts load filemanager`.

## Behaviour

### Paths

- Absolute paths are used as-is.
- Relative paths are resolved against the server directory (the directory that
  contains `srcds.exe` / `srcds_linux`), not the game/mod directory. For a
  normal L4D2 server, `./left4dead2/addons` is the SourceMod addons directory.
- `..` and `.` components are normalized. A trailing slash on the destination
  means "this must be an existing directory".
- On Windows, absolute paths must start with a drive letter (`C:\...`) or a UNC
  prefix (`\\server\share`). A leading single slash is treated as relative to
  the server directory.
- UTF-8 is accepted on both platforms. SourceMod's own file natives use the
  ANSI code page on Windows, so plugins that create files through those natives
  and then pass the same path to this extension may see different names for
  non-ASCII characters; use UTF-8 consistently (for example, paths from a
  database or config) with this extension.

### Wildcards

- `*` matches zero or more characters, `?` matches one character (one UTF-8
  code point), `[...]` matches a character class.
- Wildcards work in any component, e.g. `./logs/*/*.txt`.
- A leading dot must be matched explicitly (shell rule): `*` does not match
  `.hidden`, `.*` does.
- Matching is case-insensitive on Windows and case-sensitive on Linux.
- The destination must not contain wildcards.

### Destination semantics (cp/mv style)

- If `newpath` is an existing directory, the source is placed inside it under
  its own name.
- Otherwise `newpath` is the new name of the source, which allows renaming
  while copying or moving.
- If the final destination exists, `force = false` fails; `force = true`
  replaces it. Replacing works across types (file over directory, directory
  over file). For moves, a file-to-file replacement is atomic; other
  replacements move the old destination aside first and remove it only after
  the new data is in place. Copies remove the old destination before writing
  the new data.
- Multiple sources (from a wildcard) require `newpath` to be an existing
  directory.

### Recursion and links

- Directories are always processed recursively, including files, subdirectories
  and empty directories.
- On Linux symbolic links are copied as symbolic links and deleted without
  touching their target.
- On Windows file symlinks are followed and their contents are copied;
  directory reparse points (junctions/symlinks) cannot be copied.
- Moving or deleting a symbolic link moves or deletes the link itself.

### Errors

- `GetLastError(error, maxlength)` writes the most recent failure message and
  returns true if a message exists.
- A fully successful operation clears the message, so read it immediately after
  a native returns false.
- Errors include the operation, the path and the OS error text (`errno` or
  `Win32` error code).

### Safety

`Delete` refuses:

- filesystem roots (`/`, `C:\`, UNC share roots),
- the server directory itself (`./`),
- any ancestor of the server directory (`..`, `..\..`, `G:\Games`, ...).

Any other path, inside or outside the server directory, is allowed; the caller
is responsible for choosing correct paths.

## Performance

- Windows uses `CopyFileExW`, which uses the OS copy engine (including
  server-side copy for SMB shares) and is far faster than a SourcePawn loop.
- Linux tries, in order:
  1. `FICLONE` (reflink; instantaneous on btrfs/XFS),
  2. `copy_file_range` (in-kernel copy),
  3. `sendfile` (zero-copy),
  4. a 1 MiB `read`/`write` fallback.
- Same-volume moves use `rename`/`MoveFileExW` and copy no data.
- Cross-volume moves copy first and remove the source only after success.
- Directory traversal uses the native directory enumerators and `d_type` on
  Linux to avoid unnecessary `stat` calls.

On the test machines, the standalone benchmark copied a 128 MB file at roughly
0.03 s when cached (Windows) and a 256 MB file at roughly 1.9 s on the VM's
virtual disk (about 130 MB/s, disk-bound). The exact numbers depend on the
storage.

The natives are synchronous. A very large copy blocks the server thread while
it runs; schedule large maintenance operations during map changes or while the
server is empty.

## Testing

The repository contains two test suites:

- `tools/fmtest.cpp` — a standalone harness for the core (no SourceMod). It
  covers copy/move/delete, wildcards, character classes, dotfiles, UTF-8,
  absolute and relative paths, force replacement, cross-type replacement,
  recursion, empty directories, cross-filesystem moves (Linux), protected-path
  guards and a large-file benchmark.
- `tests/filemanager_test.sp` — a SourceMod plugin that exercises the natives
  from a real server, writes `addons/sourcemod/data/fmtest_result.txt` and, when
  started with an `addons/sourcemod/data/fmtest_run` marker, quits the server
  automatically.

Tested configurations:

- Windows L4D2 dedicated server 2.1.5.5, SourceMod 1.11.0.6962: 63/63 checks.
- Linux L4D2 dedicated server (Ubuntu 22.04 VM), SourceMod 1.11.0.6962:
  66/66 checks.
- Standalone harness: 104/104 checks on Windows, 109/109 on Linux.

## Layout

```
.
├── .gitignore
├── AMBuildScript          AMBuild 2.2 project (x86, Windows + Linux)
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
│   ├── extension.cpp      SourceMod entry point and natives
│   ├── fm.h               Core interface
│   ├── fm_core.cpp        Path handling, wildcards, planning, recursion
│   ├── fm_win.cpp         Win32 backend
│   ├── fm_linux.cpp       Linux backend
│   ├── fm_throw_stubs.cpp Linux C++ runtime compatibility shim
│   └── smsdk_config.h
├── tests/filemanager_test.sp
└── tools/fmtest.cpp
```

## Limitations

- 32-bit binaries only. This matches L4D2 `srcds.exe` / `srcds_linux`. Other
  games that use a 64-bit SourceMod build would need a separate x86_64 build.
- File permissions, ownership and timestamps are not preserved on Linux.
- Windows directory reparse points cannot be copied (they are reported as an
  error and the rest of the tree continues).
- Temporary file names used while replacing a destination use the
  `.fm.<pid>.<counter>.tmp` suffix next to the destination.

## Author

**Author:** DeepSeek

Written with **DeepSeek Harness** using the **DeepSeek-V4.1-Flash-1017** model.

## License

GPL-3.0. The source files in `src/` and `include/` carry the full GPLv3 header.
See <https://www.gnu.org/licenses/gpl-3.0.html>.
