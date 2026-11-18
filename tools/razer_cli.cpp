// Console test utility for the Razer DeathAdder V2 X HyperSpeed: lists the dongle's HID
// collections, sends the read-only battery query (class 0x07, id 0x80) and prints the reply.
//
// Exit codes: 0 battery read, 2 mouse offline, 3 dongle not found, 4 error.

#include "hid_device.h"
#include "logger.h"
#include "razer_mouse.h"
#include "razer_protocol.h"

#include <windows.h>

#include <cstdio>
#include <cwchar>
#include <string>

namespace {

void printInterfaces() {
    const auto interfaces = trayring::hid::enumerate(trayring::razer::kVendorId, trayring::razer::kDeathAdderV2XHyperSpeed);
    std::wprintf(L"HID collections for %04X:%04X: %zu\n", trayring::razer::kVendorId,
                 trayring::razer::kDeathAdderV2XHyperSpeed, interfaces.size());
    for (const auto& i : interfaces) {
        std::wprintf(L"  %ls\n    rev=%04X usagePage=%04X usage=%04X in=%u out=%u feature=%u\n", i.path.c_str(),
                     i.versionNumber, i.usagePage, i.usage, i.inputReportLength, i.outputReportLength,
                     i.featureReportLength);
    }
}

void printReading(const trayring::RazerReading& r) {
    if (!r.interfacePath.empty()) {
        std::wprintf(L"Interface: %ls\n", r.interfacePath.c_str());
    }
    if (r.rawReply) {
        std::wprintf(L"Raw reply [0..15]:");
        for (std::size_t i = 0; i < 16; ++i) {
            std::wprintf(L" %02X", (*r.rawReply)[i]);
        }
        std::wprintf(L"  crc[88]=%02X\n", (*r.rawReply)[trayring::razer::offset::kCrc]);
        std::wprintf(L"  status=%02X level=%u/255\n", (*r.rawReply)[0],
                     (*r.rawReply)[trayring::razer::offset::kArguments + 1]);
    }
    std::wprintf(L"Result: %ls\n", trayring::describe(r).c_str());
}

int exitCode(trayring::LinkState state) {
    switch (state) {
        case trayring::LinkState::Online: return 0;
        case trayring::LinkState::DeviceOffline: return 2;
        case trayring::LinkState::ReceiverNotFound: return 3;
        case trayring::LinkState::Error: return 4;
    }
    return 4;
}

void usage() {
    std::wprintf(L"Usage: razer-battery-cli [--list] [--watch <seconds>] [--log <file>]\n"
                 L"  --list           only list HID collections, send nothing\n"
                 L"  --watch <s>      repeat the battery query every <s> seconds (Ctrl+C to stop)\n"
                 L"  --log <file>     append diagnostics to <file>\n");
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    bool listOnly = false;
    unsigned watchSeconds = 0;
    for (int i = 1; i < argc; ++i) {
        const std::wstring arg = argv[i];
        if (arg == L"--list") {
            listOnly = true;
        } else if (arg == L"--watch" && i + 1 < argc) {
            watchSeconds = static_cast<unsigned>(std::wcstoul(argv[++i], nullptr, 10));
        } else if (arg == L"--log" && i + 1 < argc) {
            trayring::log::enable(argv[++i]);
        } else {
            usage();
            return arg == L"--help" || arg == L"-h" ? 0 : 1;
        }
    }

    printInterfaces();
    if (listOnly) {
        return 0;
    }

    trayring::RazerDeathAdderV2X mouse;
    for (;;) {
        SYSTEMTIME t{};
        GetLocalTime(&t);
        std::wprintf(L"\n%02u:%02u:%02u battery query 07/80\n", t.wHour, t.wMinute, t.wSecond);
        const auto reading = mouse.readDetailed();
        printReading(reading);
        std::fflush(stdout);
        if (watchSeconds == 0) {
            return exitCode(reading.state);
        }
        Sleep(watchSeconds * 1000);
    }
}
