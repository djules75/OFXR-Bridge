// Exercise the production handlers without launching a real tray, installing
// a layer, or touching Khronos registry keys. Every mutation is confined to a
// uniquely named test directory and Software/OFXRBridgeTest subkey.
#include "../src/tray/main.cpp"

#include <iostream>
#include <thread>

namespace {
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

struct Fixture {
    AppState state;
    Fixture() {
        const auto id = std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64());
        state.local_directory = std::filesystem::temp_directory_path() / (L"ofxr-lifecycle-" + id);
        state.registry_subkey = L"Software\\OFXRBridgeTest\\lifecycle-" + id;
        std::filesystem::create_directories(state.local_directory);
    }
    ~Fixture() {
        RegDeleteTreeW(HKEY_CURRENT_USER, state.registry_subkey.c_str());
        // This is only the exact directory created by this fixture.
        std::error_code ignored;
        if (state.local_directory.is_absolute() &&
            state.local_directory.filename().wstring().starts_with(L"ofxr-lifecycle-"))
            std::filesystem::remove_all(state.local_directory, ignored);
    }
    std::filesystem::path seed(std::wstring_view version, unsigned id, bool json = true) {
        const auto path = state.local_directory / L"RuntimeLayer" / version /
            (L"XR_APILAYER_XRFrameBridge_manual-42-" + std::to_wstring(id) + L".json");
        std::filesystem::create_directories(path.parent_path());
        if (json) std::ofstream(path) << "{}";
        require(xrfg::implicit_layer::register_manifest(
            path, xrfg::implicit_layer::RegistryScope::current_user,
            nullptr, state.registry_subkey), "seed registration");
        return path;
    }
    bool registered(const std::filesystem::path& path) {
        return xrfg::implicit_layer::manifest_registered(
            path, xrfg::implicit_layer::RegistryScope::current_user,
            state.registry_subkey);
    }
};

void cross_version_cleanup() {
    Fixture test;
    const auto old = test.seed(L"v065", 1);
    const auto current = test.seed(L"v069", 2);
    const auto missing = test.seed(L"v068", 3, false);
    const auto legacy = test.seed(L"", 4);
    const auto foreign = current.parent_path() / L"XR_APILAYER_another_vendor.json";
    const auto outside = test.state.local_directory / L"Outside" / old.filename();
    std::filesystem::create_directories(outside.parent_path());
    std::ofstream(foreign) << "foreign";
    std::ofstream(outside) << "outside";
    require(xrfg::implicit_layer::register_manifest(
        foreign, xrfg::implicit_layer::RegistryScope::current_user,
        nullptr, test.state.registry_subkey), "foreign registration");
    require(xrfg::implicit_layer::register_manifest(
        outside, xrfg::implicit_layer::RegistryScope::current_user,
        nullptr, test.state.registry_subkey), "outside registration");
    const auto orphan = current.parent_path() / L"XR_APILAYER_XRFrameBridge_manual-99-99.json";
    std::ofstream(orphan) << "orphan";
    const auto dll = old.parent_path() / L"XR_APILAYER_XRFrameBridge_diagnostic.dll";
    const auto ini = old.parent_path() / L"ofxr_bridge.ini";
    const auto log = old.parent_path() / L"flight.log";
    std::ofstream(dll) << "untouched DLL fixture";
    std::ofstream(ini) << "untouched settings";
    std::ofstream(log) << "untouched log";
    std::wstring error;
    // Reopened/disarmed tray: no in-memory manifest and armed=false.
    require(!test.state.armed && test.state.armed_manifest.empty(), "fresh tray state");
    require(disarm_bridge(test.state, &error), "fresh tray disarm all versions");
    for (const auto& path : {old, current, missing, legacy, orphan}) {
        require(!test.registered(path), "owned registration removed");
        require(!std::filesystem::exists(path), "owned JSON removed");
    }
    require(test.registered(foreign) && test.registered(outside), "foreign registrations untouched");
    for (const auto& path : {foreign, outside, dll, ini, log})
        require(std::filesystem::exists(path), "unrelated files untouched");
    require(disarm_bridge(test.state, &error), "repeated cleanup is idempotent");
    require(std::filesystem::exists(test.state.local_directory / L"tray-lifecycle.log"), "lifecycle log independent of recorder");
    const auto rearmed = test.seed(L"v069", 5);
    require(test.registered(rearmed) && !test.registered(old), "upgrade leaves only the new registration");
    // An old watchdog is scoped to its old lease: it cannot disarm a new arm.
    require(xrfg::implicit_layer::retire_manifest(
        old, xrfg::implicit_layer::RegistryScope::current_user,
        &error, test.state.registry_subkey), "late old watchdog");
    require(test.registered(rearmed), "late watchdog preserves new arm");
}

void ownership() {
    const std::filesystem::path root = L"C:\\Users\\Test\\OFXR Bridge";
    const auto valid = root / L"RuntimeLayer\\v065\\XR_APILAYER_XRFrameBridge_manual-32292-11205031.json";
    require(xrfg::implicit_layer::owned_registration_path(valid, root), "reported v065 value accepted");
    for (const auto& bad : {
        root / L"RuntimeLayer\\v065\\XR_APILAYER_XRFrameBridge_manual-abc.json",
        root / L"RuntimeLayer\\v065\\XR_APILAYER_XRFrameBridge_manual-1-2-3.json",
        root / L"RuntimeLayer\\v065\\XR_APILAYER_XRFrameBridge_manual--2.json",
        root / L"RuntimeLayer\\v065\\nested\\XR_APILAYER_XRFrameBridge_manual-1-2.json",
        root / L"RuntimeLayer\\v065\\..\\v068\\XR_APILAYER_XRFrameBridge_manual-1-2.json",
        root / L"RuntimeLayer\\other\\XR_APILAYER_XRFrameBridge_manual-1-2.json",
        std::filesystem::path(L"relative\\RuntimeLayer\\v065\\XR_APILAYER_XRFrameBridge_manual-1-2.json")})
        require(!xrfg::implicit_layer::owned_registration_path(bad, root), "unsafe/unrelated path rejected");
}

HWND make_window(AppState& state);

// "Pause frame generation": a switch on the armed bridge that goes both ways
// and ends with the arm.
void pause_switch() {
    Fixture test;
    const auto manifest = test.state.local_directory / L"RuntimeLayer" / L"v001" /
        L"XR_APILAYER_XRFrameBridge_manual-42-77.json";
    const auto signalled = [&] {
        return WaitForSingleObject(test.state.pause_signal, 0) == WAIT_OBJECT_0;
    };
    test.state.pause_signal = xrfg::implicit_layer::create_pause_signal(manifest);
    require(test.state.pause_signal != nullptr, "create pause signal");
    handle_command(test.state, toggle_pause);
    require(!test.state.paused && !signalled(), "no pause while disarmed");
    test.state.armed = true;
    handle_command(test.state, toggle_pause);
    require(test.state.paused && signalled(), "pause sets the signal");
    require(tray_tooltip(test.state).starts_with(L"OFXR Bridge PAUSED"), "tooltip says paused");
    // What a running session holds: its own handle on the same event.
    const HANDLE reader = OpenEventW(
        SYNCHRONIZE, FALSE, xrfg::implicit_layer::pause_signal_name(manifest).c_str());
    require(reader != nullptr, "layer-side name opens the same event");
    require(WaitForSingleObject(reader, 0) == WAIT_OBJECT_0, "reader sees the pause");
    handle_command(test.state, toggle_pause);
    require(!test.state.paused && !signalled(), "resume resets the signal");
    require(WaitForSingleObject(reader, 0) == WAIT_TIMEOUT, "reader sees the resume");
    CloseHandle(reader);
    handle_command(test.state, toggle_pause);
    require(test.state.paused, "paused again before the disarm");
    const HBITMAP symbol = create_pause_bitmap();
    BITMAP described{};
    require(symbol != nullptr && GetObjectW(symbol, sizeof(described), &described) != 0 &&
            described.bmBitsPixel == 32 && described.bmWidth == described.bmHeight,
            "pause symbol is a square 32-bit bitmap");
    DeleteObject(symbol);
    // The key does what the menu entry does, and is given back at disarm.
    // A chord nothing else on a developer's machine is likely to hold.
    test.state.settings.pause_hotkey = "ctrl+alt+shift+f24";
    const HWND window = make_window(test.state);
    register_pause_hotkey(test.state);
    require(test.state.pause_hotkey_registered, "pause key registered while armed");
    SendMessageW(window, WM_HOTKEY, kPauseHotkeyId, 0);
    require(!test.state.paused && !signalled(), "pause key resumes");
    SendMessageW(window, WM_HOTKEY, kPauseHotkeyId, 0);
    require(test.state.paused && signalled(), "pause key pauses");
    // A new key from the dialog is saved and taken at once while armed.
    test.state.settings_path = test.state.local_directory / L"tray.ini";
    apply_pause_hotkey(test.state, "ctrl+alt+shift+f23");
    require(test.state.pause_hotkey_registered &&
            test.state.settings.pause_hotkey == "ctrl+alt+shift+f23",
        "a changed key is registered while armed");
    {
        std::ifstream saved(test.state.settings_path);
        const std::string text((std::istreambuf_iterator<char>(saved)), {});
        require(text.find("pause_hotkey=ctrl+alt+shift+f23") != std::string::npos,
            "a changed key is saved to tray.ini");
    }
    require(RegisterHotKey(window, kPauseHotkeyId + 1, MOD_CONTROL | MOD_ALT | MOD_SHIFT, VK_F24) != FALSE,
        "the old chord is given back when the key changes");
    UnregisterHotKey(window, kPauseHotkeyId + 1);
    require(pause_key_display("ctrl+alt+f7") == L"Ctrl + Alt + F7" &&
            pause_key_display("off") == L"none", "the binding as the menu shows it");
    require(hotkey_from_control(hotkey_control_value({MOD_CONTROL | MOD_ALT, VK_F7})) ==
            xrfg::standalone::Hotkey{MOD_CONTROL | MOD_ALT, VK_F7} &&
            hotkey_from_control(hotkey_control_value({MOD_SHIFT, VK_NEXT})) ==
            xrfg::standalone::Hotkey{MOD_SHIFT, VK_NEXT},
        "a chord survives the hotkey control's own encoding");
    apply_pause_hotkey(test.state, "ctrl+alt+shift+f24");
    std::wstring error;
    require(disarm_bridge(test.state, &error), "disarm while paused");
    require(!test.state.paused && test.state.pause_signal == nullptr, "pause ends with the arm");
    require(!test.state.pause_hotkey_registered, "pause key released with the arm");
    require(RegisterHotKey(window, kPauseHotkeyId, MOD_CONTROL | MOD_ALT | MOD_SHIFT, VK_F24) != FALSE,
        "the chord is free again after the disarm");
    UnregisterHotKey(window, kPauseHotkeyId);
    // Destroying the window runs the tray's own cleanup, as a real exit does.
    DestroyWindow(window);
}

HWND make_window(AppState& state) {
    static bool registered = false;
    if (!registered) {
        WNDCLASSW window_class{};
        window_class.hInstance = GetModuleHandleW(nullptr);
        window_class.lpfnWndProc = window_procedure;
        window_class.lpszClassName = L"OFXRBridgeLifecycleTest";
        require(RegisterClassW(&window_class) != 0, "register hidden test window");
        registered = true;
    }
    const auto window = CreateWindowW(L"OFXRBridgeLifecycleTest", L"OFXR lifecycle test",
        WS_OVERLAPPED, 0, 0, 1, 1, nullptr, nullptr, GetModuleHandleW(nullptr), &state);
    require(window != nullptr, "create hidden test window");
    return window;
}

void window_lifecycle() {
    for (const bool menu_quit : {false, true}) {
        Fixture test;
        const auto old = test.seed(L"v065", menu_quit ? 10 : 11);
        const HWND window = make_window(test.state);
        if (menu_quit) handle_command(test.state, exit_application);
        else SendMessageW(window, WM_CLOSE, 0, 0);
        require(!IsWindow(window), "Quit/close destroys window only after cleanup");
        require(!test.registered(old) && !std::filesystem::exists(old), "Quit/close cleans old versions when unarmed");
    }
    Fixture test;
    const auto old = test.seed(L"v065", 20);
    const HWND window = make_window(test.state);
    require(SendMessageW(window, WM_QUERYENDSESSION, 0, ENDSESSION_LOGOFF) == TRUE, "shutdown query accepted after cleanup");
    require(!test.registered(old) && !std::filesystem::exists(old), "cleanup completes before shutdown query returns");
    SendMessageW(window, WM_ENDSESSION, FALSE, 0);
    require(IsWindow(window) && !test.state.armed, "cancelled shutdown stays disarmed");
    const auto newer = test.seed(L"v069", 21);
    SendMessageW(window, WM_ENDSESSION, TRUE, ENDSESSION_LOGOFF);
    require(!IsWindow(window) && !test.registered(newer), "actual logoff repeats cleanup");
}

void blocked_manifest() {
    Fixture test;
    const auto blocked = test.seed(L"v065", 30);
    const auto other = test.seed(L"v068", 31);
    const HANDLE file = CreateFileW(blocked.c_str(), GENERIC_READ, FILE_SHARE_READ,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    require(file != INVALID_HANDLE_VALUE, "lock JSON fixture");
    const HWND window = make_window(test.state);
    const auto result = SendMessageW(window, WM_QUERYENDSESSION, 0, 0);
    CloseHandle(file);
    require(result == FALSE, "failed cleanup does not report shutdown ready");
    require(!test.registered(blocked) && !test.registered(other), "disable registrations even when JSON is locked");
    require(!std::filesystem::exists(other), "one failure does not skip other versions");
    require(std::filesystem::exists(blocked), "locked JSON preserved for retry");
    require(SendMessageW(window, WM_QUERYENDSESSION, 0, 0) == TRUE, "cleanup retry succeeds");
    SendMessageW(window, WM_ENDSESSION, TRUE, 0);
    require(!std::filesystem::exists(blocked), "retry removes orphaned JSON");
}

void runtime_stop_and_watchdog() {
    Fixture test;
    const auto manifest = test.seed(L"v069", 40);
    HANDLE event = xrfg::implicit_layer::create_arm_signal(manifest);
    require(event != nullptr, "create arm event");
    require(write_runtime_configuration(test.state, nullptr, manifest), "write private control configuration");
    xrfg::implicit_layer::ManualArmControl reader(runtime_directory(test.state.local_directory));
    require(!reader.stop_requested(), "loaded DLL initially enabled");
    // Reopened tray has neither the old event handle nor the old manifest path.
    const HWND window = make_window(test.state);
    SendMessageW(window, WM_CLOSE, 0, 0);
    require(reader.stop_requested(), "Quit signals loaded DLL through registry discovery");
    require(!IsWindow(window) && !test.registered(manifest), "Quit removes registration before exit");
    CloseHandle(event);

    const auto next = test.seed(L"v069", 41);
    event = xrfg::implicit_layer::create_arm_signal(next);
    require(event != nullptr, "new arm has a distinct event");
    require(write_runtime_configuration(test.state, nullptr, next), "new arm control configuration");
    xrfg::implicit_layer::ManualArmControl next_reader(runtime_directory(test.state.local_directory));
    require(!next_reader.stop_requested() && reader.stop_requested(), "new arm never resurrects old session");
    const HANDLE parent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    bool cleaned = false;
    std::wstring error;
    std::thread watchdog([&] { cleaned = watch_registered_arm(parent, next,
        test.state.local_directory, &error,
        xrfg::implicit_layer::RegistryScope::current_user,
        test.state.registry_subkey); });
    SetEvent(parent); // A signalled process handle uses the identical wait path.
    watchdog.join();
    CloseHandle(parent);
    require(cleaned && next_reader.stop_requested() && !test.registered(next), "watchdog disables reader and unregisters after owner exit");
    CloseHandle(event);

    // Missing event is fail-closed for managed DLLs; embedded INIs have no lease.
    const auto absent = test.state.local_directory / L"RuntimeLayer\\v069\\XR_APILAYER_XRFrameBridge_manual-8-8.json";
    require(write_runtime_configuration(test.state, nullptr, absent), "write absent lease");
    xrfg::implicit_layer::ManualArmControl absent_reader(runtime_directory(test.state.local_directory));
    require(absent_reader.stop_requested(), "orphaned managed DLL starts disabled");
    xrfg::implicit_layer::ManualArmControl embedded(test.state.local_directory / L"embedded");
    require(!embedded.stop_requested(), "embedded layer without tray control unchanged");
}

void integrity_scope_policy() {
    using xrfg::implicit_layer::RegistryScope;
    require(xrfg::implicit_layer::registry_scope_for_integrity_rid(
                SECURITY_MANDATORY_MEDIUM_RID) == RegistryScope::current_user,
        "medium integrity must use HKCU");
    require(xrfg::implicit_layer::registry_scope_for_integrity_rid(
                SECURITY_MANDATORY_HIGH_RID) == RegistryScope::local_machine,
        "high integrity must use HKLM");
    require(xrfg::implicit_layer::registry_scope_for_integrity_rid(
                SECURITY_MANDATORY_SYSTEM_RID) == RegistryScope::local_machine,
        "system integrity must use HKLM");
    const auto current = xrfg::implicit_layer::preferred_registry_scope();
    require(current == RegistryScope::current_user ||
            current == RegistryScope::local_machine,
        "current process scope must be valid");
}
} // namespace

int main() {
    try {
        ownership();
        cross_version_cleanup();
        window_lifecycle();
        pause_switch();
        blocked_manifest();
        runtime_stop_and_watchdog();
        integrity_scope_policy();
        std::cout << "V074 dual-hive registry recovery and production tray lifecycle checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
