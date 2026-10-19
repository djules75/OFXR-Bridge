// ini_file (V518) against the profile API it replaces in the layer: the same
// files, the same questions, the same answers. The layer stopped calling
// GetPrivateProfile* because a game mod can hook it (Skyrim's
// PrivateProfileRedirector caches every ini), so the answers have to stay
// what the tray and the layer always relied on.

#include "xrfg/ini_file.hpp"

#include <windows.h>

#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {

int failures = 0;

void expect(bool condition, std::string_view description) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << description << '\n';
    }
}

std::string narrow(std::wstring_view text) {
    std::string out;
    for (wchar_t c : text) out += c < 128 ? static_cast<char>(c) : '?';
    return out;
}

struct Question {
    const wchar_t* section;
    const wchar_t* key;
};

constexpr std::array kQuestions{
    Question{L"ofxr", L"hybrid"},
    Question{L"ofxr", L"backend"},
    Question{L"OFXR", L"Backend"},
    Question{L"ofxr", L"quoted"},
    Question{L"ofxr", L"single_quoted"},
    Question{L"ofxr", L"empty"},
    Question{L"ofxr", L"comment"},
    Question{L"ofxr", L"late"},
    Question{L"ofxr", L"missing"},
    Question{L"ofxr", L"spaced value"},
    Question{L"ofxr", L"number_text"},
    Question{L"ofxr", L"negative"},
    Question{L"ofxr", L"hex"},
    Question{L"ofxr", L"padded"},
    Question{L"ofxr", L"word"},
    Question{L"ofxr", L"excluded_processes"},
    Question{L"ofxr", L"control_event"},
    Question{L"ofxr", L"nvidia_input_scale"},
    Question{L"overlay", L"position"},
    Question{L"overlay", L"hybrid"},
    Question{L"diagnostics", L"logging_enabled"},
    Question{L"diagnostics", L"max_file_mb"},
    Question{L"absent", L"hybrid"},
};

void compare(const std::filesystem::path& path, std::string_view name) {
    for (const auto& q : kQuestions) {
        const std::string label = std::string(name) + " [" + narrow(q.section) + "] " + narrow(q.key);
        std::array<wchar_t, 512> buffer{};
        const DWORD length = GetPrivateProfileStringW(q.section, q.key, L"<default>", buffer.data(),
            static_cast<DWORD>(buffer.size()), path.c_str());
        const std::wstring profile(buffer.data(), length);
        const std::wstring ours = xrfg::ini_file::read_string(path, q.section, q.key, L"<default>");
        expect(ours == profile,
            label + ": string '" + narrow(ours) + "' vs profile API '" + narrow(profile) + "'");
        const UINT profile_int = GetPrivateProfileIntW(q.section, q.key, 77, path.c_str());
        const UINT our_int = xrfg::ini_file::read_int(path, q.section, q.key, 77);
        expect(our_int == profile_int, label + ": int " + std::to_string(our_int) +
            " vs profile API " + std::to_string(profile_int));
    }
}

constexpr std::string_view kSample =
    "[ofxr]\r\n"
    "control_event=Local\\OFXRBridgeArmStop-8630094853005295609\r\n"
    "hybrid=1\r\n"
    "  Backend =  NVIDIA  \r\n"
    "quoted=\"two words\"\r\n"
    "single_quoted='x'\r\n"
    "empty=\r\n"
    ";comment=1\r\n"
    "spaced value = 3\r\n"
    "number_text=12abc\r\n"
    "negative=-5\r\n"
    "hex=0x1F\r\n"
    "padded=   42   \r\n"
    "word=abc\r\n"
    "hybrid=0\r\n"
    "excluded_processes=PimaxHome-Win64-Shipping.exe; other.exe\r\n"
    "nvidia_input_scale=75\r\n"
    "\r\n"
    "[Overlay]\r\n"
    "position=lower_left\r\n"
    "hybrid=0\r\n"
    "[diagnostics]\n"
    "logging_enabled=1\n"
    "max_file_mb=256\n";

void write_bytes(const std::filesystem::path& path, std::string_view bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

}  // namespace

int main(int argc, char** argv) {
    wchar_t temp[MAX_PATH]{};
    GetTempPathW(MAX_PATH, temp);
    const auto directory = std::filesystem::path(temp) /
        (L"xrfg_ini_file_tests_" + std::to_wstring(GetCurrentProcessId()));
    std::filesystem::create_directories(directory);

    const auto ansi = directory / L"ansi.ini";
    write_bytes(ansi, kSample);
    compare(ansi, "ansi");

    // UTF-16LE with a byte-order mark: the profile API's Unicode file.
    const auto wide = directory / L"utf16.ini";
    {
        std::wstring text;
        for (char c : kSample) text += static_cast<wchar_t>(static_cast<unsigned char>(c));
        std::string bytes = "\xFF\xFE";
        bytes.append(reinterpret_cast<const char*>(text.data()), text.size() * sizeof(wchar_t));
        write_bytes(wide, bytes);
    }
    compare(wide, "utf16");

    // The repository's test configurations, as the layer tests ship them.
    if (argc > 1) {
        for (const auto& entry : std::filesystem::directory_iterator(argv[1])) {
            if (entry.path().extension() == L".ini") {
                compare(entry.path(), entry.path().filename().string());
            }
        }
    }

    // No file at all: every default.
    expect(xrfg::ini_file::read_int(directory / L"none.ini", L"ofxr", L"hybrid", 5) == 5,
        "missing file gives the default int");
    expect(xrfg::ini_file::read_string(directory / L"none.ini", L"ofxr", L"backend", L"x") == L"x",
        "missing file gives the default string");
    expect(!xrfg::ini_file::read_value(ansi, L"ofxr", L"missing"), "absent key has no value");
    expect(xrfg::ini_file::read_value(ansi, L"ofxr", L"empty") == std::wstring{},
        "empty key has an empty value, not none");

    // Every call reads the file: a rewrite is seen at once.
    write_bytes(ansi, "[ofxr]\r\nhybrid=0\r\n");
    expect(xrfg::ini_file::read_int(ansi, L"ofxr", L"hybrid", 9) == 0, "rewrite seen (off)");
    write_bytes(ansi, "[ofxr]\r\nhybrid=1\r\n");
    expect(xrfg::ini_file::read_int(ansi, L"ofxr", L"hybrid", 9) == 1, "rewrite seen (on)");

    std::error_code ignored;
    std::filesystem::remove_all(directory, ignored);
    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "ini_file tests passed\n";
    return EXIT_SUCCESS;
}
