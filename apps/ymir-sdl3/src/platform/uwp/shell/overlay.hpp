#pragma once

/**
@file
@brief Overlay windows of the Xbox shell: settings, pause menu and any other "application" over the shell or the
game, driven by the gamepad.

An overlay is a stack of screens. Each screen has one or more pages (tabs, switched with LB/RB) of rows, and every
row is one of a handful of kinds (action, toggle, choice, slider, information). Pages are plain data with getter and
setter callbacks, so a new settings page is a list of rows rather than new drawing code:

- d-pad up/down moves between rows (ImGui navigation);
- A activates an action, flips a toggle or steps a choice forward;
- d-pad left/right steps a choice or a slider;
- B goes back to the previous screen, or closes the overlay from the first one.

The overlay is drawn as a hologram panel in its own ImGui window, over the shell window and under the desktop
windows, so a desktop tool opened from it (the binds editor, for instance) still shows on top.

Only compiled when YMIR_PLATFORM_UWP is set.
*/

#include <platform/uwp/shell/widgets.hpp>

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace platform::uwp::shell {

/// @brief A row of an overlay page.
struct OverlayRow {
    enum class Kind { Action, Toggle, Choice, Slider, Info };

    Kind kind = Kind::Info;
    std::string label;
    std::string help; // shown at the bottom of the panel while the row is focused

    std::function<void()> activate;         // Action
    std::function<std::string()> valueText; // Action and Info: text on the right; optional

    std::function<bool()> getToggle; // Toggle
    std::function<void(bool)> setToggle;

    std::vector<std::string> choices; // Choice
    std::function<int()> getChoice;
    std::function<void(int)> setChoice;

    float minValue = 0.0f; // Slider
    float maxValue = 1.0f;
    float step = 0.05f;
    std::function<float()> getValue;
    std::function<void(float)> setValue;
    std::function<std::string(float)> formatValue;

    std::function<bool()> enabled; // optional; disabled rows are dimmed and ignore input

    /// @brief Creates an action row.
    static OverlayRow Action(std::string label, std::string help, std::function<void()> activate,
                             std::function<std::string()> valueText = {});

    /// @brief Creates a toggle row.
    static OverlayRow Toggle(std::string label, std::string help, std::function<bool()> get,
                             std::function<void(bool)> set);

    /// @brief Creates a choice row.
    static OverlayRow Choice(std::string label, std::string help, std::vector<std::string> choices,
                             std::function<int()> get, std::function<void(int)> set);

    /// @brief Creates a slider row.
    static OverlayRow Slider(std::string label, std::string help, float minValue, float maxValue, float step,
                             std::function<float()> get, std::function<void(float)> set,
                             std::function<std::string(float)> format);

    /// @brief Creates an information row.
    static OverlayRow Info(std::string label, std::function<std::string()> valueText, std::string help = {});
};

/// @brief A page (tab) of an overlay screen.
struct OverlayPage {
    std::string title;
    std::vector<OverlayRow> rows;
};

/// @brief A screen of an overlay: a title and its pages.
struct OverlayScreen {
    std::string title;
    std::vector<OverlayPage> pages;
};

/// @brief A stack of overlay screens drawn over the shell or the game.
class Overlay {
public:
    /// @brief Opens a screen on top of the current one (or as the first one).
    void Push(OverlayScreen screen);

    /// @brief Closes the top screen.
    void Pop();

    /// @brief Replaces the top screen with a new version of it, keeping the selected page, for screens whose rows
    /// change (a removed list entry, for instance). Applied after the current frame.
    void Replace(OverlayScreen screen);

    /// @brief Closes every screen.
    void CloseAll();

    /// @brief Determines whether any screen is open.
    [[nodiscard]] bool IsOpen() const {
        return !m_stack.empty();
    }

    /// @brief Draws the top screen and handles its input. Call once per frame after the shell window.
    /// @param[in] ctx the draw context; its draw list is replaced by the overlay window's
    /// @param[in] dimBackground whether to darken everything behind the panel
    void Draw(const DrawContext &ctx, bool dimBackground);

    /// @brief Retrieves the controller legend for the focused row.
    [[nodiscard]] std::vector<ButtonHint> Legend() const;

    /// @brief Determines whether the overlay window had the focus on the last frame.
    [[nodiscard]] bool HasFocus() const {
        return m_hasFocus;
    }

private:
    struct State {
        OverlayScreen screen;
        std::size_t page = 0;
        std::size_t focusedRow = 0;
        bool focusRequested = true;
        std::vector<float> rowFocus; // eased focus per row, for the highlight animation
    };

    void DrawRow(const DrawContext &ctx, State &state, std::size_t index, Rect rect, bool focused);
    void HandleInput(State &state);

    std::vector<State> m_stack;
    std::vector<OverlayScreen> m_pendingPushes; // Push() calls from inside row callbacks, applied after the frame
    std::optional<OverlayScreen> m_pendingReplace;
    bool m_drawing = false;
    bool m_hasFocus = false;
    bool m_focusWindow = false;
    int m_closeRequests = 0; // Pop() calls from inside row callbacks, applied after the frame
};

} // namespace platform::uwp::shell
