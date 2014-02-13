# Copyright (C) 2009 The Android Open Source Project
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
LOCAL_PATH := $(call my-dir)


include $(CLEAR_VARS)
LOCAL_MODULE	:= libbluetooth
LOCAL_SRC_FILES	:= external/lib/libbluetooth.so
include $(PREBUILT_SHARED_LIBRARY)

include $(CLEAR_VARS)

LOCAL_MODULE    := virtual_device_noacc_noir
LOCAL_SRC_FILES := classic.c dynamics.c events.c io.c io_nix.c ir.c nunchuk.c wiiuse.c guitar_hero_3.c mousemode.c
LOCAL_CFLAGS = -g
LOCAL_SHARED_LIBRARIES := libbluetooth 
LOCAL_C_INCLUDES       := external/include
#include $(BUILD_SHARED_LIBRARY)
include $(BUILD_EXECUTABLE)
