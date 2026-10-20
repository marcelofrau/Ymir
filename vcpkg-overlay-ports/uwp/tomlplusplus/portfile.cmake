# Ymir UWP overlay for tomlplusplus.
# The upstream vcpkg port builds a compiled library with meson, which does not support WindowsStore.
# This overlay installs the headers and exports the same `tomlplusplus::tomlplusplus` target in
# non-header-only mode (TOML_HEADER_ONLY=0), matching the desktop build: the implementation is
# compiled by the consumer through a translation unit that defines TOML_IMPLEMENTATION
# (see apps/ymir-sdl3/src/app/toml_implementation.cpp).

vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO marzer/tomlplusplus
    REF "v${VERSION}"
    SHA512 c227fc8147c9459b29ad24002aaf6ab2c42fac22ea04c1c52b283a0172581ccd4527b33c1931e0ef0d1db6b6a53f9e9882c6d4231c7f3494cf070d0220741aa5
    HEAD_REF master
    PATCHES
        fix-android-fileapi.patch
)

file(INSTALL "${SOURCE_PATH}/include/" DESTINATION "${CURRENT_PACKAGES_DIR}/include"
     FILES_MATCHING PATTERN "*.hpp" PATTERN "*.inl" PATTERN "*.h")

file(WRITE "${CURRENT_PACKAGES_DIR}/share/tomlplusplus/tomlplusplusConfig.cmake" [[
if(NOT TARGET tomlplusplus::tomlplusplus)
    get_filename_component(_tomlplusplus_prefix "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
    add_library(tomlplusplus::tomlplusplus INTERFACE IMPORTED)
    set_target_properties(tomlplusplus::tomlplusplus PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${_tomlplusplus_prefix}/include"
        INTERFACE_COMPILE_DEFINITIONS "TOML_HEADER_ONLY=0"
        INTERFACE_COMPILE_FEATURES "cxx_std_17"
    )
    unset(_tomlplusplus_prefix)
endif()
]])

set(VCPKG_POLICY_EMPTY_INCLUDE_FOLDER disabled)
vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")
