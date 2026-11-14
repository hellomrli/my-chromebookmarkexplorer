# 在 Windows 构建和打包

[返回文档入口](index.md)

## 准备依赖并构建

需要 Visual Studio 2022 或 Build Tools、CMake 3.21+ 和 Qt 6 MSVC 64 位版本。工作流使用 Qt 6.7.3。

在 PowerShell 中执行，Qt 路径按本机安装位置调整：

```powershell
git clone https://github.com/hellomrli/my-chromebookmarkexplorer.git
cd my-chromebookmarkexplorer
.\build_windows.ps1 -QtDir "C:\Qt\6.7.3\msvc2019_64"
```

产物为 `build\Release\ChromeBookmarkExplorer.exe`，`windeployqt` 收集依赖后的分发目录为 `dist\ChromeBookmarkExplorer\`。应用使用 C++17，链接 Qt Widgets 和 Network；开启测试时还需要 Qt Test。

## 运行现有测试

```powershell
ctest --test-dir build -C Release --output-on-failure
```

测试包含网络行为，结果受网络环境影响。现有 Windows 工作流将网络测试失败记录为警告后继续打包，成功生成安装包不能代替测试结论。

## 生成分发文件

[Windows Build](../.github/workflows/windows-build.yml) 支持 main 推送和手动触发，生成 `ChromeBookmarkExplorer-windows-x64.zip` artifact。

当前工作流在 main 构建时按 CMake 版本发布 Release；同名 Release 存在时会改写说明并覆盖 ZIP。维护文档时需避免由普通文档提交触发旧版本二进制重发布。

安装 Inno Setup 后，先完成 `dist` 的依赖部署，再执行：

```powershell
iscc packaging\installer.iss
```

安装包输出在 `packaging\Output\`。不要单独分发缺少 Qt DLL 的 EXE。
