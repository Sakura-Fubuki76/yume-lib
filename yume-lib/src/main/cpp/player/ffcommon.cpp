#include "ffcommon.h"
#include <cstdlib>

void releaseContext(AVCodecContext *context) {
    if (!context) return;

    SwrContext *swrContext = static_cast<SwrContext *>(context->opaque);
    if (swrContext) {
        swr_free(&swrContext);
        context->opaque = nullptr;
    }
    av_freep(&context->extradata);
    avcodec_free_context(&context);
}

AVCodec *getCodecByName(JNIEnv *env, jstring codecName) {
    if (!codecName) return nullptr;

    const char *codecNameChars = env->GetStringUTFChars(codecName, nullptr);
    auto *codec = const_cast<AVCodec *>(avcodec_find_decoder_by_name(codecNameChars));
    env->ReleaseStringUTFChars(codecName, codecNameChars);
    return codec;
}

void logError(const char *functionName, int errorNumber) {
    char buffer[ERROR_STRING_BUFFER_LENGTH];
    av_strerror(errorNumber, buffer, sizeof(buffer));
    LOGE("Error in %s: %s", functionName, buffer);
}
