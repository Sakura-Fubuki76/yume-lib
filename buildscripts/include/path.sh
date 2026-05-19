#!/bin/bash

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )/.." && pwd )"

os=linux
[[ "$OSTYPE" == "darwin"* ]] && os=mac
export os

if [ "$os" == "mac" ]; then
	[ -z "$cores" ] && cores=$(sysctl -n hw.ncpu)
	export INSTALL=`which ginstall`
	export SED=gsed
else
	[ -z "$cores" ] && cores=$(grep -c ^processor /proc/cpuinfo)
fi
cores=${cores:-4}

unset ANDROID_SDK_ROOT
export ANDROID_HOME="$DIR/sdk/android-sdk-$os"
