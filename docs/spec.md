# ra9fael.wifi 功能规格（v0.1.0）

## v0.2.0 二维码扩展

`wifi` 已保存网络新增 copyQr、previewQr、saveQr。使用实际 WLAN profile 名与网卡定位配置，动作时重读明文；查询结果只携带网络和配置身份。`wifi_qr.h` 校验认证、转义 WIFI 载荷并渲染，`qrcode.h` 为从 ra9fael.qr 复用的 MIT 编码器，`qr_image.h` 使用 Win32/GDI+ 实现剪贴板、PNG 和独立消息线程预览，无 Qt 依赖。

预览显示后调用 SetForegroundWindow、SetActiveWindow、SetFocus；重复打开先销毁旧窗口。宿主在用户动作前通过 AllowSetForegroundWindow 授权插件激活。新权限为 ui 与 Pictures 写入，保留 elevated 和 clipboard:write。只有 saveQr 会写含连接凭据的 PNG。

`wifi_tests` 覆盖特殊字符、中文、隐藏网络、WEP、WPA3、企业认证、受保护或缺失密码，以及配置名与 SSID 不同的合并行为。`wifi_qr_native_tests` 使用虚构数据覆盖动作和 PNG，`--ui` 额外验证复制图片、连续预览的前台/键盘焦点、Esc 关闭。测试不改变真实网络连接。发布验证见 [validation-0.2.0.md](validation-0.2.0.md)。


状态：**已实施（2026-11-08）**，L1 单测与协议往返实测通过；真实无线网卡的手动 E2E 见 docs/tests.md §3。协议与清单字段以 WinShun 0.22.0 插件文档为准（resident JSON-RPC over stdio）。

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
  - `WlanGetProfile` 带 `WLAN_PROFILE_GET_PLAINTEXT_KEY`
  - **不自行提权**（见下「提权裁决」）：直接请求明文，按返回结果判断。拿不到明文（返回仍是 `protected` 的 blob 或 `ERROR_ACCESS_DENIED`）→ 如实报错，不给假成功
  - 明文只在内存里（`m_revealed`，按配置名），供本会话的行显示与剪贴板；**不落盘**
  - 列表中默认打码显示（`showPasswordMasked`），`copyPassword` 直接复制明文
- `forget`：`WlanDeleteProfile`；危险操作，两段确认（见 §4.2）
- `copyXml`：`WlanGetProfile` 返回的 XML 复制到剪贴板；keyMaterial 仍为 `protected` 时在结果行注明「密码段需管理员」

### 4.2 二级列表与回退（实施时查宿主源码后确定）

宿主的 `replace` effect 是「原地换掉整个列表」（`plugin/Merge.cpp:323` 的 `m_sink.assign(...)`，顺序照插件给的），**没有历史栈，没有面包屑，没有「返回上一级」**。Esc 在 `app/qml/Main.qml:844-852` 的逻辑是：有选中 → 清选中；否则 `clearToKeyword()`；否则关窗口。而 `clearToKeyword()`（`app/Launcher.cpp:5529`）只在关键字后面**有词**时才生效。

所以：**如果确认行是 `replace` 出来的，Esc 直接关窗口，回不到列表。** 而且键盘落点不可控——插件行没有 `path`，`followRow` 第一句 `currentPath.isEmpty()` 就返回 Stay（`app/LauncherNavigation.h:44`），键盘停在原索引；越界时落到最后一行（`app/Launcher.cpp:5934`）。对删除网络这种不可逆操作，这意味着「从第 2 行按两次 Enter 就删了」。

**因此 forget 的确认态挂在输入框上，不用 `replace`：**

1. 第一按 Enter → 返回 `setQuery("forget <ssid>")`
2. 宿主带这些词重新查询，`m_replacePending = true`（`app/Launcher.cpp:1885`，注释即「new rows: the first is selected」），且 `update` 为假所以不调 `followCurrentRow`（`:5844`、`:5892`）→ **新列表第一行必被选中**
3. 该查询只返回一行确认行（`forgetConfirmItem`），Enter 才真删
4. **Esc 清掉 `forget <ssid>` 回到 `wifi ` 空查询列表** —— 真正的回退
5. 删除成功后返回 `setQuery("")` 回到普通列表

`forgetTarget()` 大小写敏感地匹配 `forget ` 前缀，所以搜 `Forget HJJ` 仍是搜索；SSID 本身叫 `forget me` 也能通过 `forget forget me` 到达。单测覆盖了这些边界（`tests/test_wifi.cpp` 第 12 组）。

**代价**：确认时输入框里多出 `forget HJJ` 这段字。这是换取「焦点确定 + Esc 真回退」的代价，值得。

### 4.1 提权裁决（实施时实测，与初版规格不同）

初版规格写「无提权 → 触发 UAC 一次性提权子进程（同一 exe 以 runas 重启，helper 模式把明文经 stdout 传回）」。实施时否决，理由两条：

1. **与宿主权限模型冲突**：宿主的 `elevated` 含义是「Win顺 以管理员身份运行时插件也以管理员身份运行」，并且宿主**从不**以管理员身份启动插件文件夹里的程序（该文件夹普通权限可写，任何普通程序都能先换掉 exe）。插件自己 `ShellExecuteExW("runas")` 自己的 exe，等于把这个洞重新打开一遍。宿主的 `elevated` 不是「插件可以弹 UAC」。
2. **实测不需要**：2026-11-08 在本机实测（非管理员进程，`TokenIsElevated=0`），`WlanGetProfile` 带 `WLAN_PROFILE_GET_PLAINTEXT_KEY` **直接返回了明文**（`protected=false`，8 字节口令）；不带该标志时返回 460 字节的加密 blob。也就是说对当前用户自己的 profile，普通权限通常就能读到明文。

因此清单**不声明** `processes`（不需要起子进程），保留 `elevated` 与 `clipboard:write`，并为二维码预览/保存增加 `ui` 与 Pictures 写入。以管理员身份运行 Win顺 时插件跟随提权，行为不变；普通权限下若系统拒绝返回明文，则如实报错。

**未验证**：域环境、组策略下发 profile、其他用户创建的 profile 在普通权限下是否同样返回明文。这些情况下插件会走「如实报错」路径，不会假装成功。

## 5 行为细节

- `WlanOpenHandle` 每次查询创建、用完关闭，resident 常驻不长期占句柄
- 连接是异步过程：发起后返回「已发起连接」行，状态以再次 `wifi` 查询为准（v0.1 不接 `WlanRegisterNotification` 推送）
- 多接口（USB 网卡）全部列出，行内标接口名
- 隐藏网络：`includeHidden` 控制是否列出（来自 scan list 的 hidden 标志）
- 无线网卡不存在 / WlanSvc 未启动 → 提示行 `valid:false`
- 纯 Win32 + CRT 的 wifi.exe，无 Qt 依赖；二维码预览使用原生窗口

## 6 权限

- `elevated`：跟随 Win顺 的管理员权限（宿主语义，不是「插件能弹 UAC」）；明文密码读取在普通权限下通常也能成功，见 §4.1
- `clipboard:write`：copyPassword / copyXml / copyQr
- `ui`：Wi-Fi 二维码预览
- `files:write`（Pictures）：用户主动保存二维码 PNG
- 不做 `network`、`processes`（不起子进程）

## 7 数据位置

密码不进入日志或 WS_DATA_DIR；查看和复制只使用内存与剪贴板。只有用户主动执行 saveQr 时写出包含连接凭据的 PNG。

## 8 失败处理

- 拿不到明文密码 → 行内报错，不给假成功
- `WlanConnect` 失败（profile 无效/信号弱）→ 返回错误码对应含义的提示行
- 组策略下发的 profile 删除失败 → 提示「可能由组策略管理」
- 接口被禁用 / 虚拟 AP 模式 / 电台关闭 → 行内标注（`WLAN 电台已关闭`）
- 密码含特殊字符不受影响（XML 原文处理，无转义拼接）

## 9 里程碑

- v0.1：上述全部（已实施；2026-11-08 电台开启后完成真机 E2E，见 docs/tests.md §3.1——扫描、连接/断开、真删 forget 均通过；开放网络一键连、飞行模式/WlanSvc、多网卡、en locale 四项环境受限待测）
- v0.2 候选：新建连接流程（密码输入）、连接状态推送（WlanRegisterNotification）、信号历史、首查扫描等待或缓存上次扫描结果（tests.md §3.1.14）
