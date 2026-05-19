plugins {
    alias(libs.plugins.androidLibrary)
    id("maven-publish")
}

android {
    namespace = "io.github.sakurafubuki.yume.nativelib"

    ndkVersion = libs.versions.ndk.get()
    compileSdk = libs.versions.android.compileSdk.get().toInt()

    defaultConfig {
        minSdk = libs.versions.android.minSdk.get().toInt()
        consumerProguardFiles("consumer-rules.pro")

        externalNativeBuild {
            cmake {
                arguments("-DANDROID_STL=c++_shared")
                abiFilters("arm64-v8a", "armeabi-v7a", "x86", "x86_64")
            }
        }
    }

    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
            version = libs.versions.cmake.get()
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.toVersion(libs.versions.android.jvm.get().toInt())
        targetCompatibility = JavaVersion.toVersion(libs.versions.android.jvm.get().toInt())
    }

    packaging {
        jniLibs {
            useLegacyPackaging = false
        }
    }

    publishing {
        singleVariant("release") {
            withSourcesJar()
        }
    }

    sourceSets {
        getByName("main") {
            jniLibs.directories.addAll(listOf(
                "${rootProject.projectDir}/buildscripts/prefix/jniLibs",
            ))
        }
    }
}

val archToAbi = mapOf(
    "arm64"  to "arm64-v8a",
    "armv7l" to "armeabi-v7a",
    "x86"    to "x86",
    "x86_64" to "x86_64"
)

val copyFfmpegToJniLibs = tasks.register("copyFfmpegToJniLibs") {
    description = "Copies FFmpeg .so from buildscripts prefix to flat jniLibs layout"
    doLast {
        for ((arch, abi) in archToAbi) {
            val srcDir = file("buildscripts/prefix/${arch}/lib")
            val dstDir = file("buildscripts/prefix/jniLibs/${abi}")
            if (srcDir.exists()) {
                project.copy {
                    from(srcDir) { include("*.so") }
                    into(dstDir)
                }
            }
        }
    }
}

tasks.named("preBuild") {
    dependsOn(copyFfmpegToJniLibs)
}

dependencies {
    compileOnly(libs.androidx.annotation)
    compileOnly(libs.androidx.media3.common)
    compileOnly(libs.androidx.media3.decoder)
    compileOnly(libs.androidx.media3.exoplayer)
    implementation(libs.kotlinx.coroutines.android)
}

val gitTagVersion = providers.exec {
    workingDir = rootProject.projectDir
    commandLine("git", "describe", "--tags", "--match", "v*", "--abbrev=0")
}.standardOutput.asText.map { it.trim().removePrefix("v") }

val libVersion = runCatching { gitTagVersion.get() }.getOrDefault("0.0.0")
val libGroup = "io.github.sakurafubuki.yume"
val libArtifact = "yume-lib"

group = libGroup
version = libVersion

afterEvaluate {
    publishing {
        publications {
            create<MavenPublication>("release") {
                groupId = libGroup
                artifactId = libArtifact
                version = libVersion
                from(components["release"])
            }
        }
        repositories {
            maven {
                url = uri("https://maven.pkg.github.com/Sakura-Fubuki76/yume-lib")
                credentials {
                    username = providers.gradleProperty("gpr.user")
                        .orElse(providers.environmentVariable("GITHUB_ACTOR"))
                        .getOrElse("")
                    password = providers.gradleProperty("gpr.token")
                        .orElse(providers.environmentVariable("GITHUB_TOKEN"))
                        .getOrElse("")
                }
            }
        }
    }
}
