package io.github.sakurafubuki.yume.nativelib.player;

import androidx.annotation.Nullable;
import androidx.media3.common.C;
import androidx.media3.common.MediaLibraryInfo;
import androidx.media3.common.MimeTypes;
import androidx.media3.common.util.LibraryLoader;
import androidx.media3.common.util.Log;
import androidx.media3.common.util.UnstableApi;

import java.util.Collections;
import java.util.LinkedHashSet;
import java.util.Set;

/** Configures and queries the underlying native library. */
@UnstableApi
public final class FfmpegLibrary {

  static {
    MediaLibraryInfo.registerModule("media3.decoder.ffmpeg");
  }

  private static final String TAG = "FfmpegLibrary";

  private static final LibraryLoader LOADER =
      new LibraryLoader("yume_player") {
        @Override
        protected void loadLibrary(String name) {
          System.loadLibrary(name);
        }
      };

  @Nullable
  private static String version;
  private static int inputBufferPaddingSize = C.LENGTH_UNSET;

  @Nullable
  private static Set<String> availableDecoders;

  private FfmpegLibrary() {}

  /**
   * Override the names of the FFmpeg native libraries. If an application wishes to call this
   * method, it must do so before calling any other method defined by this class, and before
   * instantiating a {@link FfmpegAudioRenderer} instance.
   *
   * @param libraries The names of the FFmpeg native libraries.
   */
  public static void setLibraries(String... libraries) {
    LOADER.setLibraries(libraries);
  }

  /** Returns whether the underlying library is available, loading it if necessary. */
  public static boolean isAvailable() {
    return LOADER.isAvailable();
  }

  /** Returns the version of the underlying library if available, or null otherwise. */
  @Nullable
  public static String getVersion() {
    if (!isAvailable()) {
      return null;
    }
    if (version == null) {
      version = ffmpegGetVersion();
    }
    return version;
  }

  /**
   * Returns the required amount of padding for input buffers in bytes, or {@link C#LENGTH_UNSET} if
   * the underlying library is not available.
   */
  public static int getInputBufferPaddingSize() {
    if (!isAvailable()) {
      return C.LENGTH_UNSET;
    }
    if (inputBufferPaddingSize == C.LENGTH_UNSET) {
      inputBufferPaddingSize = ffmpegGetInputBufferPaddingSize();
    }
    return inputBufferPaddingSize;
  }

  /**
   * Returns an unmodifiable set of all decoder names available in the underlying FFmpeg build.
   * This is populated once on first call by querying the native library directly.
   */
  public static Set<String> getAvailableDecoders() {
    if (!isAvailable()) {
      return Collections.emptySet();
    }
    if (availableDecoders == null) {
      String[] decoders = ffmpegListDecoders();
      Set<String> set = new LinkedHashSet<>();
      if (decoders != null) {
        Collections.addAll(set, decoders);
      }
      availableDecoders = Collections.unmodifiableSet(set);
    }
    return availableDecoders;
  }

  /**
   * Returns whether the underlying library supports the specified MIME type.
   *
   * @param mimeType The MIME type to check.
   */
  public static boolean supportsFormat(String mimeType) {
    if (!isAvailable()) {
      return false;
    }
    @Nullable String codecName = getCodecName(mimeType);
    if (codecName == null) {
      return false;
    }
    if (!ffmpegHasDecoder(codecName)) {
      Log.w(TAG, "No " + codecName + " decoder available. Check the FFmpeg build configuration.");
      return false;
    }
    return true;
  }

  /**
   * Returns the name of the FFmpeg decoder that could be used to decode the format, or {@code null}
   * if it's unsupported.
   */
  @Nullable
  /* package */ static String getCodecName(String mimeType) {
    return switch (mimeType) {
      // --- Audio codecs ---
      case MimeTypes.AUDIO_AAC -> "aac";
      case MimeTypes.AUDIO_MPEG, MimeTypes.AUDIO_MPEG_L1, MimeTypes.AUDIO_MPEG_L2 -> "mp3";
      case MimeTypes.AUDIO_AC3 -> "ac3";
      case MimeTypes.AUDIO_E_AC3, MimeTypes.AUDIO_E_AC3_JOC -> "eac3";
      case MimeTypes.AUDIO_TRUEHD -> "truehd";
      case MimeTypes.AUDIO_DTS, MimeTypes.AUDIO_DTS_HD -> "dca";
      case MimeTypes.AUDIO_VORBIS -> "vorbis";
      case MimeTypes.AUDIO_OPUS -> "opus";
      case MimeTypes.AUDIO_AMR_NB -> "amrnb";
      case MimeTypes.AUDIO_AMR_WB -> "amrwb";
      case MimeTypes.AUDIO_FLAC -> "flac";
      case MimeTypes.AUDIO_ALAC -> "alac";
      case MimeTypes.AUDIO_MLAW -> "pcm_mulaw";
      case MimeTypes.AUDIO_ALAW -> "pcm_alaw";

      // MPEG-H 3D Audio
      case MimeTypes.AUDIO_MPEGH_MHA1, MimeTypes.AUDIO_MPEGH_MHM1 -> "mpegh_3d_audio";

      // WMA family — try the most common variant; supportsFormat double-checks via native
      case "audio/x-ms-wma" -> "wmav2";
      case "audio/x-ms-wma-pro" -> "wmapro";
      case "audio/x-ms-wma-lossless" -> "wmalossless";

      // Other audio
      case "audio/x-ape" -> "ape";
      case "audio/ac4" -> "ac4";
      case "audio/x-pn-realaudio" -> "cook";
      case MimeTypes.AUDIO_RAW -> "pcm_s16le";
      case MimeTypes.AUDIO_WAV -> "pcm_s16le";

      // --- Video codecs ---
      case MimeTypes.VIDEO_H264 -> "h264";
      case MimeTypes.VIDEO_H265 -> "hevc";
      case MimeTypes.VIDEO_MPEG -> "mpegvideo";
      case MimeTypes.VIDEO_MPEG2 -> "mpeg2video";
      case MimeTypes.VIDEO_VP8 -> "libvpx";
      case MimeTypes.VIDEO_VP9 -> "libvpx-vp9";

      // AV1 — try native av1 first; libdav1d is probed via supportsFormat
      case MimeTypes.VIDEO_AV1 -> "av1";

      // VC-1
      case MimeTypes.VIDEO_VC1 -> "vc1";

      // MPEG-4 Part 2 / DivX / Xvid
      case MimeTypes.VIDEO_MP4V, MimeTypes.VIDEO_DIVX -> "mpeg4";

      // H.263
      case MimeTypes.VIDEO_H263 -> "h263";

      // Motion JPEG
      case MimeTypes.VIDEO_MJPEG -> "mjpeg";

      // VP6 (Flash video)
      case "video/x-vnd.on2.vp6" -> "vp6f";

      // Theora
      case "video/x-theora", MimeTypes.VIDEO_OGG -> "theora";

      // Windows Media Video
      case "video/x-ms-wmv" -> "wmv3";
      case "video/x-ms-wmv7" -> "wmv1";
      case "video/x-ms-wmv8" -> "wmv2";

      // Dolby Vision (falls back to hevc if the underlying codec uses that path)
      case MimeTypes.VIDEO_DOLBY_VISION -> "hevc";

      // MS-MPEG4
      case "video/x-msmpeg4" -> "msmpeg4";

      // RealVideo
      case "video/x-pn-realvideo" -> "rv40";

      default -> null;
    };
  }

  private static native String ffmpegGetVersion();

  private static native int ffmpegGetInputBufferPaddingSize();

  private static native boolean ffmpegHasDecoder(String codecName);

  /**
   * Returns all available decoder names from the native FFmpeg build.
   * This allows dynamic discovery of codecs beyond the hardcoded mapping.
   */
  private static native String[] ffmpegListDecoders();
}
