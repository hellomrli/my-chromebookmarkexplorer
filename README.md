# Chrome Bookmark Explorer C++

Windows 原生版 Chrome 收藏夹管理工具，使用 **C++17 + Qt6** 编写。界面采用类似资源管理器的文件夹管理视图：左侧收藏夹文件夹树，右侧当前文件夹内容列表，并包含网址测活功能。

![Version](https://img.shields.io/badge/version-0.2.2-blue)
![Qt](https://img.shields.io/badge/Qt-6.x-green)
![C++](https://img.shields.io/badge/C++-17-orange)

## 功能

- 自动识别 Chrome Profile：`Default`、`Profile 1`、`Profile 2`
- 打开任意 Chrome `Bookmarks` 文件
- 文件夹树 + 书签列表视图，带图标显示
- 新建文件夹、新建书签、重命名、编辑网址、删除、移动
- **右侧列表勾选框支持批量操作**
- 左侧文件夹树支持勾选文件夹并批量删除、移动
- 左侧文件夹树和右侧列表支持右键菜单
- 右键菜单支持全选/取消全选勾选框
- 新建、重命名、删除、移动后尽量停留在原文件夹位置
- 退出或切换文件前提示保存未保存更改，窗口标题用 `*` 标记脏状态
- 支持保存、另存为，以及打开/切换文件前的未保存确认
- 重复书签扫描：按规范化后的网址查找重复项并展示名称、路径、添加时间
- 双击文件夹进入该文件夹，双击书签用默认浏览器打开
- 保存前检测 Chrome 是否正在运行
- 保存前自动备份 `Bookmarks`
- 批量网址测活：状态、HTTP 状态码、耗时、错误信息，并可设置测活并发数
- 测活后可处理异常链接：删除前会弹出异常链接清单，默认全选，可取消部分链接后确认删除；也可将异常链接移动到指定文件夹

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

测活使用更接近浏览器导航的轻量 `GET` 请求：收到最终响应头后立即停止下载正文，既避免部分网站错误处理 `HEAD` 导致误报，也不会完整下载网页。检测还会：

- 自动跟随跳转，并兼容浏览器可访问的 HTTPS → HTTP 顶层跳转
- 优先使用系统代理；也保留代码级自定义 HTTP 代理接口
- 对超时、连接中断、部分临时 HTTP 错误重试一次，并在重试时回退到 HTTP/1.1；协议自动切换也会各自获得一次重试
- 对无协议网址优先尝试 HTTPS；旧 HTTP 网址连接失败时尝试 HTTPS
- 全局并发默认 8，同时限制同一主机最多 2 个请求，降低触发限流/WAF 的概率
- 默认超时 15 秒（工具栏可调）；每次检测前会清除本次范围的旧结果，实时检测默认关闭结果缓存，避免临时故障被后续检查继续误报

结果会区分：

- 正常、跳转
- 访问受限、需要登录、访问限流（服务器可达，不计为失效）
- 链接不存在（HTTP 404/410）
- 客户端错误、服务暂时异常
- 超时、域名解析失败、TLS/证书错误、连接失败、重定向异常
- 未检测（非 HTTP）：例如 `chrome://`、`file://` 等浏览器或本地协议，不当作死链

测活完成后，可使用工具栏或右键菜单中的“删除异常链接”“移动异常链接”处理结果。删除确认窗口仅默认勾选明确的 404/410 或无效网址；超时、证书和服务器临时异常需要手动勾选，以避免误删。

## 重复书签扫描

点击工具栏“查重”会扫描当前打开文件中的全部书签，按规范化后的网址分组。扫描结果会列出重复网址、书签名称、所在文件夹和添加时间，便于回到主界面进行删除、移动或重命名。
