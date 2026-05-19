#include <jni.h>
#include <cstdlib>
#include <algorithm>
#include "ffcommon.h"

extern "C" {
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
}

static const AVSampleFormat OUTPUT_FORMAT_PCM_16BIT = AV_SAMPLE_FMT_S16;
static const AVSampleFormat OUTPUT_FORMAT_PCM_FLOAT = AV_SAMPLE_FMT_FLT;

static const int AUDIO_DECODER_ERROR_INVALID_DATA = -1;
static const int AUDIO_DECODER_ERROR_OTHER = -2;

static jmethodID growOutputBufferMethod;

struct GrowOutputBufferCallback {
    uint8_t *operator()(int requiredSize) const;

    JNIEnv *env;
    jobject thiz;
    jobject decoderOutputBuffer;
};

uint8_t *GrowOutputBufferCallback::operator()(int requiredSize) const {
    jobject newOutputData = env->CallObjectMethod(thiz, growOutputBufferMethod,
                                                  decoderOutputBuffer, requiredSize);
    if (env->ExceptionCheck()) {
        LOGE("growOutputBuffer() failed");
        env->ExceptionDescribe();
        return nullptr;
    }
    return static_cast<uint8_t *>(env->GetDirectBufferAddress(newOutputData));
}

static AVCodecContext *createContext(JNIEnv *env, AVCodec *codec, jbyteArray extraData,
                                     jboolean outputFloat, jint rawSampleRate,
                                     jint rawChannelCount) {
    AVCodecContext *context = avcodec_alloc_context3(codec);
    if (!context) {
        LOGE("Failed to allocate context.");
        return nullptr;
    }
    context->request_sample_fmt =
        outputFloat ? OUTPUT_FORMAT_PCM_FLOAT : OUTPUT_FORMAT_PCM_16BIT;

    if (extraData) {
        jsize size = env->GetArrayLength(extraData);
        context->extradata_size = size;
        context->extradata =
            static_cast<uint8_t *>(av_malloc(size + AV_INPUT_BUFFER_PADDING_SIZE));
        if (!context->extradata) {
            LOGE("Failed to allocate extra data.");
            releaseContext(context);
            return nullptr;
        }
        env->GetByteArrayRegion(extraData, 0, size,
                                reinterpret_cast<jbyte *>(context->extradata));
    }
    if (context->codec_id == AV_CODEC_ID_PCM_MULAW ||
        context->codec_id == AV_CODEC_ID_PCM_ALAW) {
        context->sample_rate = rawSampleRate;
        context->ch_layout.nb_channels = rawChannelCount;
        av_channel_layout_default(&context->ch_layout, rawChannelCount);
    }
    context->err_recognition = AV_EF_IGNORE_ERR;
    int result = avcodec_open2(context, codec, nullptr);
    if (result < 0) {
        logError("avcodec_open2", result);
        releaseContext(context);
        return nullptr;
    }
    return context;
}

static int decodePacket(AVCodecContext *context, AVPacket *packet,
                        uint8_t *outputBuffer, int outputSize,
                        const GrowOutputBufferCallback &growBuffer) {
    int result = avcodec_send_packet(context, packet);
    if (result) {
        logError("avcodec_send_packet", result);
        return result == AVERROR_INVALIDDATA ? AUDIO_DECODER_ERROR_INVALID_DATA
                                              : AUDIO_DECODER_ERROR_OTHER;
    }

    int outSize = 0;
    while (true) {
        FrameGuard frame;
        if (!frame) {
            LOGE("Failed to allocate output frame.");
            return AUDIO_DECODER_ERROR_INVALID_DATA;
        }
        result = avcodec_receive_frame(context, frame.frame);
        if (result) {
            if (result == AVERROR(EAGAIN)) break;
            logError("avcodec_receive_frame", result);
            return result == AVERROR_INVALIDDATA ? AUDIO_DECODER_ERROR_INVALID_DATA
                                                  : AUDIO_DECODER_ERROR_OTHER;
        }

        AVSampleFormat sampleFormat = context->sample_fmt;
        int channelCount = context->ch_layout.nb_channels;
        int sampleRate = context->sample_rate;
        int sampleCount = frame.frame->nb_samples;

        SwrContext *resampleContext = static_cast<SwrContext *>(context->opaque);
        if (!resampleContext) {
            resampleContext = swr_alloc();
            av_opt_set_chlayout(resampleContext, "in_chlayout",
                               &context->ch_layout, 0);
            av_opt_set_chlayout(resampleContext, "out_chlayout",
                               &context->ch_layout, 0);
            av_opt_set_int(resampleContext, "in_sample_rate", sampleRate, 0);
            av_opt_set_int(resampleContext, "out_sample_rate", sampleRate, 0);
            av_opt_set_int(resampleContext, "in_sample_fmt", sampleFormat, 0);
            av_opt_set_int(resampleContext, "out_sample_fmt",
                          context->request_sample_fmt, 0);
            result = swr_init(resampleContext);
            if (result < 0) {
                logError("swr_init", result);
                return result == AVERROR_INVALIDDATA ? AUDIO_DECODER_ERROR_INVALID_DATA
                                                      : AUDIO_DECODER_ERROR_OTHER;
            }
            context->opaque = resampleContext;
        }

        int outSampleSize = av_get_bytes_per_sample(context->request_sample_fmt);
        int outSamples = swr_get_out_samples(resampleContext, sampleCount);
        int bufferOutSize = outSampleSize * channelCount * outSamples;

        if (outSize + bufferOutSize > outputSize) {
            LOGD("Output buffer realloc: %d -> %d", outputSize, outSize + bufferOutSize);
            outputSize = outSize + bufferOutSize;
            outputBuffer = growBuffer(outputSize);
            if (!outputBuffer) {
                LOGE("Failed to reallocate output buffer.");
                return AUDIO_DECODER_ERROR_OTHER;
            }
        }

        result = swr_convert(resampleContext, &outputBuffer, bufferOutSize,
                            const_cast<const uint8_t **>(frame.frame->data),
                            frame.frame->nb_samples);
        if (result < 0) {
            logError("swr_convert", result);
            return AUDIO_DECODER_ERROR_INVALID_DATA;
        }
        if (swr_get_out_samples(resampleContext, 0) != 0) {
            LOGE("Unexpected remaining samples after resampling.");
            return AUDIO_DECODER_ERROR_INVALID_DATA;
        }
        outputBuffer += bufferOutSize;
        outSize += bufferOutSize;
    }
    return outSize;
}


extern "C"
JNIEXPORT jlong JNICALL
Java_io_github_sakurafubuki_yume_nativelib_player_FfmpegAudioDecoder_ffmpegInitialize(
        JNIEnv *env, jobject thiz, jstring codec_name, jbyteArray extra_data,
        jboolean output_float, jint raw_sample_rate, jint raw_channel_count) {
    AVCodec *codec = getCodecByName(env, codec_name);
    if (!codec) {
        LOGE("Codec not found.");
        return 0L;
    }
    jclass clazz = env->FindClass(
        "io/github/sakurafubuki/yume/nativelib/player/FfmpegAudioDecoder");
    growOutputBufferMethod = env->GetMethodID(
        clazz, "growOutputBuffer",
        "(Landroidx/media3/decoder/SimpleDecoderOutputBuffer;I)Ljava/nio/ByteBuffer;");
    return reinterpret_cast<jlong>(createContext(env, codec, extra_data,
                                                  output_float, raw_sample_rate,
                                                  raw_channel_count));
}

extern "C"
JNIEXPORT jint JNICALL
Java_io_github_sakurafubuki_yume_nativelib_player_FfmpegAudioDecoder_ffmpegDecode(
        JNIEnv *env, jobject thiz, jlong context, jobject input_data,
        jint input_size, jobject decoderOutputBuffer,
        jobject output_data, jint output_size) {
    if (!context || !input_data || !output_data) {
        LOGE("NULL argument in ffmpegDecode.");
        return -1;
    }
    if (input_size < 0 || output_size < 0) {
        LOGE("Invalid buffer size: input=%d output=%d", input_size, output_size);
        return -1;
    }
    auto *inputBuffer = static_cast<uint8_t *>(env->GetDirectBufferAddress(input_data));
    auto *outputBuffer = static_cast<uint8_t *>(env->GetDirectBufferAddress(output_data));

    PacketGuard packet;
    if (!packet) {
        LOGE("av_packet_alloc failed");
        return -1;
    }
    packet.packet->data = inputBuffer;
    packet.packet->size = input_size;

    return decodePacket(reinterpret_cast<AVCodecContext *>(context),
                       packet.packet, outputBuffer, output_size,
                       GrowOutputBufferCallback{env, thiz, decoderOutputBuffer});
}

extern "C"
JNIEXPORT jint JNICALL
Java_io_github_sakurafubuki_yume_nativelib_player_FfmpegAudioDecoder_ffmpegGetChannelCount(
        JNIEnv *, jobject, jlong context) {
    return context ? reinterpret_cast<AVCodecContext *>(context)->ch_layout.nb_channels : -1;
}

extern "C"
JNIEXPORT jint JNICALL
Java_io_github_sakurafubuki_yume_nativelib_player_FfmpegAudioDecoder_ffmpegGetSampleRate(
        JNIEnv *, jobject, jlong context) {
    return context ? reinterpret_cast<AVCodecContext *>(context)->sample_rate : -1;
}

extern "C"
JNIEXPORT jlong JNICALL
Java_io_github_sakurafubuki_yume_nativelib_player_FfmpegAudioDecoder_ffmpegReset(
        JNIEnv *env, jobject, jlong jContext, jbyteArray extra_data) {
    auto *context = reinterpret_cast<AVCodecContext *>(jContext);
    if (!context) {
        LOGE("Tried to reset without a context.");
        return 0L;
    }

    AVCodecID codecId = context->codec_id;
    if (codecId == AV_CODEC_ID_TRUEHD) {
        releaseContext(context);
        auto *codec = const_cast<AVCodec *>(avcodec_find_decoder(codecId));
        if (!codec) {
            LOGE("Unexpected error finding codec %d.", static_cast<int>(codecId));
            return 0L;
        }
        auto outputFloat = static_cast<jboolean>(
            context->request_sample_fmt == OUTPUT_FORMAT_PCM_FLOAT);
        return reinterpret_cast<jlong>(createContext(env, codec, extra_data,
                                                      outputFloat, -1, -1));
    }

    avcodec_flush_buffers(context);
    return reinterpret_cast<jlong>(context);
}

extern "C"
JNIEXPORT void JNICALL
Java_io_github_sakurafubuki_yume_nativelib_player_FfmpegAudioDecoder_ffmpegRelease(
        JNIEnv *, jobject, jlong context) {
    if (context) releaseContext(reinterpret_cast<AVCodecContext *>(context));
}
