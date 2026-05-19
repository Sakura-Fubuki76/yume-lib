#include <jni.h>
#include <android/bitmap.h>
#include <libyuv.h>
#include <android/log.h>
#include <cstddef>
#include <cstring>
#include <vector>

#define TAG "YuvToBitmap"

#define LOGD(...) ((void)0)
#define LOGE(...) ((void)0)

enum {
    COLOR_STANDARD_BT709     = 1,
    COLOR_STANDARD_BT601_PAL = 2,
    COLOR_STANDARD_BT601_NTSC = 4,
    COLOR_STANDARD_BT2020    = 6,
};

enum {
    COLOR_RANGE_FULL    = 1,
    COLOR_RANGE_LIMITED = 2,
};

static const libyuv::YuvConstants* selectYuvMatrix(jint colorStandard, jint colorRange) {
    const bool fullRange = (colorRange == COLOR_RANGE_FULL);
    switch (colorStandard) {
        case COLOR_STANDARD_BT601_PAL:
        case COLOR_STANDARD_BT601_NTSC:
            return fullRange ? &libyuv::kYuvJPEGConstants : &libyuv::kYuvI601Constants;
        case COLOR_STANDARD_BT2020:
            return fullRange ? &libyuv::kYuvV2020Constants : &libyuv::kYuv2020Constants;
        case COLOR_STANDARD_BT709:
        default:
            return fullRange ? &libyuv::kYuvF709Constants : &libyuv::kYuvH709Constants;
    }
}

static jobject createArgbBitmap(JNIEnv* env, jint width, jint height) {
    jclass bitmapClass = env->FindClass("android/graphics/Bitmap");
    if (!bitmapClass) return nullptr;

    jmethodID createBitmap = env->GetStaticMethodID(
        bitmapClass, "createBitmap",
        "(IILandroid/graphics/Bitmap$Config;)Landroid/graphics/Bitmap;");
    if (!createBitmap) return nullptr;

    jclass configClass = env->FindClass("android/graphics/Bitmap$Config");
    if (!configClass) return nullptr;

    jfieldID argb8888Field = env->GetStaticFieldID(
        configClass, "ARGB_8888", "Landroid/graphics/Bitmap$Config;");
    if (!argb8888Field) return nullptr;

    jobject config = env->GetStaticObjectField(configClass, argb8888Field);
    if (!config) return nullptr;

    return env->CallStaticObjectMethod(bitmapClass, createBitmap, width, height, config);
}

struct BitmapLock {
    JNIEnv* env;
    jobject bitmap;
    void* pixels;

    BitmapLock(JNIEnv* e, jobject b) : env(e), bitmap(b), pixels(nullptr) {
        if (AndroidBitmap_lockPixels(env, bitmap, &pixels) != ANDROID_BITMAP_RESULT_SUCCESS) {
            pixels = nullptr;
        }
    }

    ~BitmapLock() {
        if (pixels) {
            AndroidBitmap_unlockPixels(env, bitmap);
        }
    }

    bool ok() const { return pixels != nullptr; }
};

extern "C" JNIEXPORT jobject JNICALL
Java_com_sakurafubuki_yume_core_data_repository_YuvToBitmapBridge_imageToBitmap(
    JNIEnv* env, jclass ,
    jobject yBuf, jint yRowStride, jint yPixelStride,
    jobject uBuf, jint uRowStride, jint uPixelStride,
    jobject vBuf, jint vRowStride, jint vPixelStride,
    jint cropLeft, jint cropTop, jint cropWidth, jint cropHeight,
    jint colorStandard, jint colorRange,
    jboolean forceNV21)
{
    if (cropWidth <= 0 || cropHeight <= 0) return nullptr;

    auto* yPtr = static_cast<uint8_t*>(env->GetDirectBufferAddress(yBuf));
    auto* uPtr = static_cast<uint8_t*>(env->GetDirectBufferAddress(uBuf));
    auto* vPtr = static_cast<uint8_t*>(env->GetDirectBufferAddress(vBuf));

    if (!yPtr || !uPtr || !vPtr) {
        LOGE("imageToBitmap: one or more planes are not direct buffers");
        return nullptr;
    }

    LOGD("imageToBitmap: yPixel=%d uPixel=%d uRow=%d vRow=%d crop=%dx%d std=%d range=%d"
         " vu_off=%td forceNV21=%d",
         yPixelStride, uPixelStride, uRowStride, vRowStride,
         cropWidth, cropHeight, colorStandard, colorRange,
         static_cast<ptrdiff_t>(vPtr - uPtr), forceNV21);

    const uint8_t* srcY = yPtr + cropTop * yRowStride + cropLeft * yPixelStride;

    jobject bitmap = createArgbBitmap(env, cropWidth, cropHeight);
    if (!bitmap) {
        LOGE("imageToBitmap: failed to create Bitmap");
        return nullptr;
    }

    const auto* matrix = selectYuvMatrix(colorStandard, colorRange);
    {
        BitmapLock lock(env, bitmap);
        if (!lock.ok()) {
            LOGE("imageToBitmap: failed to lock bitmap pixels");
            env->DeleteLocalRef(bitmap);
            return nullptr;
        }

        auto* dstPixels = static_cast<uint8_t*>(lock.pixels);
        const int dstStride = cropWidth * 4;
        int result = -1;

        if (uPixelStride == 2 && uRowStride == vRowStride) {

            const uint8_t* srcUV = uPtr + (cropTop / 2) * uRowStride
                                         + (cropLeft & ~1);
            if (forceNV21) {
                result = libyuv::NV21ToARGBMatrix(
                    srcY, yRowStride,
                    srcUV, uRowStride,
                    dstPixels, dstStride,
                    matrix,
                    cropWidth, cropHeight);
                if (result != 0) {
                    LOGE("imageToBitmap: NV21ToARGBMatrix failed (code %d)", result);
                }
            } else {
                result = libyuv::NV12ToARGBMatrix(
                    srcY, yRowStride,
                    srcUV, uRowStride,
                    dstPixels, dstStride,
                    matrix,
                    cropWidth, cropHeight);
                if (result != 0) {
                    LOGE("imageToBitmap: NV12ToARGBMatrix failed (code %d)", result);
                }
            }
        } else {

            const uint8_t* srcU = uPtr + (cropTop / 2) * uRowStride
                                        + (cropLeft / 2) * uPixelStride;
            const uint8_t* srcV = vPtr + (cropTop / 2) * vRowStride
                                        + (cropLeft / 2) * vPixelStride;
            result = libyuv::I420ToARGBMatrix(
                srcY, yRowStride,
                srcU, uRowStride,
                srcV, vRowStride,
                dstPixels, dstStride,
                matrix,
                cropWidth, cropHeight);

            if (result != 0) {
                LOGE("imageToBitmap: I420ToARGBMatrix failed with code %d (std=%d range=%d)",
                     result, colorStandard, colorRange);
            }
        }

        if (result != 0) {
            env->DeleteLocalRef(bitmap);
            return nullptr;
        }
    }

    return bitmap;
}

extern "C" JNIEXPORT jobject JNICALL
Java_com_sakurafubuki_yume_core_data_repository_YuvToBitmapBridge_bufferToBitmap(
    JNIEnv* env, jclass ,
    jobject yuvBuffer, jint offset,
    jint colorFormat, jint stride, jint sliceHeight,
    jint cropLeft, jint cropTop, jint cropWidth, jint cropHeight,
    jint colorStandard, jint colorRange,
    jboolean forceNV21)
{
    if (cropWidth <= 0 || cropHeight <= 0) return nullptr;

    auto* data = static_cast<uint8_t*>(env->GetDirectBufferAddress(yuvBuffer));
    if (!data) {
        LOGE("bufferToBitmap: buffer is not a direct buffer");
        return nullptr;
    }
    data += offset;

    jobject bitmap = createArgbBitmap(env, cropWidth, cropHeight);
    if (!bitmap) {
        LOGE("bufferToBitmap: failed to create Bitmap");
        return nullptr;
    }

    const auto* matrix = selectYuvMatrix(colorStandard, colorRange);
    int result = -1;
    {
        BitmapLock lock(env, bitmap);
        if (!lock.ok()) {
            LOGE("bufferToBitmap: failed to lock bitmap pixels");
            env->DeleteLocalRef(bitmap);
            return nullptr;
        }

        auto* dstPixels = static_cast<uint8_t*>(lock.pixels);
        const int dstStride = cropWidth * 4;

        switch (colorFormat) {

            case 21:
            case 39:
            case 0x7F000100:
            case 0x7F420888:
            {
                const uint8_t* yPtr = data + cropTop * stride + cropLeft;
                const uint8_t* uvPtr = data + stride * sliceHeight
                                       + (cropTop / 2) * stride
                                       + (cropLeft & ~1);
                if (forceNV21) {
                    result = libyuv::NV21ToARGBMatrix(yPtr, stride,
                                                       uvPtr, stride,
                                                       dstPixels, dstStride,
                                                       matrix,
                                                       cropWidth, cropHeight);
                } else {
                    result = libyuv::NV12ToARGBMatrix(yPtr, stride,
                                                       uvPtr, stride,
                                                       dstPixels, dstStride,
                                                       matrix,
                                                       cropWidth, cropHeight);
                }
                if (result != 0) {

                    const int chromaStride = stride / 2;
                    const int chromaSliceH = sliceHeight / 2;
                    const uint8_t* uPtr = data + stride * sliceHeight
                                          + (cropTop / 2) * chromaStride
                                          + (cropLeft / 2);
                    const uint8_t* vPtr = uPtr + chromaStride * chromaSliceH;
                    result = libyuv::I420ToARGBMatrix(yPtr, stride,
                                                       uPtr, chromaStride,
                                                       vPtr, chromaStride,
                                                       dstPixels, dstStride,
                                                       matrix,
                                                       cropWidth, cropHeight);
                }
                break;
            }

            case 19:
            case 20:
            {
                const int chromaStride = stride / 2;
                const int chromaSliceH = sliceHeight / 2;
                const uint8_t* yPtr = data + cropTop * stride + cropLeft;
                const uint8_t* uPtr = data + stride * sliceHeight
                                      + (cropTop / 2) * chromaStride
                                      + (cropLeft / 2);
                const uint8_t* vPtr = uPtr + chromaStride * chromaSliceH;
                result = libyuv::I420ToARGBMatrix(yPtr, stride,
                                                   uPtr, chromaStride,
                                                   vPtr, chromaStride,
                                                   dstPixels, dstStride,
                                                   matrix,
                                                   cropWidth, cropHeight);
                break;
            }

            default:
                LOGE("bufferToBitmap: unsupported color format 0x%X", colorFormat);
                break;
        }
    }

    if (result != 0) {
        LOGE("bufferToBitmap: libyuv conversion failed with code %d (fmt=0x%X std=%d range=%d)",
             result, colorFormat, colorStandard, colorRange);
        env->DeleteLocalRef(bitmap);
        return nullptr;
    }

    return bitmap;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_sakurafubuki_yume_core_data_repository_YuvToBitmapBridge_i420Scale(
    JNIEnv* env, jclass,
    jobject srcY, jint srcStrideY,
    jobject srcU, jint srcStrideU,
    jobject srcV, jint srcStrideV,
    jint srcWidth, jint srcHeight,
    jobject dstY, jint dstStrideY,
    jobject dstU, jint dstStrideU,
    jobject dstV, jint dstStrideV,
    jint dstWidth, jint dstHeight,
    jint filterMode)
{
    auto* pSrcY = static_cast<uint8_t*>(env->GetDirectBufferAddress(srcY));
    auto* pSrcU = static_cast<uint8_t*>(env->GetDirectBufferAddress(srcU));
    auto* pSrcV = static_cast<uint8_t*>(env->GetDirectBufferAddress(srcV));
    auto* pDstY = static_cast<uint8_t*>(env->GetDirectBufferAddress(dstY));
    auto* pDstU = static_cast<uint8_t*>(env->GetDirectBufferAddress(dstU));
    auto* pDstV = static_cast<uint8_t*>(env->GetDirectBufferAddress(dstV));

    if (!pSrcY || !pSrcU || !pSrcV || !pDstY || !pDstU || !pDstV) {
        LOGE("i420Scale: one or more planes are not direct buffers");
        return JNI_FALSE;
    }

    int ret = libyuv::I420Scale(
        pSrcY, srcStrideY,
        pSrcU, srcStrideU,
        pSrcV, srcStrideV,
        srcWidth, srcHeight,
        pDstY, dstStrideY,
        pDstU, dstStrideU,
        pDstV, dstStrideV,
        dstWidth, dstHeight,
        static_cast<libyuv::FilterMode>(filterMode));

    if (ret != 0) {
        LOGE("i420Scale: libyuv I420Scale failed with code %d (%dx%d -> %dx%d filter=%d)",
             ret, srcWidth, srcHeight, dstWidth, dstHeight, filterMode);
        return JNI_FALSE;
    }
    return JNI_TRUE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_sakurafubuki_yume_core_data_repository_YuvToBitmapBridge_nv12ScaleToI420(
    JNIEnv* env, jclass,
    jobject srcY, jint srcStrideY,
    jobject srcUV, jint srcStrideUV,
    jint srcWidth, jint srcHeight,
    jobject dstY, jint dstStrideY,
    jobject dstU, jint dstStrideU,
    jobject dstV, jint dstStrideV,
    jint dstWidth, jint dstHeight,
    jint filterMode,
    jboolean forceNV21)
{
    auto* pSrcY  = static_cast<uint8_t*>(env->GetDirectBufferAddress(srcY));
    auto* pSrcUV = static_cast<uint8_t*>(env->GetDirectBufferAddress(srcUV));
    auto* pDstY  = static_cast<uint8_t*>(env->GetDirectBufferAddress(dstY));
    auto* pDstU  = static_cast<uint8_t*>(env->GetDirectBufferAddress(dstU));
    auto* pDstV  = static_cast<uint8_t*>(env->GetDirectBufferAddress(dstV));

    if (!pSrcY || !pSrcUV || !pDstY || !pDstU || !pDstV) {
        LOGE("nv12ScaleToI420: one or more planes are not direct buffers");
        return JNI_FALSE;
    }

    const int tmpYSize  = dstStrideY * dstHeight;
    const int tmpUVSize = dstStrideY * ((dstHeight + 1) / 2);
    std::vector<uint8_t> tmpY(tmpYSize);
    std::vector<uint8_t> tmpUV(tmpUVSize);

    int ret = libyuv::NV12Scale(
        pSrcY, srcStrideY,
        pSrcUV, srcStrideUV,
        srcWidth, srcHeight,
        tmpY.data(), dstStrideY,
        tmpUV.data(), dstStrideY,
        dstWidth, dstHeight,
        static_cast<libyuv::FilterMode>(filterMode));

    if (ret != 0) {
        LOGE("nv12ScaleToI420: NV12Scale failed %d (%dx%d -> %dx%d)", ret, srcWidth, srcHeight, dstWidth, dstHeight);
        return JNI_FALSE;
    }

    if (forceNV21) {
        ret = libyuv::NV21ToI420(
            tmpY.data(), dstStrideY,
            tmpUV.data(), dstStrideY,
            pDstY, dstStrideY,
            pDstU, dstStrideU,
            pDstV, dstStrideV,
            dstWidth, dstHeight);
    } else {
        ret = libyuv::NV12ToI420(
            tmpY.data(), dstStrideY,
            tmpUV.data(), dstStrideY,
            pDstY, dstStrideY,
            pDstU, dstStrideU,
            pDstV, dstStrideV,
            dstWidth, dstHeight);
    }

    if (ret != 0) {
        LOGE("nv12ScaleToI420: %s failed %d", forceNV21 ? "NV21ToI420" : "NV12ToI420", ret);
        return JNI_FALSE;
    }
    return JNI_TRUE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_sakurafubuki_yume_core_data_repository_YuvToBitmapBridge_compositeToSheet(
    JNIEnv* env, jclass,
    jobject frameBitmap,
    jobject sheetBitmap,
    jint col, jint row,
    jint frameWidth, jint frameHeight,
    jint cols)
{
    AndroidBitmapInfo frameInfo = {};
    AndroidBitmapInfo sheetInfo = {};
    if (AndroidBitmap_getInfo(env, frameBitmap, &frameInfo) < 0) {
        LOGE("compositeToSheet: cannot get frame bitmap info");
        return JNI_FALSE;
    }
    if (AndroidBitmap_getInfo(env, sheetBitmap, &sheetInfo) < 0) {
        LOGE("compositeToSheet: cannot get sheet bitmap info");
        return JNI_FALSE;
    }

    if (frameInfo.format != ANDROID_BITMAP_FORMAT_RGBA_8888 ||
        sheetInfo.format != ANDROID_BITMAP_FORMAT_RGBA_8888) {
        LOGE("compositeToSheet: both bitmaps must be ARGB_8888");
        return JNI_FALSE;
    }

    uint8_t* framePixels = nullptr;
    uint8_t* sheetPixels = nullptr;
    if (AndroidBitmap_lockPixels(env, frameBitmap, reinterpret_cast<void**>(&framePixels)) < 0) {
        LOGE("compositeToSheet: cannot lock frame bitmap");
        return JNI_FALSE;
    }
    if (AndroidBitmap_lockPixels(env, sheetBitmap, reinterpret_cast<void**>(&sheetPixels)) < 0) {
        LOGE("compositeToSheet: cannot lock sheet bitmap");
        AndroidBitmap_unlockPixels(env, frameBitmap);
        return JNI_FALSE;
    }

    const int srcStride = frameInfo.stride;
    const int dstStride = sheetInfo.stride;
    const int bytesPerRow = frameWidth * 4;
    const int dstX = col * frameWidth * 4;
    const int dstY = row * frameHeight;

    for (int y = 0; y < frameHeight; y++) {
        memcpy(sheetPixels + (dstY + y) * dstStride + dstX,
               framePixels + y * srcStride,
               bytesPerRow);
    }

    AndroidBitmap_unlockPixels(env, sheetBitmap);
    AndroidBitmap_unlockPixels(env, frameBitmap);
    return JNI_TRUE;
}

extern "C" JNIEXPORT jobject JNICALL
Java_com_sakurafubuki_yume_core_data_repository_YuvToBitmapBridge_argbScale(
    JNIEnv* env, jclass,
    jobject srcBitmap,
    jint dstWidth, jint dstHeight,
    jint filterMode)
{
    if (dstWidth <= 0 || dstHeight <= 0) return nullptr;

    AndroidBitmapInfo srcInfo = {};
    if (AndroidBitmap_getInfo(env, srcBitmap, &srcInfo) != ANDROID_BITMAP_RESULT_SUCCESS)
        return nullptr;

    BitmapLock srcLock(env, srcBitmap);
    if (!srcLock.ok()) return nullptr;

    jobject dstBitmap = createArgbBitmap(env, dstWidth, dstHeight);
    if (!dstBitmap) return nullptr;

    BitmapLock dstLock(env, dstBitmap);
    if (!dstLock.ok()) {
        env->DeleteLocalRef(dstBitmap);
        return nullptr;
    }

    auto filter = static_cast<libyuv::FilterMode>(filterMode);
    int result = libyuv::ARGBScale(
        static_cast<uint8_t*>(srcLock.pixels), srcInfo.stride,
        srcInfo.width, srcInfo.height,
        static_cast<uint8_t*>(dstLock.pixels), dstWidth * 4,
        dstWidth, dstHeight,
        filter);

    if (result != 0) {
        LOGE("argbScale: ARGBScale failed %d (%dx%d -> %dx%d filter=%d)",
             result, srcInfo.width, srcInfo.height, dstWidth, dstHeight, filterMode);
        env->DeleteLocalRef(dstBitmap);
        return nullptr;
    }
    return dstBitmap;
}
