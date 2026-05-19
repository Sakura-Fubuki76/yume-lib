#include <jni.h>
#include <vector>
#include <string>
#include "libavcodec/version.h"
#include "libavcodec/defs.h"
#include "ffcommon.h"


jint JNI_OnLoad(JavaVM *vm, void *reserved) {
    JNIEnv *env;
    if (vm->GetEnv(reinterpret_cast<void **>(&env), JNI_VERSION_1_6) != JNI_OK) {
        return -1;
    }
    return JNI_VERSION_1_6;
}

extern "C"
JNIEXPORT jstring JNICALL
Java_io_github_sakurafubuki_yume_nativelib_player_FfmpegLibrary_ffmpegGetVersion(JNIEnv *env,
                                                                   jclass clazz) {
    return env->NewStringUTF(LIBAVCODEC_IDENT);
}

extern "C"
JNIEXPORT jint JNICALL
Java_io_github_sakurafubuki_yume_nativelib_player_FfmpegLibrary_ffmpegGetInputBufferPaddingSize(
        JNIEnv *env, jclass clazz) {
    return (jint) AV_INPUT_BUFFER_PADDING_SIZE;
}

extern "C"
JNIEXPORT jboolean JNICALL
Java_io_github_sakurafubuki_yume_nativelib_player_FfmpegLibrary_ffmpegHasDecoder(JNIEnv *env,
                                                                   jclass clazz,
                                                                   jstring codec_name) {
    return getCodecByName(env, codec_name) != nullptr;
}

extern "C"
JNIEXPORT jobjectArray JNICALL
Java_io_github_sakurafubuki_yume_nativelib_player_FfmpegLibrary_ffmpegListDecoders(JNIEnv *env,
                                                                                     jclass clazz) {
    std::vector<std::string> decoderNames;
    const AVCodec *codec = nullptr;
    void *iter = nullptr;
    while ((codec = av_codec_iterate(&iter))) {
        if (av_codec_is_decoder(codec)) {
            decoderNames.push_back(codec->name);
        }
    }

    jclass stringClass = env->FindClass("java/lang/String");
    jobjectArray result = env->NewObjectArray(
        static_cast<jsize>(decoderNames.size()), stringClass, nullptr);
    for (size_t i = 0; i < decoderNames.size(); i++) {
        env->SetObjectArrayElement(result, static_cast<jsize>(i),
                                   env->NewStringUTF(decoderNames[i].c_str()));
    }
    return result;
}