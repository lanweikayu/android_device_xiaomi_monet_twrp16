#
# Copyright (C) 2025 The TeamWin Recovery Project
#
# SPDX-License-Identifier: Apache-2.0
#

DEVICE_PATH := device/xiaomi/monet

# Inherit from TWRP common product configuration
$(call inherit-product, vendor/twrp/config/common.mk)

# Inherit from device-specific configuration
$(call inherit-product, $(DEVICE_PATH)/device.mk)

PRODUCT_DEVICE := monet
PRODUCT_NAME := twrp_monet
PRODUCT_BRAND := xiaomi
PRODUCT_MODEL := Mi 10 Lite 5G
PRODUCT_MANUFACTURER := xiaomi
PRODUCT_RELEASE_NAME := monet
