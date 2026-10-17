#include "xrfg/bridge_flight_logger.hpp"

#include <windows.h>

#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace fs = std::filesystem;

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void write(const fs::path& path, const std::string& contents) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream << contents;
    require(static_cast<bool>(stream), "fixture write failed");
}

std::string read(const fs::path& path) {
    std::ifstream stream(path, std::ios::binary);
    return {(std::istreambuf_iterator<char>(stream)), {}};
}

struct TempDirectory {
    fs::path path;

    TempDirectory() {
        std::array<wchar_t, MAX_PATH> root{};
        GetTempPathW(static_cast<DWORD>(root.size()), root.data());
        path = fs::path(root.data()) /
            (L"xrfg-flight-tests-" + std::to_wstring(GetCurrentProcessId()));
        fs::remove_all(path);
        fs::create_directories(path);
    }

    ~TempDirectory() {
        std::error_code error;
        fs::remove_all(path, error);
    }
};

} // namespace

int main(int argc, char** argv) {
    if (argc == 2) {
        xrfg::BridgeFlightLogger child;
        child.initialize(argv[1]);
        child.event(xrfg::BridgeFlightOperation::application_wait_frame, 6, 11, 22, 33);
        TerminateProcess(GetCurrentProcess(), 73);
        return 1;
    }
    try {
        TempDirectory temporary;

        xrfg::BridgeFlightLogger disabled;
        disabled.initialize(temporary.path / "disabled");
        require(!disabled.enabled(), "missing INI must keep logging disabled");
        require(disabled.begin(
                    xrfg::BridgeFlightOperation::application_wait_frame)
                    .sequence == 0,
            "disabled logger produced a token");

        const fs::path enabled_directory = temporary.path / "enabled";
        fs::create_directories(enabled_directory);
        write(
            enabled_directory / "ofxr_bridge.ini",
            "[diagnostics]\r\n"
            "logging_enabled=1\r\n"
            "max_file_mb=1\r\n"
            "flush_each_event=0\r\n");

        wchar_t exe[32768]{};
        GetModuleFileNameW(nullptr, exe, 32768);
        std::wstring command = L"\"" + std::wstring(exe) + L"\" \"" + enabled_directory.wstring() + L"\"";
        STARTUPINFOW startup{sizeof(startup)};
        PROCESS_INFORMATION process{};
        require(CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, 0,
            nullptr, nullptr, &startup, &process) != FALSE, "child launch failed");
        const DWORD wait = WaitForSingleObject(process.hProcess, 10000);
        if (wait != WAIT_OBJECT_0) TerminateProcess(process.hProcess, 74);
        DWORD code{};
        GetExitCodeProcess(process.hProcess, &code);
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        require(wait == WAIT_OBJECT_0 && code == 73, "child did not terminate as expected");
        bool persisted = false;
        for (const auto& item : fs::directory_iterator(enabled_directory)) {
            if (item.path().extension() == ".log") {
                persisted |= read(item.path()).find(
                    "op=app_wait_frame result=6 dur_us=0 a=11 b=22 c=33") != std::string::npos;
            }
        }
        require(persisted, "last recorder event lost after forced termination");

        xrfg::BridgeFlightLogger enabled;
        enabled.initialize(enabled_directory);
        require(enabled.enabled(), "enabled INI did not start bridge logging");
        const fs::path log_path = enabled.log_path();
        require(!log_path.empty() && log_path.parent_path() == enabled_directory,
            "bridge log was not created beside the DLL configuration");

        const auto token = enabled.begin(
            xrfg::BridgeFlightOperation::application_wait_frame,
            11,
            22,
            33);
        require(token.sequence != 0, "enabled logger did not produce a token");
        enabled.end(
            token,
            xrfg::BridgeFlightOperation::application_wait_frame,
            7,
            44,
            55,
            66);
        enabled.event(
            xrfg::BridgeFlightOperation::synthesis_pair,
            9,
            77,
            88,
            99);
        enabled.event(
            xrfg::BridgeFlightOperation::runtime_identity,
            1,
            2,
            3,
            4);
        enabled.event(
            xrfg::BridgeFlightOperation::presenter_transition,
            3);
        enabled.event(
            xrfg::BridgeFlightOperation::presenter_submission,
            0);
        enabled.event(
            xrfg::BridgeFlightOperation::nvidia_gpu_stages,
            2,
            101,
            202,
            303);
        enabled.event(
            xrfg::BridgeFlightOperation::nvidia_gpu_total,
            0,
            404,
            1010,
            77);
        enabled.shutdown();

        const std::string log = read(log_path);
        require(log.find("phase=B op=app_wait_frame") != std::string::npos,
            "bridge log is missing the wait begin boundary");
        require(log.find("phase=E op=app_wait_frame result=7") !=
                std::string::npos,
            "bridge log is missing the matching wait completion");
        require(log.find("phase=I op=synthesis_pair result=9") !=
                std::string::npos,
            "bridge log is missing a synthesis event");
        require(log.find("a=77 b=88 c=99") != std::string::npos,
            "bridge log lost diagnostic event fields");
        require(log.find("op=runtime_identity result=1") != std::string::npos &&
                log.find("op=presenter_transition result=3") !=
                    std::string::npos &&
                log.find("op=presenter_submission result=0") !=
                    std::string::npos,
            "bridge log is missing V043 presenter event names");
        require(
            log.find(
                "op=nvidia_gpu_stages result=2 dur_us=0 a=101 b=202 c=303") !=
                    std::string::npos &&
                log.find(
                    "op=nvidia_gpu_total result=0 dur_us=0 a=404 b=1010 c=77") !=
                    std::string::npos,
            "bridge log is missing V049 NVIDIA GPU timing fields");

        // Past the cap the full file is kept as <name>.1 and recording goes on
        // in a fresh file under the same name, which starts with the session
        // records: a wrap must not take what was recorded just before it.
        const fs::path rotation_directory = temporary.path / "rotation";
        fs::create_directories(rotation_directory);
        write(
            rotation_directory / "ofxr_bridge.ini",
            "[diagnostics]\r\n"
            "logging_enabled=1\r\n"
            "max_file_mb=1\r\n"
            "flush_each_event=0\r\n");
        xrfg::BridgeFlightLogger rotating;
        rotating.initialize(rotation_directory);
        require(rotating.enabled(), "rotation logger did not start");
        const fs::path rotating_path = rotating.log_path();
        rotating.event(xrfg::BridgeFlightOperation::runtime_identity, 5, 6, 7, 8);
        // About 100 bytes a record: 15,000 is past one megabyte once, not twice.
        for (std::uint64_t index = 0; index < 15000; ++index) {
            rotating.event(
                xrfg::BridgeFlightOperation::synthesis_pair, 0, index, 1, 2);
        }
        rotating.event(
            xrfg::BridgeFlightOperation::synthesis_pair, 0, 424242, 1, 2);
        rotating.shutdown();
        fs::path previous_path = rotating_path;
        previous_path += ".1";
        require(fs::exists(previous_path), "a full log was not kept as .1");
        const std::string previous = read(previous_path);
        const std::string current = read(rotating_path);
        require(previous.size() <= 1024U * 1024U && current.size() <= 1024U * 1024U,
            "a rotated log is over its cap");
        require(previous.find("op=synthesis_pair result=0 dur_us=0 a=0 b=1 c=2") !=
                std::string::npos,
            "the kept log lost the records from before the cap");
        require(current.find("op=runtime_identity result=5") != std::string::npos,
            "the fresh log does not start with the session records");
        require(current.find("a=424242 b=1 c=2") != std::string::npos,
            "the fresh log lost the records after the cap");

        std::cout << "Bridge flight logger tests passed.\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "Test failure: " << exception.what() << '\n';
        return 1;
    }
}
