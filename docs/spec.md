# ra9fael.wifi 功能规格（v0.1.0）

状态：设计完成，代码未实现。协议与清单字段以 WinShun 0.22.0 插件文档为准（resident JSON-RPC over stdio）。

## 1 目标

- 搜索框直达的 Wi-Fi 管理：看状态、连接/断开、查密码、忘网络
- 全结构化数据（native wlanapi），零 shell 解析

## 2 非目标

- 不做新建配置/输密码连新加密网（引导去系统设置，不做输入 UI）
- 不做网卡启停（switches 已有 WLAN 电台开关）
- 不做漫游诊断/信号历史图表（v0.2 候选）
- 不持久化任何密码

## 3 命令（`wifi` / `无线`）

- 留空：三段——① 当前连接行（SSID、BSSID、频段/信道、信号 %、链路速率、认证方式）② 已保存配置（含当前未连接的）③ 附近可见网络（按信号排序）
- 输入：按 SSID 前缀/子串过滤（不区分大小写）；当前连接行始终保留
- 每行标注：`connected` / `saved` / `visible` / `open` / `WPA2|WPA3|WEP`

## 4 动作

- `Enter connect`：已保存 profile → `WlanConnect`；可见开放网络 → 临时连接；可见加密网络且无 profile → `valid:false` 提示行「请在系统设置输入密码」（不做 UI）
- `Ctrl+Enter disconnect`：当前连接的接口 `WlanDisconnect`
- `Shift+Enter password` / `copyPassword`：
  - `WlanGetProfile` 带 `WLAN_PROFILE_GET_PLAINTEXT_KEY`，需要管理员
  - 无提权 → 触发 UAC 一次性提权子进程（同一 exe 以 runas 重启，helper 模式把明文经 stdout 传回，**不落盘**）；用户拒绝 UAC → 如实报错
  - 列表中默认打码显示（`showPasswordMasked`），`copyPassword` 直接复制明文
- `forget`：`WlanDeleteProfile`；危险操作，行样式 `valid:false` + 宿主确认流程
- `copyXml`：`WlanGetProfile` 返回的 XML 复制到剪贴板；无提权时 keyMaterial 为空并在结果行注明「密码段需管理员」

## 5 行为细节

- `WlanOpenHandle` 每次查询创建、用完关闭，resident 常驻不长期占句柄
- 连接是异步过程：发起后返回「已发起连接」行，状态以再次 `wifi` 查询为准（v0.1 不接 `WlanRegisterNotification` 推送）
- 多接口（USB 网卡）全部列出，行内标接口名
- 隐藏网络：`includeHidden` 控制是否列出（来自 scan list 的 hidden 标志）
- 无线网卡不存在 / WlanSvc 未启动 → 提示行 `valid:false`
- 纯 Win32 + CRT 的 wifi.exe，无 Qt 依赖，无 `ui` 权限

## 6 权限

- `elevated`：明文密码读取；按需 UAC 子进程，不在安装时要求
- `clipboard:write`：copyPassword / copyXml
- 不做 `ui`、`network`、`processes`

## 7 数据位置

不持久化；密码只存在于内存与剪贴板（copyPassword 时），不写日志、不写 `WS_DATA_DIR`。

## 8 失败处理

- 拒绝提权 → 行内报错，不给假成功
- `WlanConnect` 失败（profile 无效/信号弱）→ 返回错误码对应含义的提示行
- 组策略下发的 profile 删除失败 → 提示「可能由组策略管理」
- 接口被禁用 / 虚拟 AP 模式 → 行内标注
- 密码含特殊字符不受影响（XML 原文处理，无转义拼接）

## 9 里程碑

- v0.1：上述全部
- v0.2 候选：新建连接流程（密码输入）、连接状态推送（WlanRegisterNotification）、信号历史
