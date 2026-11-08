# ra9fael.wifi — Win顺 Wi-Fi 管理插件

搜索框直达的 Wi-Fi 管理：查看当前连接与信号、连接/断开、查看密码、忘记网络、复制配置 XML。纯 native `wlanapi.h`，不解析 netsh 文本。

## 构建

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build
```

产物 `wifi.exe` 落在仓库根（与 `plugin.json` 同目录）。

## 关于密码

`WlanGetProfile` 带 `WLAN_PROFILE_GET_PLAINTEXT_KEY` 读取明文。插件**不自行提权**：宿主的 `elevated` 是「跟随 Win顺 的管理员权限」，不是「插件可以弹 UAC」，而自行 `runas` 自己的 exe 会重开「插件目录普通权限可写」的洞。实测（2026-11-08）普通权限对当前用户自己的 profile 通常就能读到明文；读不到时如实报错。密码只在内存与剪贴板，不落盘、不写日志。

状态：**已实施（2026-11-08）**，v0.1.0。L1 单测 171/171 通过，协议往返实测通过；真实无线网卡的手动 E2E 待做（清单见测试规格 §3）。

- 用户指南：[docs/user.md](docs/user.md)（用法、快捷键、常见问题）
- 开发文档：[docs/dev.md](docs/dev.md)（架构、协议、设计与构建）
- 功能规格：[docs/spec.md](docs/spec.md)
- 测试规格：[docs/tests.md](docs/tests.md)
- 发布与安装：[docs/release.md](docs/release.md)

English code/comments per project convention; user-facing strings follow the host locale (zh/en).

## License

MIT — see [LICENSE](LICENSE).
