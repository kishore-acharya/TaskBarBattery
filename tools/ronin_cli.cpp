// Console test utility: lists the receiver's HID collections, performs the 0xF7 status
// exchange and prints the raw reply and decoded fields.
//
// Exit codes: 0 battery read, 2 keyboard offline, 3 receiver not found, 4 error.

#include "ronin_protocol.h"
#include "ronin_keyboard.h"
#include "hid_device.h"
#include "logger.h"

#include <windows.h>

#include <cstdio>
#include <cwchar>
#include <string>

namespace {

void printInterfaces() {
    const auto interfaces = trayring::hid::enumerate(trayring::ronin::kVendorId, trayring::ronin::kReceiverProductId);
    std::wprintf(L"HID collections for %04X:%04X: %zu\n", trayring::ronin::kVendorId,
                 trayring::ronin::kReceiverProductId, interfaces.size());
    for (const auto& i : interfaces) {
        std::wprintf(L"  %ls\n    rev=%04X usagePage=%04X usage=%04X in=%u out=%u feature=%u%ls\n", i.path.c_str(),
                     i.versionNumber, i.usagePage, i.usage, i.inputReportLength, i.outputReportLength,
                     i.featureReportLength, trayring::isStatusInterface(i) ? L"  <- status interface" : L"");
    }
}

void printReading(const trayring::ReceiverReading& r) {
    if (r.rawReply) {
        std::wprintf(L"Raw reply [0..15]:");
        for (std::size_t i = 0; i < 16; ++i) {
            std::wprintf(L" %02X", (*r.rawReply)[i]);
        }
        std::wprintf(L"\n");
    }
    if (r.status) {
        const auto& s = *r.status;
        std::wprintf(L"  [0] relay reply pending : %ls\n", s.relayReplyPending ? L"yes" : L"no");
        std::wprintf(L"  [1] keyboard battery    : %u\n", s.keyboardBattery);
        std::wprintf(L"  [3] keyboard online     : %ls\n", s.keyboardOnline ? L"yes" : L"no");
        std::wprintf(L"  [5] ready to relay      : %ls\n", s.readyToRelay ? L"yes" : L"no");
        std::wprintf(L"  [6] paired device kind  : %u\n", static_cast<unsigned>(s.kind));
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
    std::wprintf(L"Usage: ronin-battery-cli [--list] [--watch <seconds>] [--log <file>]\n"
                 L"  --list           only list HID collections, send nothing\n"
                 L"  --watch <s>      repeat the status query every <s> seconds (Ctrl+C to stop)\n"
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

    trayring::BatteryMonitor monitor;
    for (;;) {
        SYSTEMTIME t{};
        GetLocalTime(&t);
        std::wprintf(L"\n%02u:%02u:%02u status query 0xF7\n", t.wHour, t.wMinute, t.wSecond);
        const auto reading = monitor.read();
        printReading(reading);
        std::fflush(stdout);
        if (watchSeconds == 0) {
            return exitCode(reading.state);
        }
        Sleep(watchSeconds * 1000);
    }
}
