# Keep this build independent of the codecs and shared libraries on the host.
vcpkg_check_linkage(ONLY_STATIC_LIBRARY)
vcpkg_from_github(
  OUT_SOURCE_PATH
  SOURCE_PATH
  REPO
  FFmpeg/FFmpeg
  REF
  "n${VERSION}"
  SHA512
  c72f4062aecc16d8b2b1e8678d5efe3af4cfaa0cc7c0997052248f9e499e60c2463acf07877cf3b78b246ce3e8078cb043e8d97e90a6b50d06af32ff7369a788
)

if(VCPKG_TARGET_ARCHITECTURE MATCHES "^(x64|x86)$")
  vcpkg_add_to_path("${CURRENT_HOST_INSTALLED_DIR}/tools/vcpkg-tool-nasm")
endif()

vcpkg_configure_make(
  SOURCE_PATH
  "${SOURCE_PATH}"
  NO_ADDITIONAL_PATHS
  DISABLE_VERBOSE_FLAGS
  OPTIONS
  --enable-pic
  --enable-runtime-cpudetect
  --disable-autodetect
  --disable-debug
  --disable-doc
  --disable-programs
  --disable-everything
  --disable-avdevice
  --disable-avfilter
  --disable-swscale
  --enable-avcodec
  --enable-avformat
  --enable-avutil
  --enable-swresample
  --enable-decoder=pcm_alaw,pcm_mulaw,pcm_u8,pcm_s8,pcm_s16le,pcm_s16be,pcm_u16le,pcm_u16be,pcm_s24le,pcm_s24be,pcm_u24le,pcm_u24be,pcm_s32le,pcm_s32be,pcm_u32le,pcm_u32be,pcm_s64le,pcm_s64be,pcm_f32le,pcm_f32be,pcm_f64le,pcm_f64be,mp3,mp3float,flac,aac,aac_latm,vorbis,opus
  --enable-parser=mpegaudio,flac,aac,aac_latm,vorbis,opus
  --enable-demuxer=wav,aiff,mp3,flac,aac,ogg,mov,matroska
  # FFmpeg 8.1's OpenSSL TLS implementation also references UDP helpers.
  --enable-protocol=file,http,https,tcp,tls,udp
  --enable-openssl
  --enable-version3
  --pkg-config-flags=--static)
vcpkg_install_make()
vcpkg_fixup_pkgconfig()
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include"
     "${CURRENT_PACKAGES_DIR}/debug/share")
vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/COPYING.LGPLv3"
                        "${SOURCE_PATH}/COPYING.LGPLv2.1")
