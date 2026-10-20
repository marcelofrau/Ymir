# UWP (Xbox Dev Mode / Windows) packaging for ymir-sdl3.
# Included from apps/ymir-sdl3/CMakeLists.txt when Ymir_PLATFORM_UWP is ON (CMAKE_SYSTEM_NAME=WindowsStore).
#
# The Visual Studio generator turns ymir-sdl3 into a UWP app project when the target has a .appxmanifest source.
# Files marked with VS_DEPLOYMENT_CONTENT are copied into the package layout (AppX folder) and the .msix.

set(Ymir_UWP_PACKAGE_NAME "Ymir.SaturnEmulator.UWP" CACHE STRING "UWP package identity name")
set(Ymir_UWP_PUBLISHER "CN=CMake Test Cert" CACHE STRING
    "UWP package publisher; must match the subject of the signing certificate")
set(Ymir_UWP_CERTIFICATE "" CACHE FILEPATH
    "Optional .pfx used to sign the UWP package; CMake's test certificate (CN=CMake Test Cert) is used if empty")

set(_ymir_uwp_dir "${CMAKE_CURRENT_SOURCE_DIR}/uwp")

## Package manifest
configure_file("${_ymir_uwp_dir}/Package.appxmanifest.in" "${CMAKE_CURRENT_BINARY_DIR}/Package.appxmanifest" @ONLY)
target_sources(ymir-sdl3 PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/Package.appxmanifest")

## Tile, logo and splash screen assets referenced by the manifest
set(_ymir_uwp_assets
    "${_ymir_uwp_dir}/Assets/LargeTile.png"
    "${_ymir_uwp_dir}/Assets/SmallTile.png"
    "${_ymir_uwp_dir}/Assets/SplashScreen.png"
    "${_ymir_uwp_dir}/Assets/Square150x150Logo.png"
    "${_ymir_uwp_dir}/Assets/Square44x44Logo.png"
    "${_ymir_uwp_dir}/Assets/StoreLogo.png"
    "${_ymir_uwp_dir}/Assets/Wide310x150Logo.png"
)
target_sources(ymir-sdl3 PRIVATE ${_ymir_uwp_assets})
set_source_files_properties(${_ymir_uwp_assets} PROPERTIES
    VS_DEPLOYMENT_CONTENT 1
    VS_DEPLOYMENT_LOCATION "Assets"
)

## Signing certificate
# CMake only signs with its test certificate when it generates the manifest itself, so pass it explicitly.
if (Ymir_UWP_CERTIFICATE)
    set(_ymir_uwp_certificate "${Ymir_UWP_CERTIFICATE}")
else ()
    set(_ymir_uwp_certificate "${CMAKE_CURRENT_BINARY_DIR}/Ymir_TemporaryKey.pfx")
    configure_file("${CMAKE_ROOT}/Templates/Windows/Windows_TemporaryKey.pfx" "${_ymir_uwp_certificate}" COPYONLY)
endif ()
if (NOT EXISTS "${_ymir_uwp_certificate}")
    message(FATAL_ERROR "UWP signing certificate not found: ${_ymir_uwp_certificate}")
endif ()
message(STATUS "Ymir UWP: signing package with ${_ymir_uwp_certificate} (publisher ${Ymir_UWP_PUBLISHER})")
target_sources(ymir-sdl3 PRIVATE "${_ymir_uwp_certificate}")

## Runtime DLLs shipped inside the package
# SDL3-uwp is prebuilt into out/uwp-deps by scripts/uwp/build-deps.ps1 and found through CMAKE_PREFIX_PATH.
get_target_property(_ymir_sdl3_dll SDL3::SDL3-shared IMPORTED_LOCATION_RELEASE)
if (NOT _ymir_sdl3_dll)
    get_target_property(_ymir_sdl3_dll SDL3::SDL3-shared IMPORTED_LOCATION)
endif ()
if (NOT _ymir_sdl3_dll OR NOT EXISTS "${_ymir_sdl3_dll}")
    message(FATAL_ERROR "Could not locate SDL3.dll for the UWP package. Run scripts/uwp/build-deps.ps1 first.")
endif ()
message(STATUS "Ymir UWP: packaging SDL3 from ${_ymir_sdl3_dll}")
target_sources(ymir-sdl3 PRIVATE "${_ymir_sdl3_dll}")
set_source_files_properties("${_ymir_sdl3_dll}" PROPERTIES VS_DEPLOYMENT_CONTENT 1)

## Target properties
set_target_properties(ymir-sdl3 PROPERTIES
    VS_WINDOWS_TARGET_PLATFORM_MIN_VERSION "10.0.17763.0"
)

unset(_ymir_uwp_dir)
unset(_ymir_uwp_assets)
unset(_ymir_sdl3_dll)
unset(_ymir_uwp_certificate)
