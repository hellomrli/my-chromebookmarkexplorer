# Chrome Bookmark Explorer

Chrome Bookmark Explorer 是 Windows 上的 Chrome 收藏夹管理工具，用文件夹树整理书签，并批量检查网址是否可访问。

[下载 Windows 包](https://github.com/hellomrli/my-chromebookmarkexplorer/releases) · [操作文档](docs/index.md) · [构建状态](https://github.com/hellomrli/my-chromebookmarkexplorer/actions/workflows/windows-build.yml)

## 打开并整理收藏夹

1. 在 Releases 选择 Windows x64 压缩包，完整解压后运行 `ChromeBookmarkExplorer.exe`，保留随包的 Qt 依赖。
2. 选择检测到的 Chrome Profile，或用“打开文件”选择自己的 `Bookmarks`。
3. 在左侧选择文件夹，在右侧新建、重命名、编辑、移动或勾选多个书签。
4. 关闭 Chrome 后点击“保存”。可先用“另存为”保存到副本，确认结果后再处理正式文件。

常见文件位置：

```text
%LOCALAPPDATA%\Google\Chrome\User Data\Default\Bookmarks
```

窗口标题中的 `*` 表示有未保存改动；退出或切换文件时会提示保存。保存前尝试生成时间戳备份，操作重要收藏夹前仍应自行保存副本。

## 管理书签

- 文件夹树和列表都支持勾选及右键菜单；双击文件夹进入，双击书签用默认浏览器打开。
- “查重”按规范化网址分组，显示位置和添加时间，便于人工决定保留项。
- “测活”显示 HTTP 状态、耗时和错误；删除确认只默认勾选明确的 404/410 或无效网址。
- 支持批量替换网址、编辑标签，导出 HTML、JSON、CSV，以及导入 JSON。

HTML 导入尚未实现。超时、证书错误或需要登录不能直接判定为死链；处理方式见 [网址检查指南](docs/health-checks.md)。

## 查阅操作和开发说明

- [编辑、导入导出与备份恢复](docs/bookmarks.md)
- [查重、测活和异常链接处理](docs/health-checks.md)
- [Windows 构建、测试和打包](docs/building.md)

版本和可下载文件以 [Releases](https://github.com/hellomrli/my-chromebookmarkexplorer/releases) 为准。CMake 中的项目版本与预发布标签可能不同。

## 反馈问题

在 [Issues](https://github.com/hellomrli/my-chromebookmarkexplorer/issues) 提供版本、Windows/Chrome 版本和复现步骤。使用脱敏的书签样本，移除私人网址及账户信息。

## 许可信息

仓库尚未提供根目录 LICENSE；使用或再分发前需向维护者确认授权范围。
