# libonion_integrity — ELF 自校验（防篡改）

> 设计对齐 kylin-core `docs/ELF_PROTECTION.md`：对**可执行 PT_LOAD** 做 Ed25519 自签名校验。  
> 软件层摩擦，不能防内核级 patch。

## 机制

1. 编译期嵌入：
   - `.onion_signature`（80 字节，放在 **rodata**，不在签名范围内）
   - `.text` 内 `OHNCFG01` 配置块（vaddr + size，由签名工具回填）
   - 公钥 hex（编入 `.text`，被签名覆盖）
2. 链接后 `tools/elf_sign/sign-elf`：
   - 要求恰好 1 个可执行 `PT_LOAD`
   - 拒绝对该段的运行时 RELA
   - 签名整段 file bytes，写入 `.onion_signature`
3. 运行时 `onion_self_integrity_verify()`：
   - `__ehdr_start + protected_vaddr` 上对内存验签
4. 可选后台线程周期性复检 `onion_self_integrity_status()`

## 开关

```bash
# 推荐：build.sh 自动管 seed / pubkey（见 scripts/build.sh）
export ONION_ENABLE_ELF_PROTECTION=ON
./scripts/build.sh

# 关闭
export ONION_ENABLE_ELF_PROTECTION=OFF
```

Seed 默认：`$HOME/.config/onionhen/elf-signing-seed.hex`（mode 0600）。

## 当前接入

1. **bootstrapper**（主门闩）  
   在启动 **private elfldr / util / daemon 之前**：
   - `onion_elf_verify_signed_image(daemon_start, daemon_size)`  
   - （若开启）`onion_trial_gate()`  
   任一失败则 **不加载** 上述组件。
2. **daemon.elf** 内再做运行时自检 + 后台 monitor（纵深防御）。

其它 ELF（util / shellui）可按同样方式 link + 签名，但尚未默认开启。

## 移除

1. `-DONION_ENABLE_ELF_PROTECTION=OFF`
2. 删除 `source/libonion_integrity/` 与 `tools/elf_sign/`
3. 还原 daemon 接入与 linker 中的 `.onion_signature` 段
