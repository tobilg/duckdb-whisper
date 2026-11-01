# SDL2 dlopen()s the host ALSA library. Build a shared library for detection,
# but never embed this copy (and its build-machine plugin paths) in the
# extension.
vcpkg_check_linkage(ONLY_DYNAMIC_LIBRARY)

# The release archive includes configure; GitHub's source archive needs an
# autoreconf toolchain that is not present in the manylinux CI image.
vcpkg_download_distfile(
  ARCHIVE
  URLS
  "https://www.alsa-project.org/files/pub/lib/alsa-lib-${VERSION}.tar.bz2"
  FILENAME
  "alsa-lib-${VERSION}.tar.bz2"
  SHA512
  7fc0fa8a5ae02d3404d2c262c6a14fcbb8b08e25993eac86b9e89b8419ed4d293b422da77b3eb7a1930f26c316b638e5aa7bdba78b0ada9908b0362d132a0cc0
)
vcpkg_extract_source_archive(SOURCE_PATH ARCHIVE "${ARCHIVE}")
vcpkg_configure_make(SOURCE_PATH "${SOURCE_PATH}" OPTIONS --disable-python
                     --with-configdir=/usr/share/alsa)
vcpkg_install_make()
vcpkg_fixup_pkgconfig()
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include"
     "${CURRENT_PACKAGES_DIR}/debug/share")
vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/COPYING")
