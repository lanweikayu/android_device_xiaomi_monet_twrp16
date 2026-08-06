# TWRP Device Tree for Xiaomi Mi 10 Lite 5G / Youth Edition (monet)

TWRP 3.7.1_16 device tree for the Xiaomi Mi 10 Lite 5G / Mi 10 Youth
Edition (codename **monet**, also compatible with **vangogh**), based on
Qualcomm Snapdragon 765G (**lito** / SM7250).

Built against the TWRP-Test AOSP-16 manifest (`twrp-16.0` branch).

## Device overview

| Item | Value |
|---|---|
| SoC | Qualcomm Snapdragon 765G (lito) |
| Memory | 6/8 GB LPDDR4X |
| Display | 6.57" AMOLED 1080x2400 (panel `j9_38_0a_0a_fhd_video`) |
| Storage | 128/256 GB UFS 2.1 (dynamic partitions) |
| Bootloader | A/B-less (A-only), separate `recovery` partition |
| Crypto | FBE v2, aes-256-xts/aes-256-cts, inlinecrypt, metadata encryption |
| Keymaster | Qualcomm Keymaster 4.0 + Gatekeeper 1.0 (qseecomd) |

## Key features implemented in this tree

### Boot / kernel
- **Prebuilt kernel**: the kernel image (`Image`), matching `dtb` and
  `dtbo.img` are extracted from the running system and shipped under
  `prebuilt/`. Display, touch (fts_ts) and DSI panel are therefore
  guaranteed to work (the stock MIUI-era recovery kernels fail to light up
  this panel).
- Boot image header v2 layout (kernel 0x8000, ramdisk 0x01000000,
  tags 0x100, dtb 0x01f00000), non-A/B recovery partition.

### VINTF / hwservicemanager
- The framework HAL manifests are provided in the ramdisk
  (`/system/etc/vintf/manifest.xml`, `/system_ext/etc/vintf/manifest.xml`
  with `android.hidl.manager@1.2::IServiceManager`). Without them
  hwservicemanager declares "HIDL is not supported", never sets
  `hwservicemanager.ready`, and qseecomd/keymaster/gatekeeper never start.
- The device vendor manifest (`vendor/etc/vintf/manifest.xml`) is shipped
  as-is plus the `vendor.qti.hardware.vibrator` fragment.

### FBE decryption (Qualcomm keymaster 4.0)
- Full QTI decryption stack shipped in the ramdisk: `qseecomd`,
  `android.hardware.keymaster@4.0-service-qti`,
  `android.hardware.gatekeeper@1.0-service-qti` plus all vendor libraries
  (`libqtikeymaster4.so`, `libQSEEComAPI.so`, `libion.so`, ...).
- Relink list (via `TW_RECOVERY_ADDITIONAL_RELINK_LIBRARY_FILES`) includes
  the hidl base libs and the AOSP/ramdisk-side dependencies that TWRP 16
  does not pull in by itself (`libsysutils`, `libresetprop`,
  `android.hidl.memory.token@1.0`, ...).

### Logical (dynamic) partitions
- TWRP's own `Setup_Super_Devices` / `Prepare_Super_Volume` path does not
  work at boot on this device, so the fstab references the device-mapper
  paths directly (`/dev/block/mapper/system` etc.) instead of using the
  `logical` flag.
- `create_lp` (source-built, under `build/`) maps all logical partitions
  (system/vendor/product/odm/system_ext) from the super metadata during
  `on fs`, so /vendor and /odm can be mounted early (TWRP reads their
  build properties without errors).

### Fastbootd
- The health HAL (`health-hal-2-1`) is started explicitly from
  `init.recovery.qcom.rc`; without it fastbootd blocks forever in
  `get_health_service()` and never opens the functionfs endpoints.
- fastbootd drives its own `sys.usb.ffs.ready`; no manual UDC tricks are
  needed (`enablefastboot` already goes `none -> fastboot`).

### Storage / settings
- `/data` carries the `storage` flag so it is always a valid storage
  partition (fastbootd mode skips `Setup_Fstab_Partitions`).
- `settings_gen` (source-built, under `build/`) pre-seeds
  `/persist/TWRP/.twrp_settings` with a valid storage path and sensible
  defaults (never show the "Keep System Read Only" page again, vibration
  durations). It only creates the file when missing (`O_EXCL`), so TWRP's
  own persisted settings are never overwritten.
- mkbootfs drops dotfiles, so the settings file is generated at boot
  instead of being shipped in the ramdisk.

### Haptics
- QTI AIDL vibrator HAL (`vendor.qti.hardware.vibrator.service` with its
  impl and effect libraries) is shipped and started, and
  `TW_SUPPORT_INPUT_AIDL_HAPTICS` (+ `_FIX_OFF`) is enabled so TWRP
  vibrates through the real aw8624 linear actuator via the AIDL interface.

### TWRP UI / menu
- `TW_NO_NETWORK := true` removes the WLAN / Extension entries from the
  Advanced menu.
- `BOARD_HAS_NO_REAL_SDCARD := true` removes "Partition SD Card".
- Backlight (max 2047), CPU temperature (thermal_zone17) and the
  1080x2400 + notch offsets are configured.

## Directory layout

```
device/xiaomi/monet/
├── AndroidProducts.mk
├── BoardConfig.mk
├── device.mk
├── twrp_monet.mk
├── vendorsetup.sh
├── build/                 # source-built helper tools
│   ├── create_lp/         # maps logical partitions at boot
│   └── settings_gen/      # pre-seeds TWRP persist settings
├── prebuilt/              # Image, dtb/monet.dtb, dtbo.img
└── recovery/root/         # ramdisk additions
    ├── init.recovery.qcom.rc
    ├── init.recovery.qcom_decrypt.rc (+ .fbe.rc)
    ├── init.recovery.usb.rc
    ├── system/etc/recovery.fstab
    ├── system/etc/vintf/manifest.xml
    ├── system_ext/etc/vintf/manifest.xml
    └── vendor/            # QTI crypto + vibrator stack
```

## Building

```bash
source build/envsetup.sh
lunch twrp_monet bp2a eng
mka recovery recoveryimage
```

Output: `out/target/product/monet/recovery.img`

## Flashing

```bash
fastboot flash recovery out/target/product/monet/recovery.img
# or boot without flashing:
fastboot boot out/target/product/monet/recovery.img
```

The recovery image carries an AVB hash footer self-signed with the AOSP
4096-bit test key, which is the key the stock ABL trusts on this device,
so the flashed recovery partition boots directly without extra AVB
re-signing.

## Notes / limitations

- The prebuilt kernel/dtb/dtbo match the running system. If you change
  ROMs, re-extract them and refresh `prebuilt/`.
- TWRP's `on()`-based haptics cannot use the Android perform() waveforms
  (TWRP source limitation); durations are pre-tuned under Settings ->
  Vibration.
