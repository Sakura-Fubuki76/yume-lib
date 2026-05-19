#pragma once

#include <jni.h>

int utils_fields_init(JavaVM *vm);
void utils_fields_free(JavaVM *vm);
JNIEnv *utils_get_env();
void utils_call_instance_method_void(JNIEnv *env, jobject instance, jmethodID methodID, ...);

struct fields {
    struct {
        jclass clazz;
        jmethodID onMediaInfoFoundID;
        jmethodID onVideoStreamFoundID;
        jmethodID onAudioStreamFoundID;
        jmethodID onSubtitleStreamFoundID;
        jmethodID onChapterFoundID;
        jmethodID onErrorID;
        jmethodID onFrameLoaderContextID;
    } MediaInfoBuilder;
};

extern struct fields fields;
