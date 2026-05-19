cmake_minimum_required(VERSION 3.16)
project("yume-lib")

set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -Wl,-z,max-page-size=16384")

if(ANDROID_ABI STREQUAL "armeabi-v7a")
    set(PREFIX_ARCH "armv7l")
elseif(ANDROID_ABI STREQUAL "arm64-v8a")
    set(PREFIX_ARCH "arm64")
elseif(ANDROID_ABI STREQUAL "x86")
    set(PREFIX_ARCH "x86")
elseif(ANDROID_ABI STREQUAL "x86_64")
    set(PREFIX_ARCH "x86_64")
else()
    message(FATAL_ERROR "Unsupported ABI: ${ANDROID_ABI}")
endif()

set(PREBUILTS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../../buildscripts/prefix/${PREFIX_ARCH}")
set(PREBUILTS_LIB "${PREBUILTS_DIR}/lib")
set(PREBUILTS_INCLUDE "${PREBUILTS_DIR}/include")

set(LIBYUV_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../../buildscripts/deps/libyuv")
if(EXISTS "${LIBYUV_SOURCE_DIR}/CMakeLists.txt")
    add_subdirectory("${LIBYUV_SOURCE_DIR}" libyuv_build)
else()
    message(WARNING "libyuv source not found at ${LIBYUV_SOURCE_DIR}. Run buildscripts/download.sh first.")
endif()

set(SND_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../../buildscripts/deps/soundtouch")
if(EXISTS "${SND_SOURCE_DIR}/CMakeLists.txt")
    set(SOUNDSTRETCH OFF CACHE BOOL "Build soundstretch utility" FORCE)
    set(SOUNDTOUCH_DLL ON CACHE BOOL "Build SoundTouchDLL (C wrapper)" FORCE)
    set(INTEGER_SAMPLES OFF CACHE BOOL "Use float samples" FORCE)
    add_subdirectory("${SND_SOURCE_DIR}" soundtouch_build)

    add_library(soundtouch_jni SHARED soundtouch_jni/soundtouch_jni.cpp)
    target_include_directories(soundtouch_jni PRIVATE
        "${SND_SOURCE_DIR}/source/SoundTouchDLL"
        "${SND_SOURCE_DIR}/include"
    )
    target_link_libraries(soundtouch_jni SoundTouchDLL android log)
    set_target_properties(soundtouch_jni PROPERTIES LIBRARY_OUTPUT_NAME soundtouch)
else()
    message(WARNING "soundtouch source not found at ${SND_SOURCE_DIR}. Run buildscripts/download.sh first.")
endif()

macro(import_ffmpeg_lib name)
    add_library(${name} SHARED IMPORTED)
    set_target_properties(${name} PROPERTIES
        IMPORTED_LOCATION "${PREBUILTS_LIB}/lib${name}.so"
    )
    target_include_directories(${name} INTERFACE "${PREBUILTS_INCLUDE}")
endmacro()

import_ffmpeg_lib(avcodec)
import_ffmpeg_lib(avformat)
import_ffmpeg_lib(avutil)
import_ffmpeg_lib(swresample)
import_ffmpeg_lib(swscale)

macro(import_static_lib name)
    add_library(${name} STATIC IMPORTED)
    set_target_properties(${name} PROPERTIES
        IMPORTED_LOCATION "${PREBUILTS_LIB}/lib${name}.a"
    )
    target_include_directories(${name} INTERFACE "${PREBUILTS_INCLUDE}")
endmacro()

import_static_lib(ass)
import_static_lib(harfbuzz)
import_static_lib(freetype)
import_static_lib(fribidi)
import_static_lib(fontconfig)
import_static_lib(xml2)
import_static_lib(unibreak)
add_library(yume_subtitle SHARED subtitle/ass_renderer.cpp)

target_include_directories(yume_subtitle PRIVATE "${PREBUILTS_INCLUDE}")

target_link_libraries(yume_subtitle
    ass harfbuzz freetype fribidi fontconfig xml2 unibreak
    GLESv3 EGL android log jnigraphics z m
)

add_library(yume_player SHARED
    player/ffmain.cpp
    player/ffcommon.cpp
    player/ffaudio.cpp
    player/ffvideo.cpp
)

target_include_directories(yume_player PRIVATE "${PREBUILTS_INCLUDE}")

target_link_libraries(yume_player
    avcodec avformat avutil swresample swscale
    android log
)

add_library(yume_mediainfo SHARED
    mediainfo/main.cpp
    mediainfo/mediainfo.cpp
    mediainfo/utils.cpp
    mediainfo/frame_extractor.cpp
    mediainfo/media_thumbnail_retriever.cpp
    mediainfo/frame_loader_context.cpp
)

target_include_directories(yume_mediainfo PRIVATE "${PREBUILTS_INCLUDE}")

target_link_libraries(yume_mediainfo
    avcodec avformat avutil swscale
    android log jnigraphics
)

if(TARGET yuv)
    add_library(yume_yuv SHARED yuv/yuv_to_bitmap.cpp)
    target_include_directories(yume_yuv PRIVATE "${LIBYUV_SOURCE_DIR}/include")
    target_link_libraries(yume_yuv yuv android log jnigraphics)
else()
    message(WARNING "libyuv target not found. Skipping yume_yuv.")
endif()
