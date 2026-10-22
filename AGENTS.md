# GameV5 构建指南

## 环境依赖

- Windows 10+
- Visual Studio 2022（Community 或 Professional）
- Clang-cl（随 VS 安装的 LLVM 工具链）
- CMake 3.10+
- Ninja
- DUIX（程序使用的UI库）具体位置读系统环境变量：DUIX 找到位置后里面有docs文档说明

## 目录结
```
src/           CMake 源码目录（顶层 CMakeLists.txt）
bin/           编译输出 + 运行时资源（skin 目录）
docs/          项目知识库文档目录（目录页 README.md）
```

**本文件只保留构建、环境约束**，勿再往此处堆属性清单；产品说明放 `docs/`，勿把知识库长文写进本文件。

## 换行符（硬约束，禁止再改）

- **本仓库固定 Windows / PC 换行：CRLF（`\r\n`）**。禁止改成 LF（Unix）或混用。
- Agent **不得** 因「规范化」「editorconfig 校准」「git autocrlf」「保存时统一」等理由把已有文件从 CRLF 改成 LF，或从 LF 再改回 CRLF 来回折腾；**只改业务内容，不动换行风格**。
- 新建或整文件重写时：必须写出 **CRLF**。若工具默认产出 LF，写完后须转成 CRLF 再结束。
- 若 diff 里只出现 `^M` / 整文件换行差异：视为错误，应还原为 CRLF，**禁止单独提交换行变更**。
- 根目录 `.editorconfig`：`end_of_line = crlf`；根目录 `.gitattributes`：文本文件 `eol=crlf`。二者与本条一致，勿删、勿改成 `lf`。

## 皮肤 HTML（硬约束）

- **仅修改 `bin/skin/` 下 HTML（或同目录皮肤资源）时，禁止编译工程**；改完直接运行 exe（需重启进程）即可看效果。
- DUIX `LoadResourceData`：**磁盘 `skin` 目录优先**，找不到再回退嵌入 ZIP（Debug/Release 相同）；因此只要 `bin/skin/GameV5/` 存在就会读文件。
- 修改 `.cpp` / `.h` / `.rc` / `CMakeLists.txt` 等源码后再执行 `build_clang_ninja_debug.bat`。



## 首次生成工程

默认使用 **Debug**：

```bat
build_clang_ninja_debug_init.bat
```

Release：

```bat
build_clang_ninja_release_init.bat
```

执行后会在项目根目录生成 `build_clang_ninja_debug` 或 `build_clang_ninja_release` 目录。

## 编译

默认增量编译 Debug：

```bat
build_clang_ninja_debug.bat
```

Release：

```bat
build_clang_ninja_release.bat
```

产物输出到 `bin/` 目录：Debug 静态 CRT `/MTd`、后缀 `_mtd`；Release 静态 CRT `/MT`、后缀 `_mt`。

## init_env.bat

自动查找 vcvarsall.bat 并初始化 x64 编译环境。默认搜索路径：

1. `C:\Program Files\Microsoft Visual Studio\18\Community`
2. `C:\Program Files\Microsoft Visual Studio\2022\Professional`

如果 VS 安装路径不同，修改 `init_env.bat` 中的路径。

## 编译选项

- 编译器：clang-cl（MSVC 兼容模式）
- 构建系统：Ninja
- C++ 标准：C++17（toml++ 依赖）
- CRT：Debug `/MTd`，Release `/MT`
- 预定义宏：`CMAKE`, `UNICODE`, `_UNICODE`
- **字符集：仅 Unicode**。CMake 已强制 `-DUNICODE -D_UNICODE`；
- 输出目录：`bin/`（exe、dll、lib 统一输出；后缀 `_mtd` / `_mt`）



