#pragma once

#include <jni.h>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
}

struct FrameLoaderContext {
    AVFormatContext *avFormatContext;
    AVCodecParameters *parameters;
    const AVCodec *avVideoCodec;
    int videoStreamIndex;
};

FrameLoaderContext *frame_loader_context_from_handle(int64_t handle);
int64_t frame_loader_context_to_handle(FrameLoaderContext *frameLoaderContext);
void frame_loader_context_free(int64_t handle);
