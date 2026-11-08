# 更新日志

[English](CHANGELOG.en.md)

本插件所有值得注意的变化。格式遵循
[Keep a Changelog](https://keepachangelog.com/zh-CN/1.1.0/)，版本遵循
[Semantic Versioning](https://semver.org/lang/zh-CN/)。`plugin.json` 里的
`version` 是唯一事实来源；发版 tag 是 `v<version>`，Release 由 CI 在 tag
提交上构建发布。

## [Unreleased]

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
