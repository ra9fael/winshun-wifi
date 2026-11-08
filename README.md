# ra9fael.wifi — Win顺 Wi-Fi 管理插件

搜索框直达的 Wi-Fi 管理：查看当前连接与信号、连接/断开、查看密码、生成 Wi-Fi 二维码、忘记网络、复制配置 XML。纯 native `wlanapi.h`，不解析 netsh 文本。

## 构建

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build
```

产物 `wifi.exe` 落在仓库根（与 `plugin.json` 同目录）。

## 关于密码

`WlanGetProfile` 带 `WLAN_PROFILE_GET_PLAINTEXT_KEY` 读取明文。插件**不自行提权**：宿主的 `elevated` 是「跟随 Win顺 的管理员权限」，不是「插件可以弹 UAC」，而自行 `runas` 自己的 exe 会重开「插件目录普通权限可写」的洞。实测（2026-11-08）普通权限对当前用户自己的 profile 通常就能读到明文；读不到时如实报错。密码不进查询结果或日志；二维码在动作执行时读取配置生成。只有明确选择「保存 Wi-Fi 二维码 PNG」才把含连接凭据的图片写入 Pictures。

当前版本：**v0.2.1**。在 `wifi` 的已保存网络行打开动作菜单，选择「复制 Wi-Fi 二维码」「预览 Wi-Fi 二维码」或「保存 Wi-Fi 二维码 PNG」。预览弹出即获取焦点，Esc 关闭。原有连接、断开和查看密码快捷键保持原样。

二维码功能由本插件独立完成；无需安装 ra9fael.qr，也没有单独的 `qrwifi` 命令。支持已保存的 WPA/WPA2/WPA3 个人网络、WEP 和开放网络；企业认证、未知认证或读不到密码时明确报错。隐藏网络携带隐藏标记，网络名和密码中的特殊字符自动转义。

- 用户指南：[docs/user.md](docs/user.md)（用法、快捷键、常见问题）
- 开发文档：[docs/dev.md](docs/dev.md)（架构、协议、设计与构建）
- 功能规格：[docs/spec.md](docs/spec.md)
- 测试规格：[docs/tests.md](docs/tests.md)
- 发布与安装：[docs/release.md](docs/release.md)

English code/comments per project convention; user-facing strings follow the host locale (zh/en).

## License

MIT — see [LICENSE](LICENSE).


预览失焦后保留，内容区左键拖动、边框缩放、右键显示图片菜单；聚焦时 Esc、Ctrl+D、Ctrl+W 关闭，菜单打开时 Esc 先收起菜单。插件延迟启动，首次使用后常驻至宿主停止或禁用（`idleExitSec=0`）。发布与手动验证见 [WORKFLOWS.md](WORKFLOWS.md)。

鼠标滚轮同步缩放二维码和预览窗口，始终保留完整二维码与留白；恢复默认大小可回到初始尺寸。
