vcpkg_download_distfile(ARCHIVE
    URLS "https://libarchive.org/downloads/libarchive-${VERSION}.tar.xz"
    FILENAME "libarchive-${VERSION}.tar.xz"
    SHA512 2524f71f4c2ebc254a1927279be3394e820d0a0c6dec7ef835a862aa08c35756edaa4208bcdc710dd092872b59c200b555b78670372e2830822e278ff1ec4e4a
)

vcpkg_extract_source_archive(
    SOURCE_PATH
    ARCHIVE "${ARCHIVE}"
)

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        -DENABLE_OPENSSL=OFF
        -DENABLE_TEST=OFF
        -DENABLE_TAR=OFF
        -DENABLE_CAT=OFF
        -DENABLE_CPIO=OFF
        -DENABLE_UNZIP=OFF
)

vcpkg_cmake_install()

vcpkg_fixup_pkgconfig()

vcpkg_copy_pdbs()

file(REMOVE_RECURSE
      "${CURRENT_PACKAGES_DIR}/debug/include"
      "${CURRENT_PACKAGES_DIR}/debug/share"
      "${CURRENT_PACKAGES_DIR}/share/man"
)

foreach(header "include/archive.h" "include/archive_entry.h")
    vcpkg_replace_string("${CURRENT_PACKAGES_DIR}/${header}" "(!defined LIBARCHIVE_STATIC)" "0")
endforeach()

file(INSTALL "${CURRENT_PORT_DIR}/usage" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}")
file(INSTALL "${SOURCE_PATH}/COPYING" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}" RENAME copyright)
