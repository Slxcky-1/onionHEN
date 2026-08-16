# libonion_trial — 内测 Beta 时间门闩（可插拔）

> **临时模块**：仅用于内测包限时，正式发布前应整体移除。  
> 所有人共用同一构建战役窗口（构建日起 14 天）；无按设备签发、无装证流程。

## 语义

| 规则 | 说明 |
|------|------|
| 战役窗口 | 从 **构建配置时刻** 起 **14 天**（`not_before` … `not_after`） |
| 防外传 | 外传包与正版 **同寿**，到期后启动被拒绝 |
| 防改时间 | 本机密封 `trial.state` 记录 `last_seen`，检测时钟回拨 |
| 正式版 | `CMAKE_BUILD_TYPE=Release`（或 `-DONION_ENABLE_BETA_TRIAL=OFF`）不链接本库 |

离线客户端无法绝对防破解（可 patch gate）；目标是普通用户改时间续命无效、过期包失去价值。

## 两层结构

### 1. 编译期 Beta Seal（内置）

CMake 注入：

| 宏 | 含义 | 默认 |
|----|------|------|
| `ONION_BETA_NOT_BEFORE` | 窗口起点 Unix 秒 | configure 时刻 |
| `ONION_BETA_NOT_AFTER` | 窗口终点 | `not_before + 14d` |
| `ONION_BETA_BUILD_ID` | 战役 id（≤31 字符） | `beta-YYYYMMDD` |
| `ONION_BETA_STATE_KEY_HEX` | 状态 HMAC 密钥（64 hex） | 随机并 cache |
| `ONION_BETA_SKEW_SEC` | 允许时钟偏差 | `86400`（1 天） |

```bash
# 可选覆盖；不设则自动生成
export ONION_BETA_BUILD_ID=beta-20260315
export ONION_BETA_NOT_BEFORE=$(date -u +%s)
export ONION_BETA_NOT_AFTER=$(( ONION_BETA_NOT_BEFORE + 14*86400 ))
export ONION_BETA_STATE_KEY_HEX=$(openssl rand -hex 32)

# Debug 默认开启；Release 自动关闭（不链接本库）
cmake ... -DCMAKE_BUILD_TYPE=Debug -DONION_ENABLE_BETA_TRIAL=ON
# 关闭（Debug 手动关，或直接打 Release）：
cmake ... -DONION_ENABLE_BETA_TRIAL=OFF
cmake ... -DCMAKE_BUILD_TYPE=Release
```

### 2. 本机 `trial.state`（加密）

路径：`/data/OnionHEN/trial/trial.state`

**落盘格式**（encrypt-then-MAC，非明文）：

```text
magic[8]="OHNTRLV1" | iv[16] | AES-256-CBC(ciphertext) | HMAC-SHA256[32]
```

- AES/MAC 密钥由 `state_key` + 标签派生（`onion-trial-aes-v1` / `onion-trial-mac-v1`）
- 明文状态内仍含字段级 HMAC；外层加密隐藏 `last_seen` 等

| 明文逻辑字段 | 说明 |
|------|------|
| `build_id` | 必须与当前二进制一致 |
| `device_fp` | 序列号派生指纹（防跨机拷状态） |
| `last_seen` | 上次成功通过 gate 的 wall clock |
| `sticky_expired` | 曾判定过期则置位 |
| 内层 `hmac` | HMAC-SHA256（`state_key`） |

## Gate 流程

```
daemon 启动 → LoadSettings → onion_trial_gate()
  ├─ now < not_before - skew  → 拒绝（时钟过早）
  ├─ now > not_after          → sticky，拒绝
  ├─ sticky_expired           → 拒绝
  ├─ now + skew < last_seen   → 拒绝（回拨）
  └─ 通过 → 更新 last_seen，toast 剩余天数
```

失败时 daemon idle，不进入后续逻辑。

## 公开 API

见 [`include/onion/trial.h`](include/onion/trial.h)。

| 符号 | 说明 |
|------|------|
| `onion_trial_gate()` | 启动门闩 |
| `onion_trial_is_active()` | 是否在有效期内 |
| `onion_trial_get_status()` | 诊断状态 |
| `onion_trial_status_t` | 状态结构 |

判定纯函数（可 host 单测）：`source/eval.c` 中的 `onion_trial_evaluate`。

## 后续移除

1. 打 `Release`、传 `-DONION_ENABLE_BETA_TRIAL=OFF`，或删 root option  
2. 删除 `source/libonion_trial/`  
3. 还原 `daemon/CMakeLists.txt` 与 `daemon/source/main.cpp` 中的 `#if` 块  
4. （可选）清理设备 `/data/OnionHEN/trial/`  
