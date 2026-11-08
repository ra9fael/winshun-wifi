# ra9fael.wifi 测试规格与实测记录

## v0.2.0 二维码扩展

`wifi` 已保存网络新增 copyQr、previewQr、saveQr。使用实际 WLAN profile 名与网卡定位配置，动作时重读明文；查询结果只携带网络和配置身份。`wifi_qr.h` 校验认证、转义 WIFI 载荷并渲染，`qrcode.h` 为从 ra9fael.qr 复用的 MIT 编码器，`qr_image.h` 使用 Win32/GDI+ 实现剪贴板、PNG 和独立消息线程预览，无 Qt 依赖。

预览显示后调用 SetForegroundWindow、SetActiveWindow、SetFocus；重复打开先销毁旧窗口。宿主在用户动作前通过 AllowSetForegroundWindow 授权插件激活。新权限为 ui 与 Pictures 写入，保留 elevated 和 clipboard:write。只有 saveQr 会写含连接凭据的 PNG。

`wifi_tests` 覆盖特殊字符、中文、隐藏网络、WEP、WPA3、企业认证、受保护或缺失密码，以及配置名与 SSID 不同的合并行为。`wifi_qr_native_tests` 使用虚构数据覆盖动作和 PNG，`--ui` 额外验证复制图片、连续预览的前台/键盘焦点、Esc 关闭。测试不改变真实网络连接。发布验证见 [validation-0.2.0.md](validation-0.2.0.md)。


日期：2026-11-08 · 关联：[功能规格](spec.md) · 实现：`logic.h` / `native.h` / `main.cpp`

> **2026-11-08 第二轮实测（电台开启）已追加到 §3**：真实扫描、连接/断开、真正的 forget 删除均已在真机跑通，驱动脚本在 `e2e/`。§2 的「本机限制」段落描述的是当天早些时候电台软件关闭时的状态，扫描路径现已由 §3.1 覆盖。

## 1 单元测试（纯逻辑，`tests/test_wifi.cpp`，ctest 驱动）

**实测：190 项断言全通过**（`ctest --test-dir build`，MSVC 14.51 / C++17 / `/W4` 零警告）。

| 组 | 覆盖 |
|---|---|
| XML 文本提取 | 首个标签、自闭合、带命名空间属性、标签前缀不误匹配（`<b>` vs `<bb>`）、空文档 |
| XML 实体 | 五个命名实体、十进制与十六进制字符引用（含 CJK）、裸 `&` 与非法引用原样保留 |
| SSID hex | 正常解码、奇数长度、非 hex 字符、UTF-8 多字节 |
| 认证分类 | open/none/WEP/WPA/WPA2/WPA3/SAE/Enterprise、`useOneX` 提升为 Enterprise、标签文案、空与乱码 |
| profile 解析 | 真实形状 XML（实体化的 SSID、`protected` 的 keyMaterial）、仅 hex 的 SSID、无 sharedKey 的开放网络、非 XML 输入不崩 |
| 密码显示 | 打码 / 明文 / 未知时为空 / 空口令 |
| 信号映射 | `2×(dBm+100)` 边界与钳制、0 dBm 视为未知、2.4/5/6 GHz 频段、信道（含 2484→14、6 GHz 从 1 起）、速率格式化 |
| 过滤 | 大小写不敏感、子串、空查询、空 SSID、CJK |
| 排序 | 已连接 → 已保存 → 仅可见；组内强信号优先；**0 dBm 排在真实信号之后**（实施中修掉的 bug） |
| 合并去重 | 同 SSID 多 BSSID 取最强、profile+扫描合并、已连接条目保留自己的 BSSID/速率/信号、明文密码跨合并保留、不同 SSID 不合并 |
| 提示行 | 无网卡优先于服务未启动、无结果、中英文案 |
| 行文案 | 频段/信道/信号/速率/认证/状态组合、未知项不输出为 0、已保存但不在范围内、打码与明文 |
| 可连接性 | 有 profile / 可见开放网络可连；可见加密无 profile、已连接不可连；拒绝文案 |
| 确认步骤 | `forget <ssid>` 前缀识别、大小写敏感、空 SSID、SSID 含空格与中文、`forget forget me` 可达、`forgetQuery` 往返一致 |
| 设置 | 默认值、显式关闭、类型错误回落默认 |

## 2 协议往返（真实进程，本机实测）

`initialize → query → action → shutdown` 全链路，直接驱动 `wifi.exe` 的 stdio。

> 2026-11-08 更新：本节实测时 WLAN 电台软件关闭（扫描列表为空）；电台开启后的完整扫描路径见 §3.1。

| # | 场景 | 实测结果 |
|---|---|---|
| 2.1 | 留空查询（zh / en） | 3 条已保存 profile，`WPA3` / `open` 认证正确，副标题含「不在范围内 · WLAN 电台已关闭」 |
| 2.2 | 筛选 `imp` | 只剩 `IMP-Wireless` |
| 2.3 | 筛选 `zzzz` | 单条 `valid:false` 提示行「No matching networks」 |
| 2.4 | `forget` 第一次按 | 返回 `setQuery("forget HJJ")`，不删除任何东西 |
| 2.4b | 带 `forget HJJ` 重新查询 | 单行确认行「⚠ Forget HJJ?」，副标题「Enter deletes this profile; Esc goes back」，id `wifi:forgetConfirm:HJJ` |
| 2.4c | 确认查询指向不存在的网络 | 单条 `valid:false`「No saved profile for this network」+「Press Esc to go back」 |
| 2.4d | `forget` 单独输入（无尾空格） | 走普通过滤，不是确认查询 |
| 2.4e | `Forget HJJ`（大写 F） | 走普通过滤，不是确认查询 |
| 2.5 | `password`（普通权限） | 拿到明文并 `copy`，随后 `winshun/refresh`；再查询该行显示 `key ********` |
| 2.6 | `password` + `showPasswordMasked=false` | 该行显示 `key rffz1997` |
| 2.7 | `copyXml` | 复制完整 profile XML；keyMaterial 仍是 `protected` blob 时通知「Profile copied (the key needs an administrator)」 |
| 2.8 | `connect` 可见加密无 profile | 通知「Enter the password in Windows Settings」，不发起连接 |
| 2.9 | `connect` 已连接 | 通知「Already connected」 |
| 2.10 | 密码零落盘 | 插件目录内 `*.log/*.json/*.txt/*.tmp` 全文搜索口令字符串：**无命中** |
| 2.11 | 冷启动耗时 | 10 次实测（含进程创建）：min 46 ms / median 49 ms / max 60 ms（清单 `timeoutMs` 3000） |
| 2.12 | 常驻内存 | 20 次连续查询后工作集 5.9 MB，峰值 5.9 MB（清单 `memoryMB` 64）；`shutdown` 后退出码 0 |

**本机限制（未测）**：本机 WLAN 电台为软件关闭（`netsh wlan show interfaces` → `Software Off`），因此**扫描列表为空**，2.x 全部只覆盖已保存 profile 路径。`WlanGetNetworkBssList` 的 IE 解析（RSN/WPA/SAE 判定）已实现但**没有真实扫描结果验证过**。

## 3 手动 E2E（真实无线网卡 + 至少两个已知网络：WPA2 与开放）

前置：打开 WLAN 电台，装入 Win顺（或直接跑 `wifi.exe` 走协议）。

### 3.1 2026-11-08 真机实测（已完成，脚本驱动）

环境：Intel AX200，已连接 HJJ（5 GHz ch44，WPA2/WPA3 过渡），范围内 17 个可见网络。驱动：`e2e/run_e2e.py`（resident JSON-RPC over stdio，直接驱动 `wifi.exe`）、`e2e/probe_scan.cpp`（绕过协议直接看 `WlanGetNetworkBssList`）。用户在对话中明确同意执行含状态变更的用例；破坏性用例完成后已恢复原状。

| # | 用例 | 结果 |
|---|---|---|
| 3.1.1 | 留空查询与系统状态一致 | ✅ HJJ 行「98% · 2402 Mbps · WPA2 · 已连接」，与 `netsh wlan show interfaces`（99%、ch44、2402 Mbps、WPA2-Personal）一致 |
| 3.1.2 | 可见网络按信号降序、同 SSID 多 BSSID 合并 | ✅ 20 行（26 条 BSS 条目合并），ZTE-NbzPG3 双频取最强；`biecengwang-1` 2.4/5 双 BSSID 合为一行 |
| 3.1.3 | IE 安全判定 | ✅ `eduroam`→WPA2-Enterprise、`DIRECT-*`（cap 0x0431 无 RSN IE）→WPA2、隐藏网络→WEP；与 netsh 的 Authentication 逐个核对一致 |
| 3.1.4 | profile+扫描合并 | ✅ IMP-Wireless 由「open · 已保存 · 不在范围内」变为「open · 已保存」（进范围后不再标范围外），仍是一行 |
| 3.1.5 | 隐藏网络 | ✅ 空 SSID 条目以 BSSID 代 id、标题「（隐藏网络）」，默认列出（includeHidden 默认开） |
| 3.1.6 | Ctrl+Enter 断开 | ✅ 通知「已断开」，重查询 HJJ 变「已保存」，netsh State=disconnected |
| 3.1.7 | Enter 重连已保存网络 | ✅ 通知「已发起连接」，约 10 秒后重查询变「已连接」，netsh State=connected（断开→重连往返两次均成功） |
| 3.1.8 | 连接范围外的已保存网络 | ✅ `WlanConnect` 返回成功、插件如实报「已发起连接」，但连接实际未建立（RFK90PPPPM 不在范围）；最终状态以重查询为准，符合规格 §5 的异步语义，无假成功 |
| 3.1.9 | Shift+Enter password | ✅ 明文复制成功，行内追加「密码 ********」；`showPasswordMasked=false` 时行内显示明文（§2.5/2.6 已测，本轮复测通过） |
| 3.1.10 | copyXml | ✅ 复制完整 profile XML（WPA3SAE + transitionMode + 明文 keyMaterial），与 spec §4.1 的实测一致 |
| 3.1.11 | forget 两段确认 | ✅ 第一按返回 `setQuery("forget HJJ")`；确认查询单行「⚠ 忘记 HJJ？」；确认后通知「已忘记这个网络」，重查询 HJJ 行仍在（扫描可见）但不再「已保存」 |
| 3.1.12 | forget 真删除交叉核对 | ✅ `netsh wlan show profiles` 中 HJJ profile 消失（仅剩 RFK90PPPPM、IMP-Wireless） |
| 3.1.13 | 密码零落盘（运行后） | ✅ 全目录（含 e2e 脚本与临时文件）grep 口令字符串：除测试后已删除的 profile 重建脚本外无命中；插件目录、`WS_DATA_DIR`、stderr 均无 |
| 3.1.14 | 首查扫描为空（已知行为） | ⚠️ 冷进程第一次查询时节流扫描（10 s）尚未返回，可见网络列表为空；第二次查询起完整。宿主 `rerun: 2.0` 会让列表很快自动补全，但值得在 v0.2 考虑首查等待或缓存上次扫描结果 |

**环境受限未测（3.1 之后仍开放）**：
- 可见开放网络一键连（当前范围内没有 open 网络；§2.8 的引导提示路径已由协议往返覆盖）。
- 飞行模式 / WlanSvc 停止 / 多网卡 / 域与组策略 profile。
- forget 后 profile 重建：测试用 WPA2PSK profile 手工重建并成功重连（原 profile 是 WPA3SAE+transitionMode；纯 WPA3SAE 无 v4 transitionMode 块的 profile 会「cipher not found」连不上——这是 Windows profile 语法问题，不是插件缺陷，但说明 forget 是真删，恢复需按 AP 能力重建）。

**遗留清理**：`e2e/` 下的驱动脚本保留（`run_e2e.py`、`case_forget_real.py`、`probe_scan.cpp`），供下次复测；口令字符串未留在任何文件中。

### 3.2 手动 E2E 清单（原始清单，供人工复测）

- [x] 留空：当前连接信息与系统设置一致（频段/信道/速率）（3.1.1）
- [x] 附近可见网络按信号降序列出；同 SSID 多 BSSID 只占一行（3.1.2）
- [x] Enter 连接已保存网络 → 重查询状态变 connected（3.1.7）
- [x] Ctrl+Enter 断开 → 重查询 disconnected（3.1.6）
- [x] Shift+Enter：明文显示/复制；系统拒绝时明确报错无假成功（3.1.9；「系统拒绝」分支仍未实测）
- [ ] copyPassword 粘贴正确；`showPasswordMasked=true` 时列表仍显示 `********`（复制路径已测，粘贴到目标程序未人工核）
- [x] forget：确认后 profile 消失（`netsh wlan show profiles` 交叉核对）（3.1.11/3.1.12）
- [ ] 可见开放网络一键连上；加密无 profile → 引导提示（后半已测 §2.8；开放网络环境受限）
- [x] copyXml：keyMaterial 可读时含明文段，不可读时注明（3.1.10；「不可读」分支 §2.7 已测）
- [ ] 飞行模式开启 → 全部动作报错不崩溃；WlanSvc 停止 → 提示行
- [x] 密码不出现在任何文件/stderr 日志中（`findstr` 全目录验证）（3.1.13）
- [ ] 中英文 locale（§2.1 已覆盖 zh；en 待复测）

## 4 验收标准

- 单测全绿（190/190）；协议往返通过；E2E 全勾
- 密码零落盘（源码层 grep + E2E 验证双重保证）

**2026-11-08 状态**：单测 190/190 ✅；协议往返 ✅；E2E 见 §3.1——除「开放网络一键连」「飞行模式/WlanSvc」「多网卡」「en locale」四项环境受限外全部通过。验收标准还差这四项与 §3.2 剩余的人工核对项。

## 5 没测的（诚实清单）

- ~~**真实扫描结果**~~：**已于 2026-11-08 电台开启后实测**（§3.1.2/3.1.3），26 条 BSS 条目、IE 判定与 netsh 一致。
- **连接/断开**：**已于 2026-11-08 实测**（§3.1.6/3.1.7），断开→重连往返成功。
- **forget 的第二次确认**：**已于 2026-11-08 实测真删**（§3.1.11/3.1.12），profile 从系统消失，测试后已重建并重连。
- **UAC/管理员路径**：本插件不自行提权（见 spec §4.1），因此没有提权流程可测；管理员身份下 `WlanGetProfile` 行为未单独验证。
- **域环境 / 组策略 profile / 其他用户的 profile**：普通权限下是否返回明文未验证。
- **多网卡**：本机只有一个无线网卡，多接口并列未验证。
- **WlanSvc 停止 / 飞行模式**：未构造。
- **可见开放网络的一键连**：本轮范围内没有 open 网络，未实测。
- **en locale 的完整列表渲染**：协议往返 §2.3 覆盖过 en 提示行，完整列表未复测。
