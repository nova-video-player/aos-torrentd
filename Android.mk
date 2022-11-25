# Copyright 2017 Archos SA
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

os := $(shell echo $(shell uname -s) | tr '[:upper:]' '[:lower:]')

LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

LOCAL_SRC_FILES := torrentd.cpp httpd.cpp \
	$(REPO_TOP_DIR)/native/libtorrent-android-builder/libtorrent/deps/try_signal/signal_error_code.cpp \
	$(REPO_TOP_DIR)/native/libtorrent-android-builder/libtorrent/deps/try_signal/try_signal.cpp \

LOCAL_MODULE:= torrentd

LOCAL_CFLAGS += -I$(REPO_TOP_DIR)/native/boost/boost_1_80_0
LOCAL_LDLIBS= $(REPO_TOP_DIR)/native/boost/boost_1_80_0-$(TARGET_ARCH_ABI)/boost/bin.v2/libs/system/build/clang-$(os)-android/release/cxxstd-14-iso/link-static/target-os-android/threading-multi/visibility-hidden/libboost_system.a
LOCAL_CFLAGS += -I$(REPO_TOP_DIR)/native/libtorrent-android-builder/libtorrent/include
LOCAL_LDLIBS += $(REPO_TOP_DIR)/native/boost/boost_1_80_0-$(TARGET_ARCH_ABI)/torrent/clang-$(os)-android/release/cxxstd-14-iso/link-static/target-os-android/threading-multi/visibility-hidden/libtorrent-rasterbar.a
LOCAL_LDLIBS += $(REPO_TOP_DIR)/native/openssl-android-builder/dist-$(TARGET_ARCH_ABI)/lib/libssl.a
LOCAL_LDLIBS += $(REPO_TOP_DIR)/native/openssl-android-builder/dist-$(TARGET_ARCH_ABI)/lib/libcrypto.a

LOCAL_LDLIBS += -latomic

LOCAL_CPPFLAGS += -fexceptions
LOCAL_CPPFLAGS += -frtti

LOCAL_CXXFLAGS += -std=c++14

LOCAL_MODULE_TAGS := optional

include $(BUILD_EXECUTABLE)
