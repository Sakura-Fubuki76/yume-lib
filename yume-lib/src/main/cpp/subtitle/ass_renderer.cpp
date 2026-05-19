#include <jni.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <android/log.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <cstring>
#include <cstdlib>
#include <mutex>
#include <dirent.h>
#include <sys/stat.h>
#include <vector>

#define LOG_TAG "AssRendererJNI"
#ifdef NDEBUG
#define LOGD(...) ((void)0)
#else
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
#endif
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

#if __has_include(<ass/ass.h>)
#include <ass/ass.h>
#define HAS_LIBASS 1
#else
#define HAS_LIBASS 0
#define YCBCR_NONE 2
struct ASS_Library {};
struct ASS_Renderer {};
struct ASS_Track { int YCbCrMatrix; };
struct ASS_Image {
    int w, h, stride, dst_x, dst_y;
    uint32_t color;
    uint8_t* bitmap;
    ASS_Image* next;
};
#endif

static const char* kVertSrc = R"(#version 300 es
layout(location = 0) in vec2 a_pos;
layout(location = 1) in vec2 a_uv;
layout(location = 2) in vec2 a_offset;
layout(location = 3) in vec2 a_size;
layout(location = 4) in vec4 a_color;
layout(location = 5) in float a_layer;
layout(location = 6) in vec2 a_uv_scale;

uniform vec2 u_resolution;

out vec2 v_uv;
out vec4 v_color;
flat out float v_layer;

void main() {
    vec2 pos = a_pos * a_size + a_offset;
    vec2 clip = (pos / u_resolution) * 2.0 - 1.0;
    gl_Position = vec4(clip.x, -clip.y, 0.0, 1.0);
    v_uv = a_uv * a_uv_scale;
    v_color = a_color;
    v_layer = a_layer;
}
)";

static const char* kFragSrc = R"(#version 300 es
precision mediump float;

uniform sampler2DArray u_tex;

in vec2 v_uv;
in vec4 v_color;
flat in float v_layer;

out vec4 frag_color;

void main() {
    float mask = texture(u_tex, vec3(v_uv, v_layer)).r;
    float alpha = v_color.a * mask;
    frag_color = vec4(v_color.rgb * alpha, alpha);
}
)";

static GLuint compileShader(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char buf[512];
        glGetShaderInfoLog(s, sizeof(buf), nullptr, buf);
        LOGE("Shader compile error: %s", buf);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

static GLuint buildProgram(const char* vs, const char* fs) {
    GLuint v = compileShader(GL_VERTEX_SHADER,   vs);
    GLuint f = compileShader(GL_FRAGMENT_SHADER, fs);
    if (!v || !f) { glDeleteShader(v); glDeleteShader(f); return 0; }
    GLuint p = glCreateProgram();
    glAttachShader(p, v); glAttachShader(p, f);
    glLinkProgram(p);
    glDeleteShader(v); glDeleteShader(f);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char buf[512];
        glGetProgramInfoLog(p, sizeof(buf), nullptr, buf);
        LOGE("Program link error: %s", buf);
        glDeleteProgram(p);
        return 0;
    }
    return p;
}

struct PboRing {
    GLuint  ids[2]   = {0, 0};
    GLsizei capacity = 0;
    int     writeIdx = 0;

    bool init() {
        glGenBuffers(2, ids);
        if (!ids[0] || !ids[1]) { LOGE("glGenBuffers for PBO failed"); return false; }
        return true;
    }

    void ensureCapacity(GLsizei needed) {
        if (needed <= capacity) return;
        GLsizei newCap = ((needed + 65535) / 65536) * 65536;
        for (int i = 0; i < 2; ++i) {
            glBindBuffer(GL_PIXEL_UNPACK_BUFFER, ids[i]);
            glBufferData(GL_PIXEL_UNPACK_BUFFER, newCap, nullptr, GL_STREAM_DRAW);
        }
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
        capacity = newCap;
        LOGD("PBO capacity -> %d bytes", newCap);
    }

    uint8_t* mapWrite(GLsizei bytes) {
        ensureCapacity(bytes);
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, ids[writeIdx]);
        auto* ptr = static_cast<uint8_t*>(
            glMapBufferRange(GL_PIXEL_UNPACK_BUFFER, 0, bytes,
                             GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT));
        if (!ptr) {
            glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
            return nullptr;
        }
        return ptr;
    }

    static void unmap() { glUnmapBuffer(GL_PIXEL_UNPACK_BUFFER); }

    void swap() {
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
        writeIdx ^= 1;
    }

    void clear() {
        if (ids[0]) { glDeleteBuffers(2, ids); ids[0] = ids[1] = 0; capacity = 0; }
    }
};

struct EglState {
    EGLDisplay display = EGL_NO_DISPLAY;
    EGLContext context  = EGL_NO_CONTEXT;
    EGLSurface surface  = EGL_NO_SURFACE;
    ANativeWindow* window = nullptr;

    [[nodiscard]] bool valid() const {
        return display != EGL_NO_DISPLAY
            && context  != EGL_NO_CONTEXT
            && surface  != EGL_NO_SURFACE;
    }
};

#if HAS_LIBASS
static void scan_fonts_dir(ASS_Library* lib, const char* path) {
    DIR* dir = opendir(path);
    if (!dir) { LOGE("Cannot open fonts dir: %s", path); return; }
    ass_set_fonts_dir(lib, path);
    LOGD("Registered fonts dir: %s", path);
    struct dirent* e;
    while ((e = readdir(dir)) != nullptr) {
        if (e->d_name[0] == '.') continue;
        size_t pl = strlen(path), nl = strlen(e->d_name);
        char* full = (char*)malloc(pl + nl + 2);
        if (!full) continue;
        snprintf(full, pl + nl + 2, "%s/%s", path, e->d_name);
        struct stat st = {};
        if (stat(full, &st) == 0 && S_ISDIR(st.st_mode)) scan_fonts_dir(lib, full);
        free(full);
    }
    closedir(dir);
}
#endif

struct AssContext {
    ASS_Library*  lib      = nullptr;
    ASS_Renderer* renderer = nullptr;
    ASS_Track*    track    = nullptr;
    int width    = 0, height   = 0;
    int ycbcrMatrix = 0;
    char* fontconfigPath = nullptr;
    std::mutex renderMutex;

    EglState egl;

    GLuint glProgram     = 0;
    GLuint glVao         = 0;
    GLuint glVboQuad     = 0;
    GLuint glVboInstance = 0;
    GLuint glTexArray    = 0;
    int    texW = 0, texH = 0, texLayers = 0;
    int    glInstanceCapacity = 0;

    GLint  locRes = -1;
    GLint  locTex = -1;

    PboRing pboRing;

    std::vector<const ASS_Image*> frameImages;
    std::vector<uint8_t> fallbackBuf;

    AssContext() = default;
    ~AssContext() {
#if HAS_LIBASS
        if (track)    ass_free_track(track);
        if (renderer) ass_renderer_done(renderer);
        if (lib)      ass_library_done(lib);
#endif
        destroyGL();
        free(fontconfigPath);
    }

    bool initEGL(ANativeWindow* win) {
        egl.display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
        if (egl.display == EGL_NO_DISPLAY) { LOGE("eglGetDisplay failed"); return false; }
        if (!eglInitialize(egl.display, nullptr, nullptr)) { LOGE("eglInitialize failed"); return false; }

        EGLint attribs[] = {
            EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
            EGL_SURFACE_TYPE,    EGL_WINDOW_BIT,
            EGL_RED_SIZE,   8, EGL_GREEN_SIZE, 8,
            EGL_BLUE_SIZE,  8, EGL_ALPHA_SIZE, 8,
            EGL_NONE
        };
        EGLConfig cfg; EGLint numCfg = 0;
        if (!eglChooseConfig(egl.display, attribs, &cfg, 1, &numCfg) || numCfg == 0) {
            LOGE("eglChooseConfig failed"); return false;
        }

        ANativeWindow_setBuffersGeometry(win, 0, 0, WINDOW_FORMAT_RGBA_8888);

        egl.surface = eglCreateWindowSurface(egl.display, cfg, win, nullptr);
        if (egl.surface == EGL_NO_SURFACE) { LOGE("eglCreateWindowSurface failed"); return false; }

        EGLint ctxAttribs[] = { EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE };
        egl.context = eglCreateContext(egl.display, cfg, EGL_NO_CONTEXT, ctxAttribs);
        if (egl.context == EGL_NO_CONTEXT) { LOGE("eglCreateContext failed"); return false; }

        if (!eglMakeCurrent(egl.display, egl.surface, egl.surface, egl.context)) {
            LOGE("eglMakeCurrent failed"); return false;
        }
        egl.window = win;

        eglSwapInterval(egl.display, 1);

        LOGD("EGL/GLES3 OK");
        return true;
    }

    bool initGLResources() {
        glProgram = buildProgram(kVertSrc, kFragSrc);
        if (!glProgram) return false;

        locRes = glGetUniformLocation(glProgram, "u_resolution");
        locTex = glGetUniformLocation(glProgram, "u_tex");

        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

        glGenVertexArrays(1, &glVao);
        glBindVertexArray(glVao);

        const float quad[24] = {
            0.f, 0.f,  0.f, 0.f,
            1.f, 0.f,  1.f, 0.f,
            0.f, 1.f,  0.f, 1.f,
            1.f, 0.f,  1.f, 0.f,
            1.f, 1.f,  1.f, 1.f,
            0.f, 1.f,  0.f, 1.f,
        };
        glGenBuffers(1, &glVboQuad);
        glBindBuffer(GL_ARRAY_BUFFER, glVboQuad);
        glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);

        constexpr GLsizei quadStride = 4 * sizeof(float);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, quadStride, nullptr);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, quadStride,
                              reinterpret_cast<void*>(2 * sizeof(float)));

        glGenBuffers(1, &glVboInstance);
        glBindBuffer(GL_ARRAY_BUFFER, glVboInstance);
        glInstanceCapacity = 256;
        glBufferData(GL_ARRAY_BUFFER, glInstanceCapacity * 11 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);

        constexpr GLsizei instStride = 11 * sizeof(float);
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, instStride, nullptr);
        glVertexAttribDivisor(2, 1);
        glEnableVertexAttribArray(3);
        glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, instStride,
                              reinterpret_cast<void*>(2 * sizeof(float)));
        glVertexAttribDivisor(3, 1);
        glEnableVertexAttribArray(4);
        glVertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, instStride,
                              reinterpret_cast<void*>(4 * sizeof(float)));
        glVertexAttribDivisor(4, 1);
        glEnableVertexAttribArray(5);
        glVertexAttribPointer(5, 1, GL_FLOAT, GL_FALSE, instStride,
                              reinterpret_cast<void*>(8 * sizeof(float)));
        glVertexAttribDivisor(5, 1);
        glEnableVertexAttribArray(6);
        glVertexAttribPointer(6, 2, GL_FLOAT, GL_FALSE, instStride,
                              reinterpret_cast<void*>(9 * sizeof(float)));
        glVertexAttribDivisor(6, 1);

        glBindVertexArray(0);
        glBindBuffer(GL_ARRAY_BUFFER, 0);

        if (!pboRing.init()) return false;

        GLenum err = glGetError();
        if (err != GL_NO_ERROR) LOGD("GL init: glGetError=0x%x", err);

        LOGD("GLES3 resources OK: program=%u vao=%u", glProgram, glVao);
        return true;
    }

    void ensureTexArray(int w, int h, int layers) {
        if (w <= texW && h <= texH && layers <= texLayers) {
            glBindTexture(GL_TEXTURE_2D_ARRAY, glTexArray);
            return;
        }
        if (glTexArray) glDeleteTextures(1, &glTexArray);

        texW = w; texH = h; texLayers = layers;
        glGenTextures(1, &glTexArray);
        glBindTexture(GL_TEXTURE_2D_ARRAY, glTexArray);
        glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_R8,
                     texW, texH, texLayers, 0,
                     GL_RED, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }

    void destroyGLResources() {
        pboRing.clear();
        if (glVboInstance) { glDeleteBuffers(1, &glVboInstance); glVboInstance = 0; }
        if (glVboQuad)     { glDeleteBuffers(1, &glVboQuad);     glVboQuad     = 0; }
        if (glVao)         { glDeleteVertexArrays(1, &glVao);    glVao         = 0; }
        if (glTexArray)    { glDeleteTextures(1, &glTexArray);   glTexArray    = 0; }
        if (glProgram)     { glDeleteProgram(glProgram);          glProgram     = 0; }
        texW = texH = texLayers = 0;
    }

    void destroyEGL() {
        if (egl.display != EGL_NO_DISPLAY) {
            eglMakeCurrent(egl.display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
            if (egl.surface != EGL_NO_SURFACE) {
                eglDestroySurface(egl.display, egl.surface); egl.surface = EGL_NO_SURFACE;
            }
            if (egl.context != EGL_NO_CONTEXT) {
                eglDestroyContext(egl.display, egl.context); egl.context = EGL_NO_CONTEXT;
            }
            eglTerminate(egl.display); egl.display = EGL_NO_DISPLAY;
        }
        if (egl.window) { ANativeWindow_release(egl.window); egl.window = nullptr; }
    }

    void destroyGL() {
        if (egl.display != EGL_NO_DISPLAY) {
            eglMakeCurrent(egl.display, egl.surface, egl.surface, egl.context);
            destroyGLResources();
        }
        destroyEGL();
    }

    bool setSurface(ANativeWindow* win) {
        destroyGL();
        if (!win) return true;
        if (!initEGL(win))      return false;
        if (!initGLResources()) { destroyEGL(); return false; }
        return true;
    }

    void renderFrame(long long timeMs) {
        if (!egl.valid()) return;
#if HAS_LIBASS
        if (!renderer || !track) return;
        int changed = 0;
        ASS_Image* head = ass_render_frame(renderer, track, timeMs, &changed);
#else
        ASS_Image* head = nullptr;
#endif
        eglMakeCurrent(egl.display, egl.surface, egl.surface, egl.context);

        glViewport(0, 0, width, height);
        glClearColor(0.f, 0.f, 0.f, 0.f);
        glClear(GL_COLOR_BUFFER_BIT);

        frameImages.clear();
        int maxW = 0, maxH = 0;
#if HAS_LIBASS
        for (ASS_Image* i = head; i; i = i->next) {
            if (i->w == 0 || i->h == 0) continue;
            frameImages.push_back(i);
            if (i->w > maxW) maxW = i->w;
            if (i->h > maxH) maxH = i->h;
        }
#endif
        int n = static_cast<int>(frameImages.size());
        if (n == 0) {
            eglSwapBuffers(egl.display, egl.surface);
            return;
        }

        ensureTexArray(maxW, maxH, n);

        int layerBytes = maxW * maxH;
        int totalBytes = layerBytes * n;

        std::vector<float> instData(n * 11);
        const float uvSX = texW > 0 ? 1.f / static_cast<float>(texW) : 1.f;
        const float uvSY = texH > 0 ? 1.f / static_cast<float>(texH) : 1.f;

        uint8_t* pboPtr = pboRing.mapWrite(totalBytes);

        for (int idx = 0; idx < n; ++idx) {
            const ASS_Image* img = frameImages[idx];
            float* dst = instData.data() + idx * 11;
            dst[0] = static_cast<float>(img->dst_x);
            dst[1] = static_cast<float>(img->dst_y);
            dst[2] = static_cast<float>(img->w);
            dst[3] = static_cast<float>(img->h);
            dst[4] = static_cast<float>((img->color >> 24) & 0xFF) / 255.f;
            dst[5] = static_cast<float>((img->color >> 16) & 0xFF) / 255.f;
            dst[6] = static_cast<float>((img->color >>  8) & 0xFF) / 255.f;
            dst[7] = static_cast<float>(255u - (img->color & 0xFF)) / 255.f;
            dst[8] = static_cast<float>(idx);
            dst[9] = static_cast<float>(img->w)  * uvSX;
            dst[10]= static_cast<float>(img->h)  * uvSY;

            if (pboPtr) {
                size_t off = static_cast<size_t>(idx) * layerBytes;
                memset(pboPtr + off, 0, layerBytes);
                for (int row = 0; row < img->h; ++row)
                    memcpy(pboPtr + off + row * maxW,
                           img->bitmap + row * img->stride, img->w);
            }
        }

        if (pboPtr) {
            pboRing.unmap();
            for (int idx = 0; idx < n; ++idx) {
                glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0,
                                0, 0, idx, maxW, maxH, 1,
                                GL_RED, GL_UNSIGNED_BYTE,
                                reinterpret_cast<void*>(
                                    static_cast<GLintptr>(
                                        static_cast<size_t>(idx) * static_cast<size_t>(layerBytes))));
            }
            pboRing.swap();
        } else {
            if (static_cast<int>(fallbackBuf.size()) < layerBytes)
                fallbackBuf.resize(layerBytes);
            for (int idx = 0; idx < n; ++idx) {
                const ASS_Image* img = frameImages[idx];
                memset(fallbackBuf.data(), 0, layerBytes);
                for (int row = 0; row < img->h; ++row)
                    memcpy(fallbackBuf.data() + row * maxW,
                           img->bitmap + row * img->stride, img->w);
                glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0,
                                0, 0, idx, maxW, maxH, 1,
                                GL_RED, GL_UNSIGNED_BYTE, fallbackBuf.data());
            }
        }

        if (n > glInstanceCapacity) {
            glInstanceCapacity = n + 64;
            glBindBuffer(GL_ARRAY_BUFFER, glVboInstance);
            glBufferData(GL_ARRAY_BUFFER, glInstanceCapacity * 11 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
        }
        glBindBuffer(GL_ARRAY_BUFFER, glVboInstance);
        glBufferSubData(GL_ARRAY_BUFFER, 0, n * 11 * sizeof(float), instData.data());
        glBindBuffer(GL_ARRAY_BUFFER, 0);

        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

        glUseProgram(glProgram);
        glUniform2f(locRes, static_cast<float>(width), static_cast<float>(height));
        glActiveTexture(GL_TEXTURE0);
        glUniform1i(locTex, 0);

        glBindVertexArray(glVao);
        glDrawArraysInstanced(GL_TRIANGLES, 0, 6, n);
        glBindVertexArray(0);

        eglSwapBuffers(egl.display, egl.surface);
    }
};

extern "C" JNIEXPORT jlong JNICALL
Java_com_sakurafubuki_yume_feature_player_ass_AssRenderer_nativeInit(
    JNIEnv* env, jobject, jstring configPath)
{
    AssContext* ctx = new AssContext();
#if HAS_LIBASS
    ctx->lib = ass_library_init();
    if (ctx->lib) {
        ass_set_extract_fonts(ctx->lib, 1);
        ass_set_fonts_dir(ctx->lib, "/system/fonts");
        ass_set_fonts_dir(ctx->lib, "/product/fonts");
        ass_set_fonts_dir(ctx->lib, "/vendor/fonts");
        ctx->renderer = ass_renderer_init(ctx->lib);
        if (ctx->renderer) {
            ass_set_shaper(ctx->renderer, ASS_SHAPING_COMPLEX);
            ass_set_hinting(ctx->renderer, ASS_HINTING_LIGHT);
            const char* cfg = configPath ? env->GetStringUTFChars(configPath, nullptr) : nullptr;
            if (cfg) LOGD("fontconfig: %s", cfg);
            ass_set_fonts(ctx->renderer, nullptr, nullptr, ASS_FONTPROVIDER_AUTODETECT, cfg, 1);
            if (cfg) env->ReleaseStringUTFChars(configPath, cfg);
        }
    }
#else
    LOGE("Compiled without libass headers!");
#endif
    return reinterpret_cast<jlong>(ctx);
}

extern "C" JNIEXPORT void JNICALL
Java_com_sakurafubuki_yume_feature_player_ass_AssRenderer_nativeSetFontsDir(
    JNIEnv* env, jobject, jlong handle, jstring path)
{
    AssContext* ctx = reinterpret_cast<AssContext*>(handle);
    if (!ctx) return;
    const char* p = env->GetStringUTFChars(path, nullptr);
    if (!p) return;
    std::lock_guard<std::mutex> lock(ctx->renderMutex);
#if HAS_LIBASS
    if (ctx->lib) {
        scan_fonts_dir(ctx->lib, p);
        if (ctx->renderer)
            ass_set_fonts(ctx->renderer, nullptr, "sans-serif", 1, nullptr, 1);
    }
#endif
    env->ReleaseStringUTFChars(path, p);
}

extern "C" JNIEXPORT void JNICALL
Java_com_sakurafubuki_yume_feature_player_ass_AssRenderer_nativeLoadTrack(
    JNIEnv* env, jobject, jlong handle, jbyteArray data, jint length)
{
    AssContext* ctx = reinterpret_cast<AssContext*>(handle);
    if (!ctx) return;
    std::lock_guard<std::mutex> lock(ctx->renderMutex);
    ctx->ycbcrMatrix = 0;
#if HAS_LIBASS
    if (ctx->track) { ass_free_track(ctx->track); ctx->track = nullptr; }
    if (ctx->lib && data && length > 0) {
        jbyte* buf = env->GetByteArrayElements(data, nullptr);
        ctx->track = ass_read_memory(ctx->lib, reinterpret_cast<char*>(buf), length, "UTF-8");
        env->ReleaseByteArrayElements(data, buf, JNI_ABORT);
        if (ctx->track) {
            ass_track_set_feature(ctx->track, ASS_FEATURE_WRAP_UNICODE, 1);
            int rx = ctx->track->PlayResX, ry = ctx->track->PlayResY;
            if (rx > 0 && ry > 0 && ctx->renderer) {
                ass_set_storage_size(ctx->renderer, rx, ry);
                LOGD("PlayRes: %dx%d", rx, ry);
            }
            ctx->ycbcrMatrix = ctx->track->YCbCrMatrix;
            LOGD("YCbCr matrix: 0x%02x", ctx->ycbcrMatrix);
        }
    }
#endif
}

extern "C" JNIEXPORT void JNICALL
Java_com_sakurafubuki_yume_feature_player_ass_AssRenderer_nativeCreateAssTrack(
    JNIEnv*, jobject, jlong handle)
{
    AssContext* ctx = reinterpret_cast<AssContext*>(handle);
    if (!ctx) return;
    std::lock_guard<std::mutex> lock(ctx->renderMutex);
#if HAS_LIBASS
    if (ctx->track) { ass_free_track(ctx->track); ctx->track = nullptr; }
    if (ctx->lib) {
        ctx->track = ass_new_track(ctx->lib);
        if (ctx->track) {
            ass_track_set_feature(ctx->track, ASS_FEATURE_WRAP_UNICODE, 1);
        }
    }
#endif
}

extern "C" JNIEXPORT void JNICALL
Java_com_sakurafubuki_yume_feature_player_ass_AssRenderer_nativeProcessAssChunk(
    JNIEnv* env, jobject, jlong handle, jbyteArray data, jint length, jlong timecode, jlong duration)
{
    AssContext* ctx = reinterpret_cast<AssContext*>(handle);
    if (!ctx || !ctx->track) return;
    jbyte* buf = env->GetByteArrayElements(data, nullptr);
    if (!buf) return;
    std::lock_guard<std::mutex> lock(ctx->renderMutex);
#if HAS_LIBASS
    ass_process_chunk(ctx->track, reinterpret_cast<char*>(buf), length, timecode, duration);
    int rx = ctx->track->PlayResX, ry = ctx->track->PlayResY;
    if (rx > 0 && ry > 0 && ctx->renderer) {
        ass_set_storage_size(ctx->renderer, rx, ry);
    }
    ctx->ycbcrMatrix = ctx->track->YCbCrMatrix;
#endif
    env->ReleaseByteArrayElements(data, buf, JNI_ABORT);
}

extern "C" JNIEXPORT void JNICALL
Java_com_sakurafubuki_yume_feature_player_ass_AssRenderer_nativeSetSurface(
    JNIEnv* env, jobject, jlong handle, jobject surface)
{
    AssContext* ctx = reinterpret_cast<AssContext*>(handle);
    if (!ctx) return;
    std::lock_guard<std::mutex> lock(ctx->renderMutex);
    ANativeWindow* win = surface ? ANativeWindow_fromSurface(env, surface) : nullptr;
    if (!ctx->setSurface(win))
        LOGE("setSurface failed — GLES3 unavailable");
    if (win && ctx->width > 0 && ctx->height > 0 && ctx->egl.valid())
        glViewport(0, 0, ctx->width, ctx->height);
}

extern "C" JNIEXPORT void JNICALL
Java_com_sakurafubuki_yume_feature_player_ass_AssRenderer_nativeSetStorageSize(
    JNIEnv*, jobject, jlong handle, jint width, jint height)
{
    AssContext* ctx = reinterpret_cast<AssContext*>(handle);
    if (!ctx) return;
    std::lock_guard<std::mutex> lock(ctx->renderMutex);
#if HAS_LIBASS
    if (ctx->renderer && width > 0 && height > 0)
        ass_set_storage_size(ctx->renderer, width, height);
#endif
}

extern "C" JNIEXPORT void JNICALL
Java_com_sakurafubuki_yume_feature_player_ass_AssRenderer_nativeSetFrameSize(
    JNIEnv*, jobject, jlong handle, jint width, jint height)
{
    AssContext* ctx = reinterpret_cast<AssContext*>(handle);
    if (!ctx) return;
    std::lock_guard<std::mutex> lock(ctx->renderMutex);
    ctx->width = width; ctx->height = height;
#if HAS_LIBASS
    if (ctx->renderer && width > 0 && height > 0) {
        ass_set_frame_size(ctx->renderer, width, height);
        ass_set_storage_size(ctx->renderer, width, height);
    }
#endif
}

extern "C" JNIEXPORT void JNICALL
Java_com_sakurafubuki_yume_feature_player_ass_AssRenderer_nativeFlushEvents(
    JNIEnv*, jobject, jlong handle)
{
    AssContext* ctx = reinterpret_cast<AssContext*>(handle);
    if (!ctx) return;
    std::lock_guard<std::mutex> lock(ctx->renderMutex);
#if HAS_LIBASS
    if (ctx->track) ass_flush_events(ctx->track);
#endif
}

extern "C" JNIEXPORT void JNICALL
Java_com_sakurafubuki_yume_feature_player_ass_AssRenderer_nativeSetStyleOverride(
    JNIEnv* env, jobject, jlong handle,
    jfloat fontSize, jint textColor, jboolean showBackground, jboolean applyEmbeddedStyles)
{
    AssContext* ctx = reinterpret_cast<AssContext*>(handle);
    if (!ctx) return;
    std::lock_guard<std::mutex> lock(ctx->renderMutex);
#if HAS_LIBASS
    if (!ctx->renderer) return;
    if (!applyEmbeddedStyles) {
        ctx->ycbcrMatrix = YCBCR_NONE;
        ASS_Style style = {};
        style.FontSize = fontSize > 0.f ? (double)fontSize : 20.0;
        uint8_t a = (textColor >> 24) & 0xFF;
        uint8_t r = (textColor >> 16) & 0xFF;
        uint8_t g = (textColor >>  8) & 0xFF;
        uint8_t b =  textColor        & 0xFF;
        style.PrimaryColour   = (r<<24)|(g<<16)|(b<<8)|(255-a);
        style.SecondaryColour = style.PrimaryColour;
        style.OutlineColour   = 0x000000FF;
        style.BackColour      = showBackground ? 0x000000FF : 0x00000000;
        style.Outline = 2.0; style.Shadow = 1.0;
        style.BorderStyle = 1; style.ScaleX = 1.0; style.ScaleY = 1.0;
        ass_set_selective_style_override(ctx->renderer, &style);
        int bits = ASS_OVERRIDE_BIT_FONT_SIZE_FIELDS
                 | ASS_OVERRIDE_BIT_COLORS
                 | ASS_OVERRIDE_BIT_BORDER;
        ass_set_selective_style_override_enabled(ctx->renderer, bits);
    } else {
        ass_set_selective_style_override_enabled(ctx->renderer, 0);
    }
#endif
}

extern "C" JNIEXPORT void JNICALL
Java_com_sakurafubuki_yume_feature_player_ass_AssRenderer_nativeRenderFrame(
    JNIEnv*, jobject, jlong handle, jlong timeMs)
{
    AssContext* ctx = reinterpret_cast<AssContext*>(handle);
    if (!ctx) return;
    std::lock_guard<std::mutex> lock(ctx->renderMutex);
    ctx->renderFrame(timeMs);
}

extern "C" JNIEXPORT void JNICALL
Java_com_sakurafubuki_yume_feature_player_ass_AssRenderer_nativeAddFont(
    JNIEnv* env, jobject, jlong handle, jstring name, jbyteArray data)
{
    AssContext* ctx = reinterpret_cast<AssContext*>(handle);
    if (!ctx) return;
    const char* nameStr = env->GetStringUTFChars(name, nullptr);
    jbyte* fontData = env->GetByteArrayElements(data, nullptr);
    jsize  dataSize = env->GetArrayLength(data);
    std::lock_guard<std::mutex> lock(ctx->renderMutex);
#if HAS_LIBASS
    if (ctx->lib && nameStr && fontData && dataSize > 0)
        ass_add_font(ctx->lib, nameStr, reinterpret_cast<char*>(fontData), dataSize);
#endif
    if (fontData) env->ReleaseByteArrayElements(data, fontData, JNI_ABORT);
    if (nameStr)  env->ReleaseStringUTFChars(name, nameStr);
}

extern "C" JNIEXPORT void JNICALL
Java_com_sakurafubuki_yume_feature_player_ass_AssRenderer_nativeRebuildFontCache(
    JNIEnv*, jobject, jlong handle)
{
    AssContext* ctx = reinterpret_cast<AssContext*>(handle);
    if (!ctx) return;
    std::lock_guard<std::mutex> lock(ctx->renderMutex);
#if HAS_LIBASS
    if (ctx->renderer)
        ass_set_fonts(ctx->renderer, nullptr, nullptr,
                      ASS_FONTPROVIDER_AUTODETECT, ctx->fontconfigPath, 1);
#endif
}

extern "C" JNIEXPORT void JNICALL
Java_com_sakurafubuki_yume_feature_player_ass_AssRenderer_nativeSetFontConfig(
    JNIEnv* env, jobject, jlong handle, jstring configPath)
{
    AssContext* ctx = reinterpret_cast<AssContext*>(handle);
    if (!ctx) return;
    const char* path = configPath ? env->GetStringUTFChars(configPath, nullptr) : nullptr;
    std::lock_guard<std::mutex> lock(ctx->renderMutex);
    free(ctx->fontconfigPath);
    ctx->fontconfigPath = path ? strdup(path) : nullptr;
#if HAS_LIBASS
    if (ctx->renderer)
        ass_set_fonts(ctx->renderer, nullptr, nullptr, ASS_FONTPROVIDER_AUTODETECT, path, 1);
#endif
    if (path) env->ReleaseStringUTFChars(configPath, path);
}

extern "C" JNIEXPORT void JNICALL
Java_com_sakurafubuki_yume_feature_player_ass_AssRenderer_nativeRelease(
    JNIEnv*, jobject, jlong handle)
{
    delete reinterpret_cast<AssContext*>(handle);
}
