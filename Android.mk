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

LOCAL_SRC_FILES := torrentd.cpp httpd.cpp
LOCAL_MODULE:= torrentd

LOCAL_CFLAGS += -I$(REPO_TOP_DIR)/native/boost/boost_1_69_0-$(TARGET_ARCH_ABI) -std=c++11
LOCAL_LDLIBS= $(REPO_TOP_DIR)/native/boost/boost_1_69_0-$(TARGET_ARCH_ABI)/stage/lib/libboost_system.a

LOCAL_CFLAGS += -I$(REPO_TOP_DIR)/native/libtorrent/include
LOCAL_LDLIBS += $(REPO_TOP_DIR)/native/libtorrent/bin-$(TARGET_ARCH_ABI)/clang-$(os)-android/release/link-static/threading-multi/libtorrent.a

LOCAL_LDLIBS += $(android_ndk)/sources/cxx-stl/llvm-libc++/libs/$(TARGET_ARCH_ABI)/libc++_static.a
LOCAL_LDLIBS += -lstdc++ -latomic

LOCAL_CPPFLAGS += -fexceptions
LOCAL_CPPFLAGS += -frtti -std=c++11
LOCAL_CXXFLAGS += -std=c++11

LOCAL_MODULE_TAGS := optional

include $(BUILD_EXECUTABLE)
