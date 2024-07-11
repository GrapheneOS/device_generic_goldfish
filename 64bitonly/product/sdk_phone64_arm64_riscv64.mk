#
# Copyright (C) 2024 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

# sdk_phone64_arm64 with riscv64 translated

$(call inherit-product, device/generic/goldfish/64bitonly/product/sdk_phone64_arm64.mk)

# TODO(b/303700901): Add riscv64 translation support.

# Overrides
PRODUCT_BRAND := Android
PRODUCT_NAME := sdk_phone64_arm64_riscv64
PRODUCT_DEVICE := emu64ar
PRODUCT_MODEL := Android SDK built for arm64 with riscv64 translated
