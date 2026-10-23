# Embeds the art assets of the Xbox shell (theme manifest, PNG and JPEG files) into the executable.
#
# Every file under uwp/shell/assets/ is embedded, so adding artwork only requires dropping the image in that folder and
# listing it in theme.toml. Re-run the configure step (scripts/uwp/build.ps1 -Reconfigure) after adding files.
#
# Included from apps/ymir-sdl3/CMakeLists.txt only when Ymir_PLATFORM_UWP is set.

file(GLOB_RECURSE Ymir_SHELL_ASSET_FILES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/uwp/shell/assets/*.toml"
    "${CMAKE_CURRENT_SOURCE_DIR}/uwp/shell/assets/*.png"
    "${CMAKE_CURRENT_SOURCE_DIR}/uwp/shell/assets/*.jpg"
    "${CMAKE_CURRENT_SOURCE_DIR}/uwp/shell/assets/*.ttf"
    "${CMAKE_CURRENT_SOURCE_DIR}/uwp/shell/assets/*.mp3"
)

cmrc_add_resource_library(ymir-sdl3-shell-rc
    ALIAS ymir-sdl3::shell-rc
    NAMESPACE Ymir_sdl3_shell_rc
    WHENCE uwp/shell/assets

    ${Ymir_SHELL_ASSET_FILES}
)

if (MSVC)
    set_target_properties(ymir-sdl3-shell-rc PROPERTIES FOLDER "Ymir")
endif ()
