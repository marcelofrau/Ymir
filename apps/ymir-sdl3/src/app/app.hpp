#pragma once

#include "cmdline_opts.hpp"

#include "imgui_data.hpp"
#include "settings.hpp"
#include "shared_context.hpp"

#include "services/disc_service.hpp"
#include "services/discord_rpc_service.hpp"
#include "services/display_service.hpp"
#include "services/file_dialog_service.hpp"
#include "services/graphics_service.hpp"
#include "services/input_service.hpp"
#include "services/midi_service.hpp"
#include "services/mouse_capture_service.hpp"
#include "services/persistence_service.hpp"
#include "services/rom_service.hpp"
#include "services/save_state_service.hpp"
#include "services/screenshot_service.hpp"
#include "services/update_checker_service.hpp"
#include "services/window_manager_service.hpp"

#include <chrono>
#include <thread>
#include <vector>

namespace app {

class App {
public:
    App();
    ~App();

    int Run(const CommandLineOptions &options);

private:
    CommandLineOptions m_options;

    YmirImGuiData m_imguiData;
    SharedContext m_context;
    services::GraphicsService m_graphicsService;
    services::SaveStateService m_saveStateService;
    services::MIDIService m_midiService;
    services::ScreenshotService m_screenshotService;
    services::UpdateCheckerService m_updateCheckerService;
    Settings m_settings;
    services::DiscordRPCService m_discordRPCService;
    services::MouseCaptureService m_mouseCaptureService;
    services::ROMService m_romService;
    services::DiscService m_discService;
    services::DisplayService m_displayService;
    services::FileDialogService m_fileDialogService;
    services::WindowManagerService m_windowManagerService;
    services::InputService m_inputService;
    services::PersistenceService m_persistenceService;

    std::thread m_emuThread;
    util::Event m_emuProcessEvent{};

    std::chrono::steady_clock::time_point m_mouseHideTime;

#if YMIR_PLATFORM_UWP
    /// @brief True when the gamepad drives the ImGui layer instead of the emulated Saturn.
    ///
    /// The Xbox has a single gamepad that has to serve both the GUI and the emulated console, so
    /// the input must be routed to one of them at a time. The View button toggles the routing and
    /// a modal dialog always takes the gamepad over while it is open.
    bool m_gamepadGuiFocus = false;

    /// @brief True while an ImGui modal dialog is open, which always captures the gamepad.
    bool m_gamepadGuiModalOpen = false;

    /// @brief True when the gamepad is routed to the ImGui layer rather than to the Saturn.
    bool GamepadDrivesGui() const {
        return m_gamepadGuiFocus || m_gamepadGuiModalOpen;
    }

    /// @brief Position of the virtual mouse cursor, in ImGui display coordinates.
    float m_virtualCursorX = 0.0f;

    /// @brief Position of the virtual mouse cursor, in ImGui display coordinates.
    float m_virtualCursorY = 0.0f;

    /// @brief True once the virtual cursor has been placed inside the display bounds.
    bool m_virtualCursorPlaced = false;

    /// @brief State of the virtual left mouse button.
    bool m_virtualMouseLeft = false;

    /// @brief State of the virtual right mouse button.
    bool m_virtualMouseRight = false;

    /// @brief Feeds the virtual mouse cursor and its buttons to ImGui.
    ///
    /// The right analog stick moves the cursor and the shoulder buttons act as the mouse buttons,
    /// so the whole ImGui layer becomes reachable without a physical mouse. The cursor is only
    /// alive while the gamepad drives the GUI.
    void UpdateVirtualMouse();

    /// @brief Sets the state of one of the virtual mouse buttons.
    ///
    /// @param button One of the ImGuiMouseButton_* values.
    /// @param down   True when the button is pressed, false when it is released.
    void SetVirtualMouseButton(int button, bool down);
#endif

    void RunEmulator();

    void StartEmulatorThread();
    void StopEmulatorThread();
    void EmulatorThread();

    void EnableRewindBuffer(bool enable);
    void ToggleRewindBuffer();

    static void OnMidiInputReceived(double delta, std::vector<unsigned char> *msg, void *userData);

    // Rewind bar
    std::chrono::steady_clock::time_point m_rewindBarFadeTimeBase;
};

} // namespace app
