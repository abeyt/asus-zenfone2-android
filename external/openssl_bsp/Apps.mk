# Copyright 2006 The Android Open Source Project

LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_CLANG := true
LOCAL_STATIC_LIBRARIES += libcrypto_static2 libssl_static2
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE := libapps_static
LOCAL_ADDITIONAL_DEPENDENCIES := $(LOCAL_PATH)/android-config.mk $(LOCAL_PATH)/Apps.mk
include $(LOCAL_PATH)/Apps-config-target.mk
include $(LOCAL_PATH)/android-config.mk
include $(BUILD_STATIC_LIBRARY)

include $(CLEAR_VARS)
LOCAL_MODULE := openssl
LOCAL_MULTILIB := both
LOCAL_MODULE_STEM_32 := openssl
LOCAL_MODULE_STEM_64 := openssl64
LOCAL_CLANG := true
LOCAL_MODULE_TAGS := optional
LOCAL_SHARED_LIBRARIES := libssl2 libcrypto2
include $(LOCAL_PATH)/Apps-config-target.mk
include $(LOCAL_PATH)/android-config.mk
LOCAL_ADDITIONAL_DEPENDENCIES := $(LOCAL_PATH)/android-config.mk $(LOCAL_PATH)/Apps.mk
include $(BUILD_EXECUTABLE)

include $(CLEAR_VARS)
LOCAL_MODULE := openssl
LOCAL_MODULE_TAGS := optional
LOCAL_SHARED_LIBRARIES := libssl-host2 libcrypto-host2
include $(LOCAL_PATH)/Apps-config-host.mk
include $(LOCAL_PATH)/android-config.mk
LOCAL_ADDITIONAL_DEPENDENCIES := $(LOCAL_PATH)/android-config.mk $(LOCAL_PATH)/Apps.mk
include $(BUILD_HOST_EXECUTABLE)
