# Keep native methods called via JNI
-keepclasseswithmembernames class * {
    native <methods>;
}

# Keep Media3 decoder/extension entry points
-keep class io.github.sakurafubuki.yume.native.player.** { *; }
-keep class io.github.sakurafubuki.yume.native.mediainfo.** { *; }
