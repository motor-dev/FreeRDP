#!/bin/bash

SCM_URL=https://github.com/FFmpeg/FFmpeg/archive
SCM_TAG=n7.1.1
SCM_HASH=f117507dc501f2a6c11f9241d8d0c3213846cfad91764361af37befd6b6c523d
OLD_PATH=$PATH

source $(dirname "${BASH_SOURCE[0]}")/android-build-common.sh

function get_toolchain() {
  HOST_OS=$(uname -s)
  case ${HOST_OS} in
  Darwin) HOST_OS=darwin ;;
  Linux) HOST_OS=linux ;;
  FreeBsd) HOST_OS=freebsd ;;
  CYGWIN* | *_NT-*) HOST_OS=cygwin ;;
  esac

  HOST_ARCH=$(uname -m)
  case ${HOST_ARCH} in
  i?86) HOST_ARCH=x86 ;;
  x86_64 | amd64) HOST_ARCH=x86_64 ;;
  esac

  echo "${HOST_OS}-${HOST_ARCH}"
}

function get_build_host() {
  case ${ARCH} in
  armeabi-v7a)
    echo "arm-linux-androideabi"
    ;;
  arm64-v8a)
    echo "aarch64-linux-android"
    ;;
  x86)
    echo "i686-linux-android"
    ;;
  x86_64)
    echo "x86_64-linux-android"
    ;;
  esac
}

function get_clang_target_host() {
  case ${ARCH} in
  armeabi-v7a)
    echo "armv7a-linux-androideabi${NDK_TARGET}"
    ;;
  arm64-v8a)
    echo "aarch64-linux-android${NDK_TARGET}"
    ;;
  x86)
    echo "i686-linux-android${NDK_TARGET}"
    ;;
  x86_64)
    echo "x86_64-linux-android${NDK_TARGET}"
    ;;
  esac
}

function get_arch_specific_ldflags() {
  case ${ARCH} in
  armeabi-v7a)
    echo "-march=armv7-a -mfpu=neon -mfloat-abi=softfp -Wl,--fix-cortex-a8"
    ;;
  arm64-v8a)
    echo "-march=armv8-a"
    ;;
  x86)
    echo "-march=i686"
    ;;
  x86_64)
    echo "-march=x86-64"
    ;;
  esac
}

function set_toolchain_clang_paths {
  TOOLCHAIN=$(get_toolchain)

  common_run export PATH=$PATH:${ANDROID_NDK}/toolchains/llvm/prebuilt/${TOOLCHAIN}/bin
  AR=llvm-ar
  NM=llvm-nm
  RANLIB=llvm-ranlib
  STRIP=llvm-strip
  CC=$(get_clang_target_host)-clang
  CXX=$(get_clang_target_host)-clang++

  case ${ARCH} in
  arm64-v8a)
    common_run export ac_cv_c_bigendian=no
    ;;
  esac
}

function build {
  echo "Building FFmpeg architecture $1..."
  BASE=$(pwd)
  common_run cd $BUILD_SRC

  BUILD_HOST=$(get_build_host)
  set_toolchain_clang_paths
  LDFLAGS=$(get_arch_specific_ldflags)

  CARCH=$TARGET_ARCH
  if [ "$CARCH" == "x86_64" ]; then
    CARCH="x86-64"
  fi
  PATH=$ANDROID_NDK:$PATH
  common_run ./configure \
    --cross-prefix="${BUILD_HOST}-" \
    --sysroot="${ANDROID_NDK}/toolchains/llvm/prebuilt/${TOOLCHAIN}/sysroot" \
    --arch="${CARCH}" \
    --cpu="${TARGET_CPU}" \
    --cc="${CC}" \
    --cxx="${CXX}" \
    --ar="${AR}" \
    --nm="${NM}" \
    --ranlib="${RANLIB}" \
    --strip="${STRIP}" \
    --extra-ldflags="${LDFLAGS}" \
    --prefix="${BUILD_DST}/${ARCH}" \
    --pkg-config="${HOST_PKG_CONFIG_PATH}" \
    --target-os=android \
    ${ARCH_OPTIONS} \
    --enable-cross-compile \
    --enable-pic \
    --enable-lto \
    --enable-jni \
    --enable-mediacodec \
    --enable-shared \
    --disable-vulkan \
    --disable-stripping \
    --disable-programs \
    --disable-doc \
    --disable-avdevice \
    --disable-avfilter \
    --disable-avformat \
    --disable-everything \
    --enable-encoder=aac \
    --enable-encoder=libfdk_aac \
    --enable-encoder=libgsm \
    --enable-encoder=libgsm_ms \
    --enable-encoder=libopenh264 \
    --enable-encoder=libopus \
    --enable-encoder=pcm_alaw \
    --enable-encoder=pcm_mulaw \
    --enable-encoder=pcm_s16le \
    --enable-encoder=pcm_u16le \
    --enable-encoder=h264 \
    --enable-encoder=h264_omx \
    --enable-encoder=h264_mediacodec \
    --enable-encoder=h264_vulkan \
    --enable-decoder=aac \
    --enable-decoder=aac_mediacodec \
    --enable-decoder=adpcm_g722 \
    --enable-decoder=adpcm_g726 \
    --enable-decoder=adpcm_g726le \
    --enable-decoder=gsm \
    --enable-decoder=gsm_ms \
    --enable-decoder=mp3 \
    --enable-decoder=mp3_mediacodec \
    --enable-decoder=h264 \
    --enable-decoder=h264_mediacodec \
    --enable-decoder=libopus \
    --enable-decoder=pcm_alaw \
    --enable-decoder=pcm_mulaw \
    --enable-decoder=pcm_s16le \
    --enable-decoder=pcm_u16le

  common_run make clean
  common_run make -j
  common_run make install
}

# Run the main program.
common_parse_arguments $@
common_update $SCM_URL $SCM_TAG $BUILD_SRC $SCM_HASH

HOST_PKG_CONFIG_PATH=$(command -v pkg-config)
if [ -z ${HOST_PKG_CONFIG_PATH} ]; then
  echo "(*) pkg-config command not found\n"
  exit 1
fi

for ARCH in $BUILD_ARCH; do
  case ${ARCH} in
  armeabi-v7a)
    TARGET_CPU="armv7-a"
    TARGET_ARCH="armv7-a"
    ARCH_OPTIONS="	--enable-neon --enable-asm --enable-inline-asm"
    ;;
  arm64-v8a)
    TARGET_CPU="armv8-a"
    TARGET_ARCH="aarch64"
    ARCH_OPTIONS="	--enable-neon --enable-asm --enable-inline-asm"
    ;;
  x86)
    TARGET_CPU="i686"
    TARGET_ARCH="i686"

    # asm disabled due to this ticker https://trac.ffmpeg.org/ticket/4928
    ARCH_OPTIONS="	--disable-neon --disable-asm --disable-inline-asm"
    ;;
  x86_64)
    TARGET_CPU="x86_64"
    TARGET_ARCH="x86_64"
    ARCH_OPTIONS="	--disable-neon --enable-asm --enable-inline-asm"
    ;;
  esac

  build
  common_run cp -L $BUILD_DST/$ARCH/lib/*.so $BUILD_DST/$ARCH/

  common_run export PATH=$OLD_PATH
done
