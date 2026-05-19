#include <jni.h>

#include "SoundTouchDLL.h"

namespace {

HANDLE as_handle(jlong handle) {
    return reinterpret_cast<HANDLE>(handle);
}

}

extern "C" JNIEXPORT jlong JNICALL
Java_com_tianscar_soundtouch_SoundTouch_createInstance(JNIEnv*, jclass) {
    return reinterpret_cast<jlong>(soundtouch_createInstance());
}

extern "C" JNIEXPORT void JNICALL
Java_com_tianscar_soundtouch_SoundTouch_destroyInstance(JNIEnv*, jclass, jlong handle) {
    soundtouch_destroyInstance(as_handle(handle));
}

extern "C" JNIEXPORT void JNICALL
Java_com_tianscar_soundtouch_SoundTouch_setRate(JNIEnv*, jclass, jlong handle, jfloat rate) {
    soundtouch_setRate(as_handle(handle), rate);
}

extern "C" JNIEXPORT void JNICALL
Java_com_tianscar_soundtouch_SoundTouch_setTempo(JNIEnv*, jclass, jlong handle, jfloat tempo) {
    soundtouch_setTempo(as_handle(handle), tempo);
}

extern "C" JNIEXPORT void JNICALL
Java_com_tianscar_soundtouch_SoundTouch_setRateChange(JNIEnv*, jclass, jlong handle, jfloat rate) {
    soundtouch_setRateChange(as_handle(handle), rate);
}

extern "C" JNIEXPORT void JNICALL
Java_com_tianscar_soundtouch_SoundTouch_setTempoChange(JNIEnv*, jclass, jlong handle, jfloat tempo) {
    soundtouch_setTempoChange(as_handle(handle), tempo);
}

extern "C" JNIEXPORT void JNICALL
Java_com_tianscar_soundtouch_SoundTouch_setPitch(JNIEnv*, jclass, jlong handle, jfloat pitch) {
    soundtouch_setPitch(as_handle(handle), pitch);
}

extern "C" JNIEXPORT void JNICALL
Java_com_tianscar_soundtouch_SoundTouch_setPitchOctaves(JNIEnv*, jclass, jlong handle, jfloat pitch) {
    soundtouch_setPitchOctaves(as_handle(handle), pitch);
}

extern "C" JNIEXPORT void JNICALL
Java_com_tianscar_soundtouch_SoundTouch_setPitchSemiTones(JNIEnv*, jclass, jlong handle, jfloat pitch) {
    soundtouch_setPitchSemiTones(as_handle(handle), pitch);
}

extern "C" JNIEXPORT void JNICALL
Java_com_tianscar_soundtouch_SoundTouch_setChannels(JNIEnv*, jclass, jlong handle, jlong channels) {
    soundtouch_setChannels(as_handle(handle), static_cast<unsigned int>(channels));
}

extern "C" JNIEXPORT void JNICALL
Java_com_tianscar_soundtouch_SoundTouch_setSampleRate(JNIEnv*, jclass, jlong handle, jlong sample_rate) {
    soundtouch_setSampleRate(as_handle(handle), static_cast<unsigned int>(sample_rate));
}

extern "C" JNIEXPORT void JNICALL
Java_com_tianscar_soundtouch_SoundTouch_flush(JNIEnv*, jclass, jlong handle) {
    soundtouch_flush(as_handle(handle));
}

extern "C" JNIEXPORT void JNICALL
Java_com_tianscar_soundtouch_SoundTouch_putSamples(JNIEnv* env, jclass, jlong handle, jfloatArray samples, jint offset, jint frames) {
    jfloat* data = env->GetFloatArrayElements(samples, nullptr);
    if (data == nullptr) return;
    soundtouch_putSamples(as_handle(handle), data + offset, static_cast<unsigned int>(frames));
    env->ReleaseFloatArrayElements(samples, data, JNI_ABORT);
}

extern "C" JNIEXPORT void JNICALL
Java_com_tianscar_soundtouch_SoundTouch_putSamples_1i16(JNIEnv* env, jclass, jlong handle, jshortArray samples, jint offset, jint frames) {
    jshort* data = env->GetShortArrayElements(samples, nullptr);
    if (data == nullptr) return;
    soundtouch_putSamples_i16(as_handle(handle), data + offset, static_cast<unsigned int>(frames));
    env->ReleaseShortArrayElements(samples, data, JNI_ABORT);
}

extern "C" JNIEXPORT void JNICALL
Java_com_tianscar_soundtouch_SoundTouch_clear(JNIEnv*, jclass, jlong handle) {
    soundtouch_clear(as_handle(handle));
}

extern "C" JNIEXPORT jint JNICALL
Java_com_tianscar_soundtouch_SoundTouch_setSetting(JNIEnv*, jclass, jlong handle, jint setting_id, jint value) {
    return soundtouch_setSetting(as_handle(handle), setting_id, value);
}

extern "C" JNIEXPORT jint JNICALL
Java_com_tianscar_soundtouch_SoundTouch_getSetting(JNIEnv*, jclass, jlong handle, jint setting_id) {
    return soundtouch_getSetting(as_handle(handle), setting_id);
}

extern "C" JNIEXPORT jlong JNICALL
Java_com_tianscar_soundtouch_SoundTouch_numUnprocessedSamples(JNIEnv*, jclass, jlong handle) {
    return soundtouch_numUnprocessedSamples(as_handle(handle));
}

extern "C" JNIEXPORT jint JNICALL
Java_com_tianscar_soundtouch_SoundTouch_receiveSamples(JNIEnv* env, jclass, jlong handle, jfloatArray out_buffer, jint offset, jint max_samples) {
    jfloat* data = env->GetFloatArrayElements(out_buffer, nullptr);
    if (data == nullptr) return 0;
    const auto received = soundtouch_receiveSamples(as_handle(handle), data + offset, static_cast<unsigned int>(max_samples));
    env->ReleaseFloatArrayElements(out_buffer, data, 0);
    return static_cast<jint>(received);
}

extern "C" JNIEXPORT jint JNICALL
Java_com_tianscar_soundtouch_SoundTouch_receiveSamples_1i16(JNIEnv* env, jclass, jlong handle, jshortArray out_buffer, jint offset, jint max_samples) {
    jshort* data = env->GetShortArrayElements(out_buffer, nullptr);
    if (data == nullptr) return 0;
    const auto received = soundtouch_receiveSamples_i16(as_handle(handle), data + offset, static_cast<unsigned int>(max_samples));
    env->ReleaseShortArrayElements(out_buffer, data, 0);
    return static_cast<jint>(received);
}

extern "C" JNIEXPORT jlong JNICALL
Java_com_tianscar_soundtouch_SoundTouch_numSamples(JNIEnv*, jclass, jlong handle) {
    return soundtouch_numSamples(as_handle(handle));
}

extern "C" JNIEXPORT jint JNICALL
Java_com_tianscar_soundtouch_SoundTouch_isEmpty(JNIEnv*, jclass, jlong handle) {
    return soundtouch_isEmpty(as_handle(handle));
}
