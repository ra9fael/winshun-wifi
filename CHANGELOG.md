# 更新日志

[English](CHANGELOG.en.md)

本插件所有值得注意的变化。格式遵循
[Keep a Changelog](https://keepachangelog.com/zh-CN/1.1.0/)，版本遵循
[Semantic Versioning](https://semver.org/lang/zh-CN/)。`plugin.json` 里的
`version` 是唯一事实来源；发版 tag 是 `v<version>`，Release 由 CI 在 tag
提交上构建发布。

## [Unreleased]

## [0.2.1] - 2026-11-09

- QR 与 Wi-Fi 使用一致的原生预览窗口：正确插件图标、单行超长截断说明、标题栏置顶开关，默认不置顶；图片操作收进右键菜单。
- 鼠标滚轮同步缩放图片和窗口，在屏幕工作区内完整显示二维码。
- 二维码预览打开即获取焦点，失焦后保留；支持边框缩放及内容区左键拖动。
- 聚焦时 Esc、Ctrl+D、Ctrl+W 关闭；右键提供复制图片、保存 PNG、恢复默认大小和关闭。
- 缩放保持清晰模块和留白，菜单操作使用当前图片快照；停用五分钟空闲回收，避免预览意外关闭。
- 构建、测试和发布仅由新版本 tag 或手动运行触发，手动默认只验证。

## [0.2.0] - 2026-11-09

- 已保存网络的 wifi 动作菜单新增二维码复制、预览和 PNG 保存，无需安装二维码插件。
- 沿用 elevated 权限，动作时重读实际 WLAN 配置名对应的密码；支持隐藏网络、特殊字符、WEP、开放网络与 WPA 个人认证，企业网络和读不到密码时明确拒绝。
- 原生预览弹出即获取前台和键盘焦点，Esc 关闭；保存 PNG 不覆盖已有文件，复制/预览不落盘。
- 新增 ui 与 Pictures 写入权限；保留连接/断开/查看密码快捷键。

## [0.1.2] - 2026-11-09

### Fixed

- 修复可执行文件图标，补齐打包中的 ICO 和资源脚本。

### Notes

- 本次仅生成本地标签与安装包。实际验证范围见 docs/icon-validation.md；未进行 GitHub 发布。更新后需重新启动插件进程。


## [0.1.1] - 2026-11-08

### Added

- 用户指南（docs/user.md）与开发文档（docs/dev.md），含界面示意图与
  mermaid 流程图；README 挂全部文档索引。

### Changed

- 构建清理：移除未使用的 `shell32` 链接（提权裁决后代码里已无
  `ShellExecuteExW` 调用，只剩 wlanapi/ole32/user32）。
- 发版基础设施：CI 工作流（ci.yml + release.yml，tag 推送即构建、单测并
  公开发布）与 tools/package.py。

### Validated

- WLAN 电台开启后的真机 E2E：扫描与 IE 安全判定、连接/断开往返、
  forget 真删除（netsh 交叉核对）、密码零落盘复查——见 docs/tests.md §3.1。

## [0.1.0] - 2026-11-08

### Added

- 首版：`wifi` / `无线` 关键词，三段列表（当前连接、已保存、附近可见），
  连接/断开、查看与复制密码、复制配置 XML、忘记网络（两段确认）。
- 纯 `wlanapi` 结构化数据，不解析 netsh；密码只在内存与剪贴板，不落盘。
