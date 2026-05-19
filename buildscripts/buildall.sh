#!/bin/bash -e

cd "$( dirname "${BASH_SOURCE[0]}" )"
. ./include/depinfo.sh

cleanbuild=0
nodeps=0
onlydeps=0
target=all
arch=armv7l

getdeps() {
	local t="$1"
	local deps_name="dep_${t}[*]"
	local deps=${!deps_name}
	[[ -z "$deps" ]] && return 0 || true
	echo "$deps"
}

wasbuilt() {
	local t="$1"
	local varname="built_$t"
	[[ -n "${!varname}" ]]
}

markbuilt() {
	local t="$1"
	local varname="built_$t"
	eval "$varname=1"
}

loadndk() {
	unset ANDROID_NDK_ROOT

	local ndk="$PWD/sdk/android-ndk-${v_ndk}"
	local toolchain=$(echo "$ndk/toolchains/llvm/prebuilt/"*)
	if [ ! -d "$toolchain" ]; then
		echo "Can't find toolchain inside NDK" >&2
		return 1
	fi
	export PATH="$toolchain/bin:$ndk:$PWD/sdk/bin:$PATH"
}

loadarch() {
	local arch_arg="$1"

	unset CC CXX CFLAGS LDFLAGS PKG_CONFIG_PATH
	unset ndk_triple cc_triple prefix_name api_level

	case "$arch_arg" in
		armv7l)
			ndk_triple="arm-linux-androideabi"
			cc_triple="armv7a-linux-androideabi21"
			prefix_name="armv7l"
			api_level=21 ;;
		arm64)
			ndk_triple="aarch64-linux-android"
			cc_triple="aarch64-linux-android21"
			prefix_name="arm64"
			api_level=21 ;;
		x86)
			ndk_triple="i686-linux-android"
			cc_triple="i686-linux-android21"
			prefix_name="x86"
			api_level=21 ;;
		x86_64)
			ndk_triple="x86_64-linux-android"
			cc_triple="x86_64-linux-android21"
			prefix_name="x86_64"
			api_level=21 ;;
		*) echo "Unknown arch: $arch_arg"; exit 255 ;;
	esac

	export CC="${cc_triple}-clang"
	export CXX="${cc_triple}-clang++"
	export AR=llvm-ar
	export RANLIB=llvm-ranlib
	export STRIP=llvm-strip
	export LDFLAGS="-Wl,-O1,--icf=safe -Wl,-z,max-page-size=16384"

	export prefix_dir="$PWD/prefix/$prefix_name"

	export PKG_CONFIG_SYSROOT_DIR="$prefix_dir"
	export PKG_CONFIG_LIBDIR="$prefix_dir/lib/pkgconfig"

	export ndk_suffix="-${prefix_name}"
	export ndk_triple
	export cc_triple
	export prefix_name
}

setup_prefix() {
	mkdir -p "$prefix_dir"

	# Flat symlinks for autotools compatibility
	[ ! -e "$prefix_dir/usr" ] && ln -s . "$prefix_dir/usr" || true
	[ ! -e "$prefix_dir/local" ] && ln -s . "$prefix_dir/local" || true

	# Generate Meson cross file
	local crossfile="$prefix_dir/crossfile.txt"
	local new_hash
	new_hash=$(echo "$cc_triple" | sha256sum | cut -c1-8)

	if [ -f "$crossfile" ] && [ -f "$crossfile.hash" ]; then
		read -r old_hash < "$crossfile.hash"
		[ "$new_hash" == "$old_hash" ] && return 0
	fi

	cat > "$crossfile" <<-HEREDOC
	[binaries]
	c = '${CC}'
	cpp = '${CXX}'
	ar = 'llvm-ar'
	ranlib = 'llvm-ranlib'
	strip = 'llvm-strip'
	pkgconfig = 'pkg-config'

	[built-in options]
	buildtype = 'release'
	default_library = 'static'
	wrap_mode = 'nodownload'

	[host_machine]
	system = 'android'
	cpu_family = '${ndk_triple%%-*}'
	cpu = '${ndk_triple%%-*}'
	endian = 'little'
	HEREDOC

	echo "$new_hash" > "$crossfile.hash"
}

build() {
	local t="$1"
	wasbuilt "$t" && return 0

	if [ $nodeps -eq 0 ]; then
		local deps
		deps=$(getdeps "$t")
		for dep in $deps; do
			build "$dep"
		done
	fi

	echo "========================"
	echo "Building $t ($prefix_name)"
	echo "========================"

	pushd "deps/$t" > /dev/null
	local BUILDSCRIPT="../../scripts/${t}.sh"
	[ $cleanbuild -eq 1 ] && $BUILDSCRIPT clean
	$BUILDSCRIPT build
	popd > /dev/null

	markbuilt "$t"
}

# ---- main ----

while [ $# -gt 0 ]; do
	case "$1" in
		--clean)   cleanbuild=1 ;;
		-n|--no-deps) nodeps=1 ;;
		--only-deps) onlydeps=1 ;;
		--arch) shift; arch="$1" ;;
		-h|--help)
			echo "Usage: $0 [--clean] [-n|--no-deps] [--only-deps] --arch <arch> [target]"
			echo "Architectures: armv7l, arm64, x86, x86_64"
			echo "Targets: all, ffmpeg, libass"
			exit 0 ;;
		*) target="$1" ;;
	esac
	shift
done

loadndk
loadarch "$arch"
setup_prefix

if [ "$target" == "all" ]; then
	# Build all targets: ffmpeg (shared .so) + libass (static .a) + all their deps
	build ffmpeg
	build libass
else
	build "$target"
fi

# Create ABI-named symlink for AGP jniLibs compatibility
case "$prefix_name" in
	arm64)   abi_name="arm64-v8a" ;;
	armv7l)  abi_name="armeabi-v7a" ;;
	*)       abi_name="$prefix_name" ;;
esac
if [ "$abi_name" != "$prefix_name" ]; then
	rm -f "$PWD/prefix/$abi_name"
	ln -sf "$prefix_name" "$PWD/prefix/$abi_name"
fi

echo "========================"
echo "Build complete!"
echo "Prefix: $prefix_dir"
echo "========================"
ls -la "$prefix_dir/lib/" 2>/dev/null || echo "(no libs)"
