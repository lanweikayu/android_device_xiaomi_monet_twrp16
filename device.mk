#
# Copyright (C) 2025 The TeamWin Recovery Project
#
# SPDX-License-Identifier: Apache-2.0
#

DEVICE_PATH := device/xiaomi/monet

# Base product configuration (provides recovery-essential modules:
# linker.recovery, shell_and_utilities_recovery, init_second_stage.recovery,
# charger.recovery, watchdogd.recovery, adbd.recovery, ...)
$(call inherit-product, $(SRC_TARGET_DIR)/product/base.mk)

# Emulated storage for /sdcard on /data/media
$(call inherit-product, $(SRC_TARGET_DIR)/product/emulated_storage.mk)

PRODUCT_USE_DYNAMIC_PARTITIONS := true

# Tools built from source and relinked into the recovery ramdisk
PRODUCT_PACKAGES += \
    settings_gen \
    create_lp \
    avb_disable \
    libtwrpmtp-ffs-fixed

# NOTE: recovery/root is picked up automatically by the build system via
# recovery_root_private ($(TARGET_DEVICE_DIR)/recovery/root), copied into the
# ramdisk AFTER the baseline root rsync (no vendor-dir conflicts).

