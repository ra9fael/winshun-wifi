# ra9fael.wifi 测试规格

## 1 单元测试（纯逻辑，`tests/test_wifi.exe`，ctest 驱动）

- profile XML 解析：name / auth / encryption / keyMaterial（打码）、WPA2/WPA3/WEP/open 识别、XML 转义字符不破坏解析
- SSID 过滤：大小写不敏感、前缀/子串、空查询
- 行排序：当前连接 → 已保存（未连接）→ 可见网络（按信号降序）；同 SSID 多 BSSID 去重
- RSSI → 信号百分比映射（WiFi.signalQuality 公式边界值）
- 设置解析：非法值回默认
- 提示行选择：无网卡 / 服务未启动 / 无 profile 的加密网络

目标：≥30 用例。

## 2 协议往返（真实进程）

- `initialize → query → action → shutdown` 全链路 JSON-RPC
- 留空 / 带筛选 / 不存在的 SSID / 仅数字输入
- action connect / disconnect / forget / password 各自的 effects 与结果更新
- `timeoutMs` 3000：BSSID 很多（企业环境）时不超时

## 3 手动 E2E（真实无线网卡 + 至少两个已知网络：WPA2 与开放）

- [ ] 留空：当前连接信息与系统设置一致（频段/信道/速率）
- [ ] Enter 连接已保存网络 → 重查询状态变 connected
- [ ] Ctrl+Enter 断开 → 重查询 disconnected
- [ ] Shift+Enter：UAC 弹窗 → 明文显示；拒绝 UAC → 明确报错无假成功
- [ ] copyPassword 粘贴正确；`showPasswordMasked=true` 时列表仍显示 ****
- [ ] forget：确认后 profile 消失（`netsh wlan show profiles` 交叉核对）
- [ ] 可见开放网络一键连上；加密无 profile → 引导提示
- [ ] copyXml：有提权含 keyMaterial，无提权密码段为空且注明
- [ ] 飞行模式开启 → 全部动作报错不崩溃；WlanSvc 停止 → 提示行
- [ ] 密码不出现在任何文件/stderr 日志中（`findstr` 全目录验证）
- [ ] 中英文 locale

## 4 验收标准

- 单测全绿；协议往返通过；E2E 全勾
- 密码零落盘（源码层 grep + E2E 验证双重保证）
