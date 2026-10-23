## Create the executable target
add_executable(ymir-sdl3-tests
    src/util/stdio_suppress_tests.cpp
    src/platform/uwp/shell/audio_cube_tests.cpp
    src/platform/uwp/shell/dot_matrix_tests.cpp
    src/platform/uwp/shell/layout_tests.cpp
    src/platform/uwp/shell/marquee_tests.cpp
    src/platform/uwp/shell/quad_mesh_tests.cpp
    src/platform/uwp/shell/starfield_tests.cpp

    ${Ymir_SOURCE_DIR}/apps/ymir-sdl3/src/util/stdio_suppress.cpp

    # The Xbox shell is only part of the UWP app, but its layout, starfield, marquee, mesh, dot-matrix and audio cube
    # logic is plain C++ and is tested here
    ${Ymir_SOURCE_DIR}/apps/ymir-sdl3/src/platform/uwp/shell/audio_cube.cpp
    ${Ymir_SOURCE_DIR}/apps/ymir-sdl3/src/platform/uwp/shell/dot_matrix.cpp
    ${Ymir_SOURCE_DIR}/apps/ymir-sdl3/src/platform/uwp/shell/layout.cpp
    ${Ymir_SOURCE_DIR}/apps/ymir-sdl3/src/platform/uwp/shell/marquee.cpp
    ${Ymir_SOURCE_DIR}/apps/ymir-sdl3/src/platform/uwp/shell/quad_mesh.cpp
    ${Ymir_SOURCE_DIR}/apps/ymir-sdl3/src/platform/uwp/shell/starfield.cpp
)
add_executable(ymir::ymir-sdl3-tests ALIAS ymir-sdl3-tests)
set_target_properties(ymir-sdl3-tests PROPERTIES
                      VERSION ${Ymir_VERSION}
                      SOVERSION ${Ymir_VERSION_MAJOR})
target_link_libraries(ymir-sdl3-tests PRIVATE ymir::ymir-core)
target_compile_features(ymir-sdl3-tests PUBLIC cxx_std_20)

## The tested sources live with the frontend, not in this folder.
target_include_directories(ymir-sdl3-tests PRIVATE ${Ymir_SOURCE_DIR}/apps/ymir-sdl3/src)

find_package(Catch2 CONFIG REQUIRED)

## Add dependencies
target_link_libraries(ymir-sdl3-tests PRIVATE fmt::fmt Catch2::Catch2WithMain)

cmrk_copy_runtime_dlls(ymir-sdl3-tests)

## Enable LTO if supported
include(CheckIPOSupported)
check_ipo_supported(RESULT IPO_SUPPORTED OUTPUT IPO_ERROR)

if (IPO_SUPPORTED AND Ymir_ENABLE_IPO)
    message(STATUS "Enabling IPO / LTO for ymir-sdl3-tests")
    set_property(TARGET ymir-sdl3-tests PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
endif ()

## Apply performance options
if (Ymir_AVX2)
    if (MSVC)
        target_compile_options(ymir-sdl3-tests PUBLIC "/arch:AVX2")
    else ()
        target_compile_options(ymir-sdl3-tests PUBLIC "-mavx2" "-mfma" "-mpopcnt" "-mlzcnt" "-mbmi" "-mbmi2")
    endif ()
endif ()

## Configure Visual Studio solution
if (MSVC)
    vs_set_filters(TARGET ymir-sdl3-tests)
    set_target_properties(ymir-sdl3-tests PROPERTIES FOLDER "Ymir-tests")
endif ()

## Register Catch2 test with CTest
include(Catch)
catch_discover_tests(ymir-sdl3-tests)

## No packaging for this project as it's meant for unit tests
