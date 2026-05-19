#include <jni.h>
#include <cstdlib>
#include <algorithm>
#include <android/native_window_jni.h>
#include "ffcommon.h"

extern "C" {
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
}

#define ALIGN(x, a) (((x) + ((a) - 1)) & ~((a) - 1))

static const int VIDEO_DECODER_SUCCESS = 0;
static const int VIDEO_DECODER_ERROR_INVALID_DATA = -1;
static const int VIDEO_DECODER_ERROR_OTHER = -2;
static const int VIDEO_DECODER_ERROR_READ_FRAME = -3;

const int kPlaneY = 0;
const int kPlaneU = 1;
const int kPlaneV = 2;
const int kImageFormatYV12 = 0x32315659;

struct JniContext {
    ~JniContext() {
        if (native_window) ANativeWindow_release(native_window);
    }

    bool MaybeAcquireNativeWindow(JNIEnv *env, jobject new_surface) {
        if (surface == new_surface) return true;
        if (native_window) {
            ANativeWindow_release(native_window);
        }
        native_window_width = 0;
        native_window_height = 0;
        native_window = ANativeWindow_fromSurface(env, new_surface);
        if (!native_window) {
            LOGE("ANativeWindow_fromSurface failed");
            surface = nullptr;
            return false;
        }
        surface = new_surface;
        return true;
    }

    jfieldID data_field{};
    jfieldID yuvPlanes_field{};
    jfieldID yuvStrides_field{};
    jmethodID init_for_yuv_frame_method{};
    jmethodID init_method{};

    AVCodecContext *codecContext{};
    SwsContext *swsContext{};

    ANativeWindow *native_window = nullptr;
    jobject surface = nullptr;
    int native_window_width = 0;
    int native_window_height = 0;
};

static JniContext *createVideoContext(JNIEnv *env, AVCodec *codec,
                                      jbyteArray extraData, jint threads) {
    auto *jniContext = new JniContext();
    AVCodecContext *codecContext = avcodec_alloc_context3(codec);
    if (!codecContext) {
        LOGE("Failed to allocate context.");
        delete jniContext;
        return nullptr;
    }

    if (extraData) {
        jsize size = env->GetArrayLength(extraData);
        codecContext->extradata_size = size;
        codecContext->extradata = static_cast<uint8_t *>(
            av_malloc(size + AV_INPUT_BUFFER_PADDING_SIZE));
        if (!codecContext->extradata) {
            LOGE("Failed to allocate extradata.");
            releaseContext(codecContext);
            delete jniContext;
            return nullptr;
        }
        env->GetByteArrayRegion(extraData, 0, size,
                                reinterpret_cast<jbyte *>(codecContext->extradata));
    }

    codecContext->thread_count = threads;
    codecContext->err_recognition = AV_EF_IGNORE_ERR;
    int result = avcodec_open2(codecContext, codec, nullptr);
    if (result < 0) {
        logError("avcodec_open2", result);
        releaseContext(codecContext);
        delete jniContext;
        return nullptr;
    }

    jniContext->codecContext = codecContext;

    jclass outputBufferClass = env->FindClass(
        "androidx/media3/decoder/VideoDecoderOutputBuffer");
    jniContext->data_field = env->GetFieldID(outputBufferClass, "data",
                                              "Ljava/nio/ByteBuffer;");
    jniContext->yuvStrides_field = env->GetFieldID(outputBufferClass, "yuvStrides",
                                                    "[I");
    jniContext->yuvPlanes_field = env->GetFieldID(outputBufferClass, "yuvPlanes",
                                                   "[Ljava/nio/ByteBuffer;");
    jniContext->init_for_yuv_frame_method = env->GetMethodID(
        outputBufferClass, "initForYuvFrame", "(IIIII)Z");
    jniContext->init_method = env->GetMethodID(
        outputBufferClass, "init", "(JILjava/nio/ByteBuffer;)V");

    return jniContext;
}


extern "C"
JNIEXPORT jlong JNICALL
Java_io_github_sakurafubuki_yume_nativelib_player_FfmpegVideoDecoder_ffmpegInitialize(
        JNIEnv *env, jobject, jstring codec_name, jbyteArray extra_data, jint threads) {
    AVCodec *codec = getCodecByName(env, codec_name);
    if (!codec) {
        LOGE("Codec not found.");
        return 0L;
    }
    return reinterpret_cast<jlong>(createVideoContext(env, codec, extra_data, threads));
}

extern "C"
JNIEXPORT jlong JNICALL
Java_io_github_sakurafubuki_yume_nativelib_player_FfmpegVideoDecoder_ffmpegReset(
        JNIEnv *, jobject, jlong jContext) {
    auto *jniContext = reinterpret_cast<JniContext *>(jContext);
    if (!jniContext || !jniContext->codecContext) {
        LOGE("Tried to reset without a context.");
        return 0L;
    }
    avcodec_flush_buffers(jniContext->codecContext);
    return reinterpret_cast<jlong>(jniContext);
}

extern "C"
JNIEXPORT void JNICALL
Java_io_github_sakurafubuki_yume_nativelib_player_FfmpegVideoDecoder_ffmpegRelease(
        JNIEnv *, jobject, jlong jContext) {
    auto *jniContext = reinterpret_cast<JniContext *>(jContext);
    if (!jniContext) return;
    if (jniContext->swsContext) sws_freeContext(jniContext->swsContext);
    if (jniContext->codecContext) releaseContext(jniContext->codecContext);
    delete jniContext;
}

extern "C"
JNIEXPORT jint JNICALL
Java_io_github_sakurafubuki_yume_nativelib_player_FfmpegVideoDecoder_ffmpegRenderFrame(
        JNIEnv *env, jobject, jlong jContext, jobject surface,
        jobject output_buffer, jint displayed_width, jint displayed_height) {
    auto *jniContext = reinterpret_cast<JniContext *>(jContext);
    if (!jniContext->MaybeAcquireNativeWindow(env, surface)) {
        return VIDEO_DECODER_ERROR_OTHER;
    }

    if (jniContext->native_window_width != displayed_width ||
        jniContext->native_window_height != displayed_height) {

        if (ANativeWindow_setBuffersGeometry(jniContext->native_window,
                                              displayed_width, displayed_height,
                                              kImageFormatYV12)) {
            LOGE("ANativeWindow_setBuffersGeometry failed");
            return VIDEO_DECODER_ERROR_OTHER;
        }

        jniContext->native_window_width = displayed_width;
        jniContext->native_window_height = displayed_height;

        SwsContext *swsContext = sws_getContext(
            displayed_width, displayed_height, jniContext->codecContext->pix_fmt,
            displayed_width, displayed_height, AV_PIX_FMT_YUV420P,
            SWS_BILINEAR, nullptr, nullptr, nullptr);
        if (!swsContext) {
            LOGE("Failed to allocate swsContext.");
            return VIDEO_DECODER_ERROR_OTHER;
        }
        jniContext->swsContext = swsContext;
    }

    ANativeWindow_Buffer native_window_buffer;
    int result = ANativeWindow_lock(jniContext->native_window,
                                     &native_window_buffer, nullptr);
    if (result == -19) {
        jniContext->surface = nullptr;
        return VIDEO_DECODER_SUCCESS;
    } else if (result || !native_window_buffer.bits) {
        LOGE("ANativeWindow_lock failed");
        return VIDEO_DECODER_ERROR_OTHER;
    }

    jobject yuvPlanesObj = env->GetObjectField(output_buffer, jniContext->yuvPlanes_field);
    auto yuvPlanesArray = reinterpret_cast<jobjectArray>(yuvPlanesObj);
    jobject yuvPlanesY = env->GetObjectArrayElement(yuvPlanesArray, kPlaneY);
    jobject yuvPlanesU = env->GetObjectArrayElement(yuvPlanesArray, kPlaneU);
    jobject yuvPlanesV = env->GetObjectArrayElement(yuvPlanesArray, kPlaneV);

    auto *planeY = static_cast<uint8_t *>(env->GetDirectBufferAddress(yuvPlanesY));
    auto *planeU = static_cast<uint8_t *>(env->GetDirectBufferAddress(yuvPlanesU));
    auto *planeV = static_cast<uint8_t *>(env->GetDirectBufferAddress(yuvPlanesV));

    jobject yuvStridesObj = env->GetObjectField(output_buffer, jniContext->yuvStrides_field);
    auto yuvStridesArray = reinterpret_cast<jintArray>(yuvStridesObj);
    jint *yuvStrides = env->GetIntArrayElements(yuvStridesArray, nullptr);
    int strideY = yuvStrides[kPlaneY];
    int strideU = yuvStrides[kPlaneU];
    int strideV = yuvStrides[kPlaneV];

    const int uvHeight = (native_window_buffer.height + 1) / 2;
    auto *windowBits = static_cast<uint8_t *>(native_window_buffer.bits);
    const int windowUvStride = ALIGN(native_window_buffer.stride / 2, 16);
    const int vPlaneHeight = std::min(uvHeight, displayed_height);
    const int yPlaneSize = native_window_buffer.stride * native_window_buffer.height;
    const int vPlaneSize = vPlaneHeight * windowUvStride;

    uint8_t *src[3] = {planeY, planeU, planeV};
    int srcStride[3] = {strideY, strideU, strideV};

    uint8_t *dest[3] = {windowBits,
                        windowBits + yPlaneSize + vPlaneSize,
                        windowBits + yPlaneSize};
    int destStride[3] = {native_window_buffer.stride,
                          windowUvStride,
                          windowUvStride};

    sws_scale(jniContext->swsContext, src, srcStride, 0, displayed_height,
              dest, destStride);

    env->ReleaseIntArrayElements(yuvStridesArray, yuvStrides, 0);

    if (ANativeWindow_unlockAndPost(jniContext->native_window)) {
        LOGE("ANativeWindow_unlockAndPost failed");
        return VIDEO_DECODER_ERROR_OTHER;
    }
    return VIDEO_DECODER_SUCCESS;
}

extern "C"
JNIEXPORT jint JNICALL
Java_io_github_sakurafubuki_yume_nativelib_player_FfmpegVideoDecoder_ffmpegSendPacket(
        JNIEnv *env, jobject, jlong jContext, jobject encoded_data,
        jint length, jlong input_time) {
    auto *jniContext = reinterpret_cast<JniContext *>(jContext);
    AVCodecContext *avContext = jniContext->codecContext;

    auto *inputBuffer = static_cast<uint8_t *>(env->GetDirectBufferAddress(encoded_data));
    PacketGuard pg;
    if (!pg) return VIDEO_DECODER_ERROR_OTHER;
    pg.packet->data = inputBuffer;
    pg.packet->size = length;
    pg.packet->pts = input_time;

    int result = avcodec_send_packet(avContext, pg.packet);
    if (result) {
        logError("avcodec_send_packet", result);
        if (result == AVERROR_INVALIDDATA) return VIDEO_DECODER_ERROR_INVALID_DATA;
        if (result == AVERROR(EAGAIN)) return VIDEO_DECODER_ERROR_READ_FRAME;
        return VIDEO_DECODER_ERROR_OTHER;
    }
    return result;
}

extern "C"
JNIEXPORT jint JNICALL
Java_io_github_sakurafubuki_yume_nativelib_player_FfmpegVideoDecoder_ffmpegReceiveFrame(
        JNIEnv *env, jobject, jlong jContext, jint output_mode,
        jobject output_buffer, jboolean decode_only) {
    auto *jniContext = reinterpret_cast<JniContext *>(jContext);
    AVCodecContext *avContext = jniContext->codecContext;

    FrameGuard frame;
    if (!frame) {
        LOGE("Failed to allocate output frame.");
        return VIDEO_DECODER_ERROR_OTHER;
    }
    int result = avcodec_receive_frame(avContext, frame.frame);

    if (decode_only || result == AVERROR(EAGAIN)) {
        return VIDEO_DECODER_ERROR_INVALID_DATA;
    }
    if (result) {
        logError("avcodec_receive_frame", result);
        return VIDEO_DECODER_ERROR_OTHER;
    }

    env->CallVoidMethod(output_buffer, jniContext->init_method,
                        frame.frame->pts, output_mode, nullptr);

    jboolean initOk = env->CallBooleanMethod(
        output_buffer, jniContext->init_for_yuv_frame_method,
        frame.frame->width, frame.frame->height,
        frame.frame->linesize[0], frame.frame->linesize[1], 0);
    if (env->ExceptionCheck() || !initOk) {
        return VIDEO_DECODER_ERROR_OTHER;
    }

    jobject dataObj = env->GetObjectField(output_buffer, jniContext->data_field);
    auto *data = static_cast<uint8_t *>(env->GetDirectBufferAddress(dataObj));
    const int uvHeight = (frame.frame->height + 1) / 2;
    const size_t yLen = static_cast<size_t>(frame.frame->linesize[0]) * frame.frame->height;
    const size_t uvLen = static_cast<size_t>(frame.frame->linesize[1]) * uvHeight;

    memcpy(data, frame.frame->data[0], yLen);
    memcpy(data + yLen, frame.frame->data[1], uvLen);
    memcpy(data + yLen + uvLen, frame.frame->data[2], uvLen);

    return result;
}
