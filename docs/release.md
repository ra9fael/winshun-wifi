# ra9fael.wifi 发布与安装流程

## 构建

纯 Win32 + CRT 的 `wifi.exe`（静态链接 CRT），无 Qt 依赖，manifest 不声明 `runtime.qt`。

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

产物落在仓库根（与 plugin.json 同目录）。

## 打包（.wsplugin）

1. 确认 `plugin.json` 的 `version` 与发布 tag 一致
2. `dist/ra9fael.wifi-<ver>.wsplugin` = zip，由 `python tools/package.py --tag v<ver>` 生成，包含 git 跟踪的非隐藏文件（源码、文档、图标、许可证）以及构建的 `wifi.exe`；排除 tools/verify。生成包 SHA-256 与 release-notes.md
3. 计算 SHA-256：`Get-FileHash dist\ra9fael.wifi-<ver>.wsplugin`

## 已出包记录

| 版本 | 日期 | SHA-256（整包） | 备注 |
|---|---|---|---|
| 0.1.0 | 2026-11-08 | —（早于本记录） | 首包，电台关闭期实测 |
| 0.1.1 | 2026-11-08 | `0D6D79D7EC4F65177E6261C503E3B4182C91FBD4D4D9C998559C7A251CBE7B2A` | 电台开启后的完整 E2E（tests.md §3.1）之后重打；包内 wifi.exe 与仓库构建逐字节一致（SHA-256 `1549A3B906A304F281A0E542AD5E7A030DE0BCD4ED811C9D5667D5DD0CEC6C25`） |

## 发布

1. `git tag v<ver> && git push origin main --tags`
2. GitHub Release `v<ver>`：上传 `.wsplugin`；release notes 写变更点、SHA-256、构建命令，并注明：查看密码走 `WlanGetProfile` 明文读取（插件不自行提权，读不到时如实报错），查询和日志不包含密码；只有用户执行保存二维码动作才输出含连接凭据的 PNG
3. 声明为社区原生包（`unreviewed-native`）

## 安装（用户）

- 方式 A（宿主界面）：Win顺 → 插件管理 → 本地安装 → 选择 `.wsplugin` → 确认（unreviewed-native 提示）→ 停用再启用插件（或重启宿主）
- 方式 B（手动）：解包到 `%LOCALAPPDATA%\WinShun\plugins\ra9fael.wifi\`（`plugin.json` 必须在目录根部）→ 重启 Win顺
- 首次启用弹权限确认（提权、剪贴板写入、窗口与 Pictures 写入）；lazy 启动，首次输入 `wifi` 才拉起进程

## 升级

- 覆盖安装新版 `.wsplugin`，或替换目录内文件后停用/启用插件
- 无数据迁移（不持久化）

## 卸载

- 宿主中停用 + 删除，或直接删除插件目录后重启宿主
- 残留：无（不写数据目录、不动系统配置；忘记网络是用户主动操作的结果）

## 回滚

- GitHub Release 保留历史版本；下载旧 `.wsplugin` 覆盖安装即可
