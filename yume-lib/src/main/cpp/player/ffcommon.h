#pragma once

#include <jni.h>
#include <android/log.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libswresample/swresample.h>
}

#define LOG_TAG "ffmpeg_jni"
#define LOGE(...) ((void)__android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__))
#define LOGD(...) ((void)__android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__))
#define ERROR_STRING_BUFFER_LENGTH 256

void releaseContext(AVCodecContext *context);
AVCodec *getCodecByName(JNIEnv *env, jstring codecName);
void logError(const char *functionName, int errorNumber);

// RAII wrappers for FFmpeg resources
struct FrameGuard {
    AVFrame *frame;
    explicit FrameGuard() : frame(av_frame_alloc()) {}
    ~FrameGuard() { if (frame) av_frame_free(&frame); }
    explicit operator bool() const { return frame != nullptr; }
};

struct PacketGuard {
    AVPacket *packet;
    explicit PacketGuard() : packet(av_packet_alloc()) {}
    ~PacketGuard() { if (packet) av_packet_free(&packet); }
    explicit operator bool() const { return packet != nullptr; }
};
