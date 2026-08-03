# libonion_activation — 内测 Ed25519 证书激活（可插拔）

> **临时模块**：仅用于内测证书激活，正式发布前应整体移除。  
> 实现参考 kylin-core 标准激活（Ed25519 签名的 `license.json`）。**无短码**。

## 设计原则

| 要求 | 做法 |
|------|------|
| 可插拔 | 独立目录 `source/libonion_activation/` |
| 易关闭 | CMake `ONION_ENABLE_BETA_ACTIVATION`（默认 `ON`） |
| 易删除 | `OFF` 后删本目录 + 去掉 daemon 两处开关 |
| 与主逻辑隔离 | daemon 仅 `onion_activation_gate()` 一处接入 |

## 开关

```bash
# 开启（默认）时必须提供验签公钥（仅编译期注入，源码无默认值）
export ONION_ACTIVATION_PUBLIC_KEY_HEX='<64 hex chars>'
cmake ... -DONION_ENABLE_BETA_ACTIVATION=ON
# 或显式：
# cmake ... -DONION_ACTIVATION_PUBLIC_KEY_HEX=<64hex>

# 关闭
cmake ... -DONION_ENABLE_BETA_ACTIVATION=OFF
```

`scripts/build.sh` 会把环境变量 `ONION_ACTIVATION_PUBLIC_KEY_HEX` 传给 CMake。  
未设置时配置阶段直接 **FATAL_ERROR**。

## 证书格式

```json
{
  "version": 1,
  "licenseId": "OHN-...",
  "subject": "beta",
  "deviceId": "sha256:...",
  "features": ["*"],
  "issuedAt": 1700000000,
  "expiresAt": 0,
  "nonce": "random",
  "signature": "<ed25519 hex>"
}
```

签名载荷（与 kylin-core 相同的规范字符串）：

```text
version=1
licenseId=...
subject=...
deviceId=...
features=*
issuedAt=...
expiresAt=...
nonce=...
```

## 签发

```bash
pip install cryptography   # once

# 用你自己的 32 字节 seed（保密）；工具会打印对应 public_key_hex
./source/libonion_activation/scripts/issue_activation.py \
  --device-id 'sha256:完整64位hex...' \
  --seed-hex "$YOUR_SEED_HEX" \
  --out license.json

# 构建 payload 时必须 export 同一把公钥：
export ONION_ACTIVATION_PUBLIC_KEY_HEX='<tool printed public_key_hex>'
```

设备 ID 可从 toast / 日志 / TCP `STATUS` 获取：

```bash
printf 'STATUS\n' | nc <ps5-ip> 9099
```

## 安装到主机

任选其一：

```bash
# 文件
# /data/OnionHEN/activation/license.json   （主路径）
# /mnt/usb0/onion_license.json
# /data/OnionHEN/onion_license.json

# TCP
printf 'LICENSE %s\n' "$(cat license.json)" | nc <ps5-ip> 9099
```

## 设备 ID

```text
device_id = "sha256:" + hex(SHA256("onionhen-activation-v1" || hardware_serial))
```

优先 `sceKernelGetHwSerialNumber`，失败回退 OpenPsId。  
与 kylin-core 命名空间不同，**两边 license 不可互换**。

## 后续移除

1. `-DONION_ENABLE_BETA_ACTIVATION=OFF` 或删 root option
2. 删除 `source/libonion_activation/`
3. 还原 `daemon/CMakeLists.txt` 与 `daemon/source/main.cpp` 中的 `#if` 块
4. （可选）清理设备 `/data/OnionHEN/activation/`

## 通知与 i18n

用户可见文案走平台封装，**不**手写 toast 结构：

- `onion_notify_debug(english_fmt, ...)` — 无图标 debug toast
- 英文源串在 `source/i18n/{en-US,zh-CN}.json` 的 `notifications` 段
- daemon 在 gate 前 `LoadSettings()`，以便 `onion_notify_apply_ui_language` 生效

## 公开 API

见 [`include/onion/activation.h`](include/onion/activation.h)。
