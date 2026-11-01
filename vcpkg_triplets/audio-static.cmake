set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_BUILD_TYPE release)

# SDL embeds its ALSA adapter, but dlopen()s the host's libasound.so.2 when
# recording is requested. Do not bake vcpkg's ALSA configuration/plugin paths
# into the extension or make libasound a dependency of LOAD whisper.
if(PORT STREQUAL "alsa")
  set(VCPKG_LIBRARY_LINKAGE dynamic)
endif()
