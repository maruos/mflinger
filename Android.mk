LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE := libmaru
LOCAL_SRC_FILES := mlib.c
include $(BUILD_SHARED_LIBRARY)

include $(CLEAR_VARS)
LOCAL_MODULE := mflinger
LOCAL_SRC_FILES := mflinger.cpp
LOCAL_CFLAGS := -DLOG_TAG=\"mflinger\"
LOCAL_SHARED_LIBRARIES := \
    liblog \
    libutils \
    libgui \
    libmaru
include $(BUILD_EXECUTABLE)

include $(CLEAR_VARS)
LOCAL_MODULE := mtclient
LOCAL_SRC_FILES := test-client.c
LOCAL_SHARED_LIBRARIES := libmaru
include $(BUILD_EXECUTABLE)
