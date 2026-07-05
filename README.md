# Chrome Bookmark Explorer C++

Windows 原生版 Chrome 收藏夹管理工具，使用 **C++17 + Qt6** 编写。界面采用类似资源管理器的文件夹管理视图：左侧收藏夹文件夹树，右侧当前文件夹内容列表，并包含网址测活功能。

## 功能

- 自动识别 Chrome Profile：`Default`、`Profile 1`、`Profile 2`
- 打开任意 Chrome `Bookmarks` 文件
- 文件夹树 + 书签列表视图
- 新建文件夹、新建书签、重命名、删除、移动
- 左侧文件夹树支持勾选文件夹并批量删除、移动
- 左侧文件夹树和右侧列表支持右键菜单
- 新建、重命名、删除、移动后尽量停留在原文件夹位置
- 双击书签用默认浏览器打开
- 保存前检测 Chrome 是否正在运行
- 保存前自动备份 `Bookmarks`
- 批量网址测活：状态、HTTP 状态码、耗时、错误信息，并可设置测活并发数

## Windows 构建

安装：

- Visual Studio 2022 Build Tools 或 Visual Studio 2022
- CMake 3.21+
- Qt 6.x MSVC 64-bit

PowerShell：

```powershell
cd C:\path\to\chrome-bookmark-explorer-cpp
.\build_windows.ps1 -QtDir "C:\Qt\6.7.3\msvc2019_64"
```

构建产物：

```text
build\Release\ChromeBookmarkExplorer.exe
dist\ChromeBookmarkExplorer\
```

`dist\ChromeBookmarkExplorer\` 是用 `windeployqt` 收集依赖后的可分发目录。

## GitHub Actions 自动打包

项目已包含：

```text
.github/workflows/windows-build.yml
```

推送到 GitHub 后，在 Actions 里手动运行 `Windows Build`，会生成 artifact：

```text
ChromeBookmarkExplorer-windows-x64.zip
```

解压后里面就是 `ChromeBookmarkExplorer.exe` 和 Qt 运行依赖。

## 打包安装包

安装 Inno Setup 后：

```powershell
iscc packaging\installer.iss
```

生成的安装包在：

```text
packaging\Output\
```

## Chrome Bookmarks 位置

通常在：

```text
%LOCALAPPDATA%\Google\Chrome\User Data\Default\Bookmarks
```

本工具保存时会生成类似：

```text
Bookmarks.backup-20260705-190000
```

并移除旧 `checksum` 字段，让 Chrome 下次启动自动重建校验。

## 网址测活

工具会先发起 `HEAD` 请求；遇到不支持 `HEAD` 的站点，会自动回退到轻量 `GET` 请求。工具栏的“并发数”用于控制同时检测的网址数量，默认 8，最高 128。结果分类：

- 正常
- 跳转
- 客户端错误
- 服务端错误
- 超时
- 连接失败
