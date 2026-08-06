#
# Copyright (C) 2025 The TeamWin Recovery Project
#
# SPDX-License-Identifier: Apache-2.0
#

DEVICE_PATH := device/xiaomi/monet

# Architecture
TARGET_ARCH := arm64
TARGET_ARCH_VARIANT := armv8-a
TARGET_CPU_ABI := arm64-v8a
TARGET_CPU_ABI2 :=
TARGET_CPU_VARIANT := generic
TARGET_CPU_VARIANT_RUNTIME := kryo385

TARGET_2ND_ARCH := arm
TARGET_2ND_ARCH_VARIANT := armv8-a
TARGET_2ND_CPU_ABI := armeabi-v7a
TARGET_2ND_CPU_ABI2 := armeabi
TARGET_2ND_CPU_VARIANT := generic
TARGET_2ND_CPU_VARIANT_RUNTIME := kryo385

TARGET_USES_64_BIT_BINDER := true
TARGET_SUPPORTS_64_BIT_APPS := true
TARGET_SUPPORTS_32_BIT_APPS := true

# Bootloader
TARGET_BOOTLOADER_BOARD_NAME := lito
TARGET_NO_BOOTLOADER := true

# Platform
TARGET_BOARD_PLATFORM := lito
BOARD_USES_QCOM_HARDWARE := true

# monet is A-only (non-A/B): boot and recovery are separate partitions and
# there are no boot_* / recovery_* slots.  The AOSP build default is
# AB_OTA_UPDATER=true, which makes TWRP expose the "Slot A/B" and "Flash to
# both slots" entries and report ro.build.ab_update=true in recovery.
AB_OTA_UPDATER := false

# AVB: the stock ABL verifies the recovery partition with AVB and expects a
# hash footer.  The device's vendor recovery image is signed with the AOSP
# 4096-bit test key (public key sha1 2597c218aae470a130f61162feaae70afd97f011),
# so recovery must be self-signed with the same key for a flashed image to
# boot from the recovery partition.
BOARD_AVB_ENABLE := true
BOARD_AVB_ALGORITHM := SHA256_RSA4096
BOARD_AVB_KEY_PATH := external/avb/test/data/testkey_rsa4096.pem
BOARD_AVB_RECOVERY_KEY_PATH := $(BOARD_AVB_KEY_PATH)
BOARD_AVB_RECOVERY_ALGORITHM := $(BOARD_AVB_ALGORITHM)
BOARD_AVB_RECOVERY_ROLLBACK_INDEX := 0
BOARD_AVB_RECOVERY_ROLLBACK_INDEX_LOCATION := 0

# Partition layout: real vendor partition layout (not system/vendor).
# With TARGET_COPY_OUT_VENDOR := vendor, BOARD_USES_VENDORIMAGE becomes true and
# the baseline root fileset materializes /vendor as a real directory instead of
# the "vendor -> /system/vendor" symlink (avoids recovery ramdisk rsync clash
# with the TWRP crypto stack in /vendor).
TARGET_COPY_OUT_VENDOR := vendor

# Build hacks
ALLOW_MISSING_DEPENDENCIES := true
BUILD_BROKEN_DUP_RULES := true
BUILD_BROKEN_ELF_PREBUILT_PRODUCT_COPY_FILES := true
BUILD_BROKEN_MISSING_REQUIRED_MODULES := true

# Kernel (prebuilt, extracted from running system)
BOARD_KERNEL_BASE := 0x00000000
BOARD_KERNEL_PAGESIZE := 4096
BOARD_KERNEL_OFFSET := 0x00008000
BOARD_RAMDISK_OFFSET := 0x01000000
BOARD_KERNEL_TAGS_OFFSET := 0x00000100
BOARD_DTB_OFFSET := 0x01f00000
BOARD_BOOT_HEADER_VERSION := 2
BOARD_KERNEL_IMAGE_NAME := Image
TARGET_PREBUILT_KERNEL := $(DEVICE_PATH)/prebuilt/Image
BOARD_INCLUDE_DTB_IN_BOOTIMG := true
BOARD_PREBUILT_DTBIMAGE_DIR := $(DEVICE_PATH)/prebuilt/dtb
BOARD_PREBUILT_DTBOIMAGE := $(DEVICE_PATH)/prebuilt/dtbo.img
BOARD_INCLUDE_RECOVERY_DTBO := true

BOARD_KERNEL_CMDLINE := \
    console=ttyMSM0,115200n8 \
    androidboot.hardware=qcom \
    androidboot.console=ttyMSM0 \
    androidboot.memcg=1 \
    androidboot.init_fatal_reboot_target=recovery \
    androidboot.usbcontroller=a600000.dwc3 \
    cgroup.memory=nokmem,nosocket \
    kpti=off \
    lpm_levels.sleep_disabled=1 \
    msm_rtb.filter=0x237 \
    reboot=panic_warm \
    service_locator.enable=1 \
    swiotlb=2048 \
    loop.max_part=7

BOARD_MKBOOTIMG_ARGS += --header_version $(BOARD_BOOT_HEADER_VERSION)
BOARD_MKBOOTIMG_ARGS += --kernel_offset $(BOARD_KERNEL_OFFSET)
BOARD_MKBOOTIMG_ARGS += --ramdisk_offset $(BOARD_RAMDISK_OFFSET)
BOARD_MKBOOTIMG_ARGS += --tags_offset $(BOARD_KERNEL_TAGS_OFFSET)
BOARD_MKBOOTIMG_ARGS += --dtb_offset $(BOARD_DTB_OFFSET)
BOARD_MKBOOTIMG_ARGS += --pagesize $(BOARD_KERNEL_PAGESIZE) --board ""

# Partitions
BOARD_FLASH_BLOCK_SIZE := 262144
BOARD_BOOTIMAGE_PARTITION_SIZE := 134217728
BOARD_RECOVERYIMAGE_PARTITION_SIZE := 134217728
BOARD_DTBOIMG_PARTITION_SIZE := 33554432
BOARD_USERDATAIMAGE_PARTITION_SIZE := 114936492032

# Dynamic partitions (super = sda33, 8912896 KiB)
BOARD_SUPER_PARTITION_SIZE := 9126805504
BOARD_SUPER_PARTITION_GROUPS := qti_dynamic_partitions
BOARD_QTI_DYNAMIC_PARTITIONS_PARTITION_LIST := system system_ext product vendor odm
BOARD_QTI_DYNAMIC_PARTITIONS_SIZE := 9122611200

# File systems
TARGET_USERIMAGES_USE_EXT4 := true
TARGET_USERIMAGES_USE_F2FS := true
BOARD_USES_METADATA_PARTITION := true

# Materialize /system_ext in the root fileset as a real directory (not the
# "system_ext -> /system/system_ext" symlink): the TWRP ramdisk ships
# /system_ext/etc/vintf/ files, and the recovery rsync step cannot replace
# a non-empty directory with a symlink.
BOARD_SYSTEM_EXTIMAGE_FILE_SYSTEM_TYPE := ext4
TARGET_COPY_OUT_SYSTEM_EXT := system_ext

# Recovery
TARGET_RECOVERY_PIXEL_FORMAT := RGBX_8888
TARGET_USES_MKE2FS := true
RECOVERY_SDCARD_ON_DATA := true

# Anti-rollback workaround for FBE key unwrap in recovery
PLATFORM_VERSION := 99.87.36
PLATFORM_VERSION_LAST_STABLE := 99.87.36
PLATFORM_SECURITY_PATCH := 2099-12-31
VENDOR_SECURITY_PATCH := 2099-12-31
BOOT_SECURITY_PATCH := 2099-12-31

# TWRP - display
TW_THEME := portrait_hdpi
TW_FRAMERATE := 60
TW_MAX_BRIGHTNESS := 2047
TW_DEFAULT_BRIGHTNESS := 1024
TW_BRIGHTNESS_PATH := "/sys/class/backlight/panel0-backlight/brightness"
TW_CUSTOM_CPU_TEMP_PATH := "/sys/devices/virtual/thermal/thermal_zone17/temp"
TW_Y_OFFSET := 60
TW_H_OFFSET := -60

# TWRP - features
TW_DEVICE_VERSION := Mi10-Lite
TW_EXTRA_LANGUAGES := true
TW_EXCLUDE_DEFAULT_USB_INIT := true
TW_INCLUDE_FASTBOOTD := true
TW_USE_FSCRYPT_POLICY := 2
TW_EXCLUDE_TWRPAPP := true

# Hide the WLAN / Extension entries from the Advanced menu
TW_NO_NETWORK := true

# No physical SD card slot - hide "Partition SD Card" from Advanced
BOARD_HAS_NO_REAL_SDCARD := true

# QTI AIDL vibrator haptics (aw8624 via vendor.qti.hardware.vibrator)
TW_SUPPORT_INPUT_AIDL_HAPTICS := true
TW_SUPPORT_INPUT_AIDL_HAPTICS_FIX_OFF := true

# TWRP - crypto (Qualcomm FBE v2 inlinecrypt + metadata encryption)
TW_INCLUDE_CRYPTO := true
TW_INCLUDE_CRYPTO_FBE := true
TW_INCLUDE_FBE_METADATA_DECRYPT := true

# Extra libs needed by prebuilt qseecom/keymaster/gatekeeper binaries
TW_RECOVERY_ADDITIONAL_RELINK_LIBRARY_FILES += \
    $(TARGET_OUT_SHARED_LIBRARIES)/libhidlmemory.so \
    $(TARGET_OUT_SHARED_LIBRARIES)/android.hidl.allocator@1.0.so \
    $(TARGET_OUT_SHARED_LIBRARIES)/android.hidl.memory@1.0.so \
    $(TARGET_OUT_SHARED_LIBRARIES)/android.hidl.memory.token@1.0.so

# Recovery binary (twrp) links libsysutils/libresetprop; relink them into the
# ramdisk (missing deps made the recovery service crash-loop at boot).
TW_RECOVERY_ADDITIONAL_RELINK_LIBRARY_FILES += \
    $(TARGET_OUT_SHARED_LIBRARIES)/libsysutils.so \
    $(TARGET_OUT_SHARED_LIBRARIES)/libresetprop.so

# AIDL vibrator HAL client library for QTI haptics
TW_RECOVERY_ADDITIONAL_RELINK_LIBRARY_FILES += \
    $(TARGET_OUT_SHARED_LIBRARIES)/android.hardware.vibrator-V2-ndk.so

# Device helper tools (built from source) relinked into the recovery ramdisk
RECOVERY_BINARY_SOURCE_FILES += \
    $(TARGET_OUT_EXECUTABLES)/settings_gen \
    $(TARGET_OUT_EXECUTABLES)/create_lp

# MTP fix: the stock bootable/recovery/mtp/ffs/Android.mk module
# libtwrpmtp-ffs sends DATE_MODIFIED/DATE_ADDED as UINT64 in the batch
# ObjectPropertyList reply, which makes libmtp/GVFS on the host crash when a
# directory with >4 GiB files is opened.  The fixed library is compiled from
# build/twrpmtp-ffs/Android.bp (module libtwrpmtp-ffs-fixed) and relinked into
# the ramdisk like any other recovery lib; BOARD_RECOVERY_IMAGE_PREPARE (runs
# after the ramdisk is assembled) then overwrites the unpatched
# libtwrpmtp-ffs.so with the fixed build.  bootable/recovery is untouched.
TW_RECOVERY_ADDITIONAL_RELINK_LIBRARY_FILES += \
    $(TARGET_OUT_SHARED_LIBRARIES)/libtwrpmtp-ffs-fixed.so

BOARD_RECOVERY_IMAGE_PREPARE += \
    cp -f $(TARGET_RECOVERY_ROOT_OUT)/system/lib64/libtwrpmtp-ffs-fixed.so $(TARGET_RECOVERY_ROOT_OUT)/system/lib64/libtwrpmtp-ffs.so; \
    cp -f $(TARGET_RECOVERY_ROOT_OUT)/system/lib/libtwrpmtp-ffs-fixed.so $(TARGET_RECOVERY_ROOT_OUT)/system/lib/libtwrpmtp-ffs.so || true;
