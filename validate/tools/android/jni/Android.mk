LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_CFLAGS 		:= -w
LOCAL_MODULE    := android_launch
LOCAL_SRC_FILES := android-launch.c gst-validate-android.c ../../gst-validate.c ../../gst-validate-transcoding.c gst-inspect.c
LOCAL_SHARED_LIBRARIES := gstreamer_android
LOCAL_LDLIBS := -llog -landroid
include $(BUILD_SHARED_LIBRARY)

ifndef GSTREAMER_ROOT
ifndef GSTREAMER_ROOT_ANDROID
$(error GSTREAMER_ROOT_ANDROID is not defined!)
endif
GSTREAMER_ROOT        := $(GSTREAMER_ROOT_ANDROID)
endif

GSTREAMER_NDK_BUILD_PATH  := $(GSTREAMER_ROOT)/share/gst-android/ndk-build/

include $(GSTREAMER_NDK_BUILD_PATH)/plugins.mk
GSTREAMER_PLUGINS         := $(GSTREAMER_PLUGINS_CORE) $(GSTREAMER_PLUGINS_PLAYBACK) $(GSTREAMER_PLUGINS_CODECS) $(GSTREAMER_PLUGINS_NET) $(GSTREAMER_PLUGINS_SYS) $(GSTREAMER_CODECS_RESTRICTED) $(GSTREAMER_CODECS_GPL) $(GSTREAMER_PLUGINS_ENCODING) $(GSTREAMER_PLUGINS_VIS) $(GSTREAMER_PLUGINS_EFFECTS) $(GSTREAMER_PLUGINS_NET_RESTRICTED)
GSTREAMER_EXTRA_DEPS      := gstreamer-video-1.0 gio-2.0 gst-validate-1.0 glib-2.0

include $(GSTREAMER_NDK_BUILD_PATH)/gstreamer-1.0.mk
