# 小米 10 青春版（monet）TWRP 设备树

为小米 10 青春版 / Mi 10 Lite 5G（代号 **monet**，与 **vangogh** 通用）
构建的 TWRP 3.7.1_16 设备树，基于高通骁龙 765G（**lito** / SM7250）。

基于 TWRP-Test AOSP-16 清单（`twrp-16.0` 分支）。

## 设备概览

| 项目 | 参数 |
|---|---|
| SoC | 高通骁龙 765G（lito） |
| 内存 | 6/8 GB LPDDR4X |
| 屏幕 | 6.57" AMOLED 1080x2400（面板 `j9_38_0a_0a_fhd_video`） |
| 存储 | 128/256 GB UFS 2.1（动态分区） |
| 启动 | 非 A/B（A-only），独立 `recovery` 分区 |
| 加密 | FBE v2，aes-256-xts/aes-256-cts，inlinecrypt，metadata 加密 |
| Keymaster | 高通 Keymaster 4.0 + Gatekeeper 1.0（qseecomd） |

## 本设备树实现的关键特性

### 启动 / 内核
- **预编译内核**：内核（`Image`）、配套 `dtb` 与 `dtbo.img` 从运行中的
  系统提取，置于 `prebuilt/`。显示、触摸（fts_ts）与 DSI 面板保证可用
  （MIUI 时代的旧 recovery 内核无法点亮此面板）。
- Boot header v2 布局（kernel 0x8000、ramdisk 0x01000000、tags 0x100、
  dtb 0x01f00000），非 A/B recovery 分区。

### VINTF / hwservicemanager
- ramdisk 内提供框架侧 HAL 清单（`/system/etc/vintf/manifest.xml`、
  `/system_ext/etc/vintf/manifest.xml`，含
  `android.hidl.manager@1.2::IServiceManager`）。缺少它们时
  hwservicemanager 会判定 "HIDL is not supported"，永不设置
  `hwservicemanager.ready`，导致 qseecomd/keymaster/gatekeeper 全部无法启动。
- 设备 vendor 清单（`vendor/etc/vintf/manifest.xml`）原样内置，并附带
  `vendor.qti.hardware.vibrator` 片段。

### FBE 解密（高通 Keymaster 4.0）
- ramdisk 内置完整高通解密栈：`qseecomd`、
  `android.hardware.keymaster@4.0-service-qti`、
  `android.hardware.gatekeeper@1.0-service-qti` 及全部 vendor 库
  （`libqtikeymaster4.so`、`libQSEEComAPI.so`、`libion.so` 等）。
- 通过 `TW_RECOVERY_ADDITIONAL_RELINK_LIBRARY_FILES` 补齐 TWRP 16 未自带的
  hidl 基础库与系统侧依赖（`libsysutils`、`libresetprop`、
  `android.hidl.memory.token@1.0` 等）。

### 逻辑（动态）分区
- TWRP 自身的 `Setup_Super_Devices` / `Prepare_Super_Volume` 在此设备启动
  时不可用，因此 fstab 直接引用 device-mapper 路径
  （`/dev/block/mapper/system` 等），不使用 `logical` 标志。
- `create_lp`（源码构建，位于 `build/`）在 `on fs` 阶段依据 super
  metadata 映射全部逻辑分区（system/vendor/product/odm/system_ext），
  使 /vendor 与 /odm 可提前挂载（TWRP 读取其 build 属性不再报错）。

### Fastbootd
- `init.recovery.qcom.rc` 显式启动 health HAL（`health-hal-2-1`）；
  否则 fastbootd 会在 `get_health_service()` 中永久阻塞，永不打开
  functionfs 端点。
- fastbootd 自行设置 `sys.usb.ffs.ready`，无需手工 UDC 技巧
  （`enablefastboot` 已走 `none -> fastboot`）。

### 存储 / 设置
- `/data` 带 `storage` 标志，保证始终是有效存储分区（fastbootd 模式会
  跳过 `Setup_Fstab_Partitions`）。
- `settings_gen`（源码构建，位于 `build/`）预置
  `/persist/TWRP/.twrp_settings`：有效的存储路径、合理默认值（不再询问
  "是否保持系统分区只读"、震动时长）。仅在文件不存在时创建（`O_EXCL`），
  不会覆盖 TWRP 自身持久化的设置。
- mkbootfs 会丢弃点文件，因此设置文件在开机时生成而非内置进 ramdisk。

### 震动
- 内置并启动 QTI AIDL 震动 HAL（`vendor.qti.hardware.vibrator.service`
  及其 impl 与效果库），并启用
  `TW_SUPPORT_INPUT_AIDL_HAPTICS`（+ `_FIX_OFF`），TWRP 通过 AIDL 接口
  驱动真实的 aw8624 线性马达。

### TWRP 界面 / 菜单
- `TW_NO_NETWORK := true` 移除 Advanced 菜单中的 WLAN / Extension 项。
- `BOARD_HAS_NO_REAL_SDCARD := true` 移除 "Partition SD Card"。
- 背光（最大 2047）、CPU 温度（thermal_zone17）、1080x2400 分辨率与
  刘海偏移均已配置。

## 目录结构

```
device/xiaomi/monet/
├── AndroidProducts.mk
├── BoardConfig.mk
├── device.mk
├── twrp_monet.mk
├── vendorsetup.sh
├── build/                 # 源码构建的辅助工具
│   ├── create_lp/         # 开机映射逻辑分区
│   └── settings_gen/      # 预置 TWRP 持久化设置
├── prebuilt/              # Image、dtb/monet.dtb、dtbo.img
└── recovery/root/         # ramdisk 附加内容
    ├── init.recovery.qcom.rc
    ├── init.recovery.qcom_decrypt.rc（+ .fbe.rc）
    ├── init.recovery.usb.rc
    ├── system/etc/recovery.fstab
    ├── system/etc/vintf/manifest.xml
    ├── system_ext/etc/vintf/manifest.xml
    └── vendor/            # QTI 解密 + 震动栈
```

## 构建

```bash
source build/envsetup.sh
lunch twrp_monet-bp2a-eng
mka recovery recoveryimage
```

产物：`out/target/product/monet/recovery.img`

## 刷入

```bash
fastboot flash recovery out/target/product/monet/recovery.img
# 或不刷入直接热启动验证：
fastboot boot out/target/product/monet/recovery.img
```

Bootloader 已解锁，无需 AVB 重新签名。

## 注意事项 / 已知限制

- 预编译内核/dtb/dtbo 与当前系统匹配；更换 ROM 后请重新提取并更新
  `prebuilt/`。
- TWRP 的震动基于 `on()`（TWRP 源码限制，无法使用 Android 的
  perform() 波形）；时长已预调优，可在 设置 -> 震动 中微调。
