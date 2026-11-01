# Build a host tool so Linux CI does not need the shared multimedia toolchain
# just to obtain NASM. Release archives include the generated configure script.
vcpkg_download_distfile(
  ARCHIVE
  URLS
  "https://www.nasm.us/pub/nasm/releasebuilds/${VERSION}/nasm-${VERSION}.tar.xz"
  "https://www.nasm.dev/pub/nasm/releasebuilds/${VERSION}/nasm-${VERSION}.tar.xz"
  FILENAME
  "nasm-${VERSION}.tar.xz"
  SHA512
  ca9550ad3239cfaa9546a908e8de86e11a7c7dd9d6006f42d272d0b025b601cdb205e60b8b3929f31123ef450dd0f0403e1300328cadf0ffdfd459f443944482
)
vcpkg_extract_source_archive(SOURCE_PATH ARCHIVE "${ARCHIVE}")
vcpkg_configure_make(SOURCE_PATH "${SOURCE_PATH}" OPTIONS --disable-doc)
vcpkg_install_make()
vcpkg_copy_tools(TOOL_NAMES nasm ndisasm SEARCH_DIR
                 "${CURRENT_PACKAGES_DIR}/tools/${PORT}/bin")
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug"
     "${CURRENT_PACKAGES_DIR}/tools/${PORT}/bin"
     "${CURRENT_PACKAGES_DIR}/share/${PORT}/man1")
set(VCPKG_POLICY_EMPTY_INCLUDE_FOLDER enabled)
vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")
