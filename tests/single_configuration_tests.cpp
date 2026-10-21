#include "xrfg/autonomous_control.hpp"
#include "xrfg/dlssg_profile.hpp"
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

int main() {
    using namespace xrfg::autonomous;
    const auto root=std::filesystem::temp_directory_path()/(L"ofxr-single-config-"+std::to_wstring(GetCurrentProcessId()));
    if(configuration_path().parent_path().filename()!=L"ofxr-bridge") {
        std::cerr<<"Stable shared data root must be ofxr-bridge\n";return 1;
    }
    SetEnvironmentVariableW(L"XRFG_AUTONOMOUS_PROFILE_DIR",root.c_str());
    const auto check=[](bool ok,const char* why){if(!ok)throw std::runtime_error(why);};
    try {
        std::filesystem::create_directories(root);
        std::ofstream(root/L"autonomous.ini")<<"[autonomous]\nbackend=1\ndiagnostics=1\nnr_intensity=0.7\n";
        std::ofstream(root/L"tray.ini")<<"[tray]\npause_hotkey=ctrl+alt+f7\nsteamvr_separate_queue=0\n";
        check(initialize_user_configuration(),"fresh configuration creation failed");
        check(configuration_path().filename()==L"ofxr-bridge.ini","single public INI name");
        auto state=snapshot();
        check(state.desired==sanitize(Settings{}),"fresh defaults must ignore old autonomous/tray files entirely");
        check(!benchmark_enabled(),"benchmarks default off");
        check(write_configuration_sections(configuration_path(),"[benchmark]\nenabled=1\n"),"enable benchmarks in existing INI");
        auto changed=state.desired;changed.overlay_position=0;
        check(request(changed) && benchmark_enabled(),"UI saves must preserve manually enabled benchmarks");
        wchar_t hotkey[128]{};GetPrivateProfileStringW(L"tray",L"pause_hotkey",L"",hotkey,128,configuration_path().c_str());
        check(std::wstring(hotkey).empty(),"old tray preferences must not be imported");
        check(write_configuration_sections(configuration_path(),"[tray]\npause_hotkey=ctrl+alt+f7\n"),"new tray preferences");
        check(request(state.desired),"save runtime preferences alongside new tray preferences");
        GetPrivateProfileStringW(L"tray",L"pause_hotkey",L"",hotkey,128,configuration_path().c_str());
        check(std::wstring(hotkey)==L"ctrl+alt+f7","new settings sections must coexist");
        check(xrfg::standalone::remember_dlssg_game(configuration_path(),xrfg::standalone::process_executable()),"remember game in shared INI");
        using namespace xrfg::standalone;
        check(preferred_dlssg_preset(configuration_path())==DlssgPreset::a,"A is the initial preference without importing old settings");
        check(remember_dlssg_preset(configuration_path(),DlssgPreset::c),"explicit C choice remains available");
        check(request(state.desired) && xrfg::standalone::last_dlssg_game(configuration_path())==xrfg::standalone::process_executable(),
            "later UI save must preserve offline preset target");
        check(preferred_dlssg_preset(configuration_path())==DlssgPreset::c,"renderer save preserves explicit C");
        check(remember_dlssg_game(configuration_path(),process_executable()) && preferred_dlssg_preset(configuration_path())==DlssgPreset::c,"game discovery does not reset explicit preference");
        check(remember_dlssg_preset(configuration_path(),DlssgPreset::game_default) && preferred_dlssg_preset(configuration_path())==DlssgPreset::game_default,"explicit game default can opt out");
        check(remember_dlssg_preset(configuration_path(),DlssgPreset::a) && last_dlssg_game(configuration_path())==process_executable(),"select A without losing the remembered game");
        check(write_configuration_sections(configuration_path(),"[tray]\npause_hotkey=off\nsteamvr_separate_queue=1\n"),"tray save");
        check(parse_settings([&]{std::ifstream in(configuration_path());return std::string(std::istreambuf_iterator<char>(in),{});}()).backend==Settings{}.backend && benchmark_enabled(),
            "tray save must preserve rendering choices and benchmark flag");
        check(std::filesystem::exists(root/L"autonomous.ini") && std::filesystem::exists(root/L"tray.ini"),"legacy rollback inputs are retained");
        for(const auto* name:{L"ofxr-bridge.ini",L"autonomous.ini",L"tray.ini"})std::filesystem::remove(root/name);
        std::filesystem::remove(root);
        std::cout<<"Fresh single INI, no legacy import, shared sections, offline target and benchmark flag PASS\n";
        return 0;
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
