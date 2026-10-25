// Adapted from elliotttate/OFXR-Bridge b8048568 (release 36), LGPL-3.0-or-later.
#include "xrfg/dlssg_profile.hpp"
#include "xrfg/autonomous_control.hpp"

#include <windows.h>

#include <cstdint>
#include <cwchar>
#include <functional>
#include <initializer_list>
#include <fstream>
#include <utility>

namespace xrfg::standalone {
namespace {

// Minimal public NVAPI DRS ABI (version 1), not private NGX parameters:
// https://github.com/NVIDIA/nvapi/blob/main/nvapi.h
// https://github.com/NVIDIA/nvapi/blob/main/nvapi_interface.h
// https://github.com/NVIDIA/nvapi/blob/main/NvApiDriverSettings.h
using U32 = std::uint32_t;
using Handle = void*;
constexpr U32 kFgOverride = 0x10E41E03;
constexpr U32 kFgPreset = 0x10E41DF1;
constexpr int kSettingNotFound = -160;
constexpr int kApplicationNotFound = -166;
constexpr std::size_t kStringLength = 2048;

union Value { U32 dword; unsigned char storage[4100]; };
struct Setting {
    U32 version;
    wchar_t name[kStringLength];
    U32 id, type, location, predefined, valid;
    Value default_value, current;
};
struct Application {
    U32 version, predefined;
    wchar_t name[kStringLength], friendly_name[kStringLength], launcher[kStringLength];
};
struct Profile {
    U32 version;
    wchar_t name[kStringLength];
    U32 gpu, predefined, applications, settings;
};
static_assert(sizeof(Setting) == 12320 && sizeof(Application) == 12296 && sizeof(Profile) == 4116);

template<class T> T versioned() {
    T result{};
    result.version = static_cast<U32>(sizeof(T)) | (1U << 16);
    return result;
}

void check(int status, const wchar_t* operation) {
    if (status != 0) throw std::wstring(operation) + L" failed (NVAPI " + std::to_wstring(status) + L").";
}

class Driver {
    HMODULE library{};
    using Query = void* (__cdecl*)(U32);
    Query query{};
    template<class... Args> using Fn = int (__cdecl*)(Args...);
    Fn<> unload{};
    bool initialized{};
    template<class... Args> Fn<Args...> bind(U32 id) {
        const auto address = query(id);
        if (!address) throw std::wstring(L"The NVIDIA driver does not expose the required profile interface.");
        return reinterpret_cast<Fn<Args...>>(address);
    }
public:
    Handle session{}, profile{};
    Fn<Handle> destroy{}, load{}, save{};
    Fn<Handle, const wchar_t*, Handle*, Application*> find{};
    Fn<Handle, Profile*, Handle*> create_profile{};
    Fn<Handle, Handle, Application*> create_application{};
    Fn<Handle, Handle, Profile*> profile_info{};
    Fn<Handle, Handle, U32, Setting*> get{};
    Fn<Handle, Handle, Setting*> set{};
    Fn<Handle, Handle, U32> remove{};

    Driver() {
        try {
            library = LoadLibraryExW(L"nvapi64.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
            if (!library) throw std::wstring(L"An NVIDIA driver with 64-bit NVAPI is required.");
            query = reinterpret_cast<Query>(GetProcAddress(library, "nvapi_QueryInterface"));
            if (!query) throw std::wstring(L"The NVIDIA profile interface is unavailable.");
            unload = bind<>(0xD22BDD7E);
            check(bind<>(0x0150E828)(), L"Initializing NVIDIA profiles");
            initialized = true;
            destroy = bind<Handle>(0xDAD9CFF8);
            load = bind<Handle>(0x375DBD6B);
            save = bind<Handle>(0xFCBC7E14);
            find = bind<Handle, const wchar_t*, Handle*, Application*>(0xEEE566B2);
            create_profile = bind<Handle, Profile*, Handle*>(0xCC176068);
            create_application = bind<Handle, Handle, Application*>(0x4347A9DE);
            profile_info = bind<Handle, Handle, Profile*>(0x61CD6FD6);
            get = bind<Handle, Handle, U32, Setting*>(0x73BF8338);
            set = bind<Handle, Handle, Setting*>(0x577DD202);
            remove = bind<Handle, Handle, U32>(0xE4A26362);
            check(bind<Handle*>(0x0694D52E)(&session), L"Opening NVIDIA profiles");
            check(load(session), L"Reading NVIDIA profiles");
        } catch (...) { close(); throw; }
    }
    Driver(const Driver&) = delete;
    Driver& operator=(const Driver&) = delete;
    ~Driver() { close(); }
    void close() noexcept {
        if (session && destroy) destroy(std::exchange(session, nullptr));
        if (std::exchange(initialized, false) && unload) unload();
        if (library) FreeLibrary(std::exchange(library, nullptr));
    }
    bool locate(const std::wstring& executable) {
        auto application = versioned<Application>();
        const int status = find(session, executable.c_str(), &profile, &application);
        if (status == kApplicationNotFound) { profile = nullptr; return false; }
        check(status, L"Finding the game's NVIDIA profile");
        return true;
    }
    Setting setting(U32 id) {
        auto value = versioned<Setting>();
        value.id = id;
        const int status = get(session, profile, id, &value);
        if (status == kSettingNotFound) { value.location = 1; return value; }
        check(status, L"Reading the DLSS FG preset");
        return value;
    }
};

std::wstring validated_path(const std::filesystem::path& executable) {
    if (!executable.is_absolute() || !std::filesystem::is_regular_file(executable) ||
        _wcsicmp(executable.extension().c_str(), L".exe") != 0) {
        throw std::wstring(L"Choose the game's existing .exe file using its full path.");
    }
    const auto path = executable.lexically_normal().wstring();
    if (path.size() >= kStringLength) throw std::wstring(L"The game path is too long for NVIDIA profiles.");
    return path;
}

bool custom(const Setting& value) { return value.location == 0 && value.predefined == 0; }

template<class Action> bool protect(Action action, std::wstring& error) noexcept {
    error.clear();
    try { action(); return true; }
    catch (const std::wstring& message) { error = message; }
    catch (...) { error = L"Could not access the game's NVIDIA profile."; }
    return false;
}
} // namespace

bool read_dlssg_profile(const std::filesystem::path& executable,
                       DlssgProfile& result, std::wstring& error) noexcept {
    result = {};
    return protect([&] {
        const auto path = validated_path(executable);
        Driver driver;
        if (!driver.locate(path)) return;
        auto info = versioned<Profile>();
        check(driver.profile_info(driver.session, driver.profile, &info), L"Reading the profile name");
        result.name = info.name;
        const auto enabled = driver.setting(kFgOverride);
        const auto preset = driver.setting(kFgPreset);
        result.enabled = enabled.current.dword == 1;
        result.preset = preset.current.dword;
        result.custom_override = custom(enabled) || custom(preset);
    }, error);
}

static bool write_exact_profile(const std::filesystem::path& executable,
                        DlssgPreset preset, std::wstring& error) noexcept {
    return protect([&] {
        if (preset != DlssgPreset::game_default && preset != DlssgPreset::a && preset != DlssgPreset::c)
            throw std::wstring(L"Unsupported DLSS Frame Generation preset.");
        const auto path = validated_path(executable);
        // New session per operation: never save a stale snapshot retained
        // while the NVIDIA app or another OFXR menu was changing settings.
        Driver driver;
        if (!driver.locate(path)) {
            if (preset == DlssgPreset::game_default) return;
            auto info = versioned<Profile>();
            const auto name = L"OFXR DLSS FG: " + executable.filename().wstring() + L" " +
                              std::to_wstring(std::hash<std::wstring>{}(path));
            wcscpy_s(info.name, name.c_str());
            check(driver.create_profile(driver.session, &info, &driver.profile), L"Creating the game's NVIDIA profile");
            auto application = versioned<Application>();
            wcscpy_s(application.name, path.c_str());
            check(driver.create_application(driver.session, driver.profile, &application), L"Adding the game to its NVIDIA profile");
        }
        for (U32 id : {kFgOverride, kFgPreset}) {
            if (preset == DlssgPreset::game_default) {
                const int status = driver.remove(driver.session, driver.profile, id);
                if (status != kSettingNotFound) check(status, L"Removing the game's DLSS FG override");
            } else {
                auto value = versioned<Setting>();
                value.id = id;
                value.current.dword = id == kFgOverride ? 1U : static_cast<U32>(preset);
                check(driver.set(driver.session, driver.profile, &value), L"Selecting the DLSS FG preset");
            }
        }
        // Both edits succeed before the single save. Other profile keys and
        // the global profile are untouched.
        check(driver.save(driver.session), L"Saving the DLSS FG preset");
        check(driver.load(driver.session), L"Verifying the DLSS FG preset");
        if (!driver.locate(path)) throw std::wstring(L"The game's NVIDIA profile disappeared after saving.");
        const auto enabled = driver.setting(kFgOverride);
        const auto selected = driver.setting(kFgPreset);
        if (preset == DlssgPreset::game_default) {
            if (custom(enabled) || custom(selected)) throw std::wstring(L"The driver retained a custom FG override.");
        } else if (enabled.current.dword != 1 || selected.current.dword != static_cast<U32>(preset) ||
                   enabled.location != 0 || selected.location != 0) {
            throw std::wstring(L"The driver did not retain the requested FG preset.");
        }
    }, error);
}
bool write_dlssg_profile(const std::filesystem::path& executable,
                        DlssgPreset preset, std::wstring& error, DlssgPreset* saved) noexcept {
    if (write_exact_profile(executable, preset, error)) {
        if (saved) *saved = preset;
        return true;
    }
    if (preset != DlssgPreset::c) return false;
    const auto c_error = error;
    if (write_exact_profile(executable, DlssgPreset::a, error)) {
        if (saved) *saved = DlssgPreset::a;
        return true;
    }
    error = L"Preset C could not be selected: " + c_error + L" Preset A fallback also failed: " + error;
    return false;
}

std::filesystem::path game_executable(unsigned process_id) noexcept {
    if (!process_id) return {};
    const auto process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, process_id);
    if (!process) return {};
    wchar_t path[32768]{}; DWORD length = 32768;
    const bool found = QueryFullProcessImageNameW(process, 0, path, &length) != FALSE;
    CloseHandle(process);
    try { return found ? std::filesystem::path(path) : std::filesystem::path{}; }
    catch (...) { return {}; }
}

std::filesystem::path last_dlssg_game(const std::filesystem::path& selection_file) noexcept {
    try {
        if ((selection_file == autonomous::configuration_path() || selection_file == autonomous::global_configuration_path() || selection_file.parent_path()==autonomous::common_folder()/L"Profiles")) {
            std::ifstream input(selection_file,std::ios::binary);std::string line;bool selected=false;
            while(std::getline(input,line)) {
                if(!line.empty() && line.back()=='\r')line.pop_back();
                if(line.starts_with('['))selected=line=="[dlssg]";
                if(!selected || !line.starts_with("executable="))continue;
                const auto encoded=line.substr(11);
                const int count=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,encoded.data(),int(encoded.size()),nullptr,0);
                if(count>0) {
                    std::wstring decoded(count,L'\0');
                    MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,encoded.data(),int(encoded.size()),decoded.data(),count);
                    const std::filesystem::path executable(decoded);
                    if(executable.is_absolute() && std::filesystem::is_regular_file(executable) &&
                        _wcsicmp(executable.extension().c_str(),L".exe")==0)return executable;
                }
            }
        }
        wchar_t path[32768]{};
        GetPrivateProfileStringW(L"dlssg", L"executable", L"", path, 32768, selection_file.c_str());
        const std::filesystem::path executable(path);
        if (executable.is_absolute() && std::filesystem::is_regular_file(executable) &&
            _wcsicmp(executable.extension().c_str(), L".exe") == 0) return executable;
    } catch (...) {}
    return {};
}

bool remember_dlssg_game(const std::filesystem::path& selection_file,
                        const std::filesystem::path& executable) noexcept {
    try {
        const auto path = validated_path(executable);
        if (_wcsicmp(last_dlssg_game(selection_file).c_str(), path.c_str()) == 0) return true;
        if ((selection_file == autonomous::configuration_path() || selection_file == autonomous::global_configuration_path() || selection_file.parent_path()==autonomous::common_folder()/L"Profiles")) {
            const int bytes=WideCharToMultiByte(CP_UTF8,0,path.c_str(),int(path.size()),nullptr,0,nullptr,nullptr);
            std::string encoded(bytes,'\0');
            WideCharToMultiByte(CP_UTF8,0,path.c_str(),int(path.size()),encoded.data(),bytes,nullptr,nullptr);
            return autonomous::write_configuration_sections(selection_file,"[dlssg]\nexecutable="+encoded+
                "\npreset="+std::to_string(static_cast<unsigned>(preferred_dlssg_preset(selection_file)))+'\n');
        }
        std::filesystem::create_directories(selection_file.parent_path());
        return WritePrivateProfileStringW(L"dlssg", L"executable", path.c_str(), selection_file.c_str()) != FALSE;
    } catch (...) { return false; }
}
DlssgPreset preferred_dlssg_preset(const std::filesystem::path& configuration) noexcept {
    const auto value=GetPrivateProfileIntW(L"dlssg",L"preset",1,configuration.c_str());
    return value==0 ? DlssgPreset::game_default : value==3 ? DlssgPreset::c : DlssgPreset::a;
}
bool remember_dlssg_preset(const std::filesystem::path& configuration,DlssgPreset preset) noexcept {
    if(preset!=DlssgPreset::game_default && preset!=DlssgPreset::a && preset!=DlssgPreset::c)return false;
    try {
        const auto target=last_dlssg_game(configuration).u8string();
        const std::string encoded(reinterpret_cast<const char*>(target.data()),target.size());
        return autonomous::write_configuration_sections(configuration,"[dlssg]\nexecutable="+encoded+
            "\npreset="+std::to_string(static_cast<unsigned>(preset))+'\n');
    } catch(...) {return false;}
}
bool apply_preferred_dlssg_preset(const std::filesystem::path& configuration,
    const std::filesystem::path& executable,std::wstring& error) noexcept {
    const auto desired=preferred_dlssg_preset(configuration);
    DlssgProfile profile;
    if(!read_dlssg_profile(executable,profile,error))return false;
    if(desired==DlssgPreset::game_default ? !profile.custom_override :
        profile.enabled && profile.custom_override && profile.preset==static_cast<unsigned>(desired))return true;
    DlssgPreset saved{};
    return write_dlssg_profile(executable,desired,error,&saved) && remember_dlssg_preset(configuration,saved);
}
std::filesystem::path process_executable() noexcept {
    wchar_t path[32768]{};
    const auto length = GetModuleFileNameW(nullptr, path, 32768);
    return length && length < 32768 ? std::filesystem::path(path) : std::filesystem::path{};
}

unsigned process_dlssg_preset() noexcept {
    static const unsigned preset = [] {
        DlssgProfile profile; std::wstring error;
        if (!read_dlssg_profile(process_executable(), profile, error) ||
            !profile.enabled || !profile.custom_override) return 0U;
        return profile.preset == 1 || profile.preset == 3 ? profile.preset : 0U;
    }();
    return preset;
}
} // namespace xrfg::standalone
