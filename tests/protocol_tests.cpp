#include "ronin_protocol.h"
#include "bluetooth_battery.h"
#include "charge_tracker.h"
#include "icon_renderer.h"
#include "razer_protocol.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>

using namespace trayring::ronin;

namespace {

int gFailures = 0;

void check(bool condition, const char* expression, int line) {
    if (!condition) {
        std::fprintf(stderr, "FAIL line %d: %s\n", line, expression);
        ++gFailures;
    }
}

#define CHECK(x) check((x), #x, __LINE__)

Report reportOf(std::initializer_list<std::uint8_t> head) {
    Report r{};
    std::size_t i = 0;
    for (auto b : head) {
        r[i++] = b;
    }
    return r;
}

void statusRequestUsesBit7Checksum() {
    const auto request = buildStatusRequest();
    CHECK(request[0] == 0xF7);
    CHECK(request[7] == 0x08);
    for (std::size_t i = 1; i < request.size(); ++i) {
        if (i != 7) {
            CHECK(request[i] == 0);
        }
    }
}

void checksumCoversOnlyBytesZeroToSix() {
    const auto r = reportOf({0xF6, 0x0A, 0, 0, 0, 0, 0, 0x55, 0x77});
    CHECK(bit7Checksum(r) == 0xFF);
}

// Reply captured on this receiver (3151:4011 rev 0502) with the keyboard linked.
void parsesOnlineReplyFromThisReceiver() {
    const auto parsed = parseStatusReply(reportOf({0x00, 0x05, 0x00, 0x00, 0x01, 0x01, 0x01, 0x01, 0xC8}));
    CHECK(parsed.status.has_value());
    CHECK(parsed.status->keyboardOnline);
    CHECK(parsed.status->keyboardBattery == 5);
    CHECK(parsed.status->hasKeyboard());
    CHECK(parsed.status->readyToRelay);
    CHECK(!parsed.status->relayReplyPending);
}

// Same receiver, keyboard switched off/asleep: byte 3 is 1.
void parsesOfflineReplyFromThisReceiver() {
    const auto parsed = parseStatusReply(reportOf({0x00, 0x05, 0x00, 0x01, 0x01, 0x01, 0x01, 0x01, 0xC8}));
    CHECK(parsed.status.has_value());
    CHECK(!parsed.status->keyboardOnline);
}

// Upstream sharkfin sample from an X86 receiver: 96 % with a relayed reply waiting.
void parsesUpstreamSample() {
    const auto parsed = parseStatusReply(reportOf({0x01, 0x60, 0, 0, 1, 1, 1, 0, 0}));
    CHECK(parsed.status.has_value());
    CHECK(parsed.status->relayReplyPending);
    CHECK(parsed.status->keyboardBattery == 96);
}

void rejectsRepliesThatAreNotStatus() {
    CHECK(parseStatusReply(Report{}).error == ParseError::NotAStatusReply);
    // A relayed identify reply: opcode echoed in byte 0, device id in bytes 1..4.
    CHECK(!parseStatusReply(reportOf({0x8F, 0xAF, 0x07, 0, 0, 0, 0})).status);
    CHECK(!parseStatusReply(reportOf({0, 50, 0, 0, 0, 1, 4})).status);
    const std::array<std::uint8_t, 6> shortReply{};
    CHECK(parseStatusReply(shortReply).error == ParseError::TooShort);
}

void batteryRangeIsValidated() {
    CHECK(isValidBatteryPercent(0));
    CHECK(isValidBatteryPercent(100));
    CHECK(!isValidBatteryPercent(101));
    CHECK(!isValidBatteryPercent(0xFF));
}

void ringColourFollowsThresholds() {
    using namespace trayring::ui;
    CHECK(levelColor(0) == kRed);
    CHECK(levelColor(10) == kRed);
    CHECK(levelColor(11) == kYellow);
    CHECK(levelColor(20) == kYellow);
    CHECK(levelColor(21) == kGreen);
    CHECK(levelColor(100) == kGreen);
}

void pulseStaysVisibleAndStartsBright() {
    using trayring::ui::pulseOpacity;
    CHECK(pulseOpacity(0, 30) > 0.99f);
    for (int frame = 0; frame < 30; ++frame) {
        const float opacity = pulseOpacity(frame, 30);
        CHECK(opacity >= 0.2f && opacity <= 1.0f);
    }
    CHECK(pulseOpacity(15, 30) < 0.25f);
}

trayring::BatteryReading online(int percent) {
    return {trayring::LinkState::Online, percent, false, {}};
}

void chargingIsInferredFromASettledRise() {
    using trayring::ChargeTracker;
    const auto t0 = ChargeTracker::Clock::time_point{};
    ChargeTracker tracker;
    CHECK(!tracker.update(online(50), false, t0));  // first reading after link-up may be stale
    CHECK(!tracker.update(online(50), true, t0 + std::chrono::minutes{1}));
    CHECK(!tracker.update(online(51), true, t0 + std::chrono::minutes{2}));  // below kMinRise
    CHECK(tracker.update(online(52), true, t0 + std::chrono::minutes{3}));
    CHECK(tracker.update(online(52), true, t0 + std::chrono::minutes{13}));   // within hold time
    CHECK(!tracker.update(online(52), true, t0 + std::chrono::minutes{14}));  // hold expired
}

void staleOrFallingLevelsAreNotCharging() {
    using trayring::ChargeTracker;
    const auto t0 = ChargeTracker::Clock::time_point{};
    ChargeTracker tracker;
    CHECK(!tracker.update(online(5), true, t0));
    CHECK(!tracker.update(online(80), true, t0 + std::chrono::minutes{1}));  // receiver correcting a stale value
    CHECK(!tracker.update(online(70), false, t0 + std::chrono::minutes{2})); // unsettled readings are ignored
    CHECK(tracker.update(online(83), true, t0 + std::chrono::minutes{3}));
    CHECK(!tracker.update(online(82), true, t0 + std::chrono::minutes{4}));  // a drop ends charging
}

void receiverLossForgetsHistory() {
    using trayring::ChargeTracker;
    const auto t0 = ChargeTracker::Clock::time_point{};
    ChargeTracker tracker;
    CHECK(!tracker.update(online(40), true, t0));
    CHECK(tracker.update(online(42), true, t0 + std::chrono::minutes{1}));
    CHECK(tracker.update({trayring::LinkState::DeviceOffline, -1, false, {}}, false, t0 + std::chrono::minutes{2}));
    CHECK(!tracker.update({}, false, t0 + std::chrono::minutes{3}));
    CHECK(!tracker.update(online(44), true, t0 + std::chrono::minutes{4}));
}

trayring::razer::Report razerReply(std::initializer_list<std::uint8_t> head) {
    trayring::razer::Report r{};
    std::ranges::copy(head, r.begin());
    r[trayring::razer::offset::kCrc] = trayring::razer::crc(r);
    return r;
}

void razerBatteryRequestMatchesOpenRazerLayout() {
    namespace rz = trayring::razer;
    const auto request = rz::buildBatteryRequest();
    CHECK(request[0] == 0x00);
    CHECK(request[1] == 0x1F);
    CHECK(request[5] == 0x02);
    CHECK(request[6] == 0x07);
    CHECK(request[7] == 0x80);
    CHECK(request[88] == (0x02 ^ 0x07 ^ 0x80));
    CHECK(request[89] == 0x00);
}

void razerParsesReplyCapturedFromDeathAdderV2X() {
    namespace rz = trayring::razer;
    // Synapse showed 27% for this reply (level 0x47 = 71/255).
    const auto parsed = rz::parseBatteryReply(razerReply({0x02, 0x1F, 0, 0, 0, 0x02, 0x07, 0x80, 0x00, 0x47}));
    CHECK(parsed.kind == rz::ReplyKind::Battery);
    CHECK(parsed.percent == 27);
    CHECK(rz::levelToPercent(255) == 100);
    CHECK(rz::levelToPercent(0) == 0);
}

void razerClassifiesOtherReplies() {
    namespace rz = trayring::razer;
    CHECK(rz::parseBatteryReply(razerReply({0x01, 0x1F, 0, 0, 0, 0x02, 0x07, 0x80})).kind == rz::ReplyKind::Busy);
    CHECK(rz::parseBatteryReply(razerReply({0x04, 0x1F, 0, 0, 0, 0x02, 0x07, 0x80})).kind ==
          rz::ReplyKind::DeviceAsleep);
    // Synapse's Chroma frame reply, captured while it was running.
    CHECK(rz::parseBatteryReply(razerReply({0x05, 0x1F, 0, 0, 0, 0x35, 0x0F, 0x03})).kind ==
          rz::ReplyKind::Unrelated);
    auto corrupted = razerReply({0x02, 0x1F, 0, 0, 0, 0x02, 0x07, 0x80, 0x00, 0x47});
    corrupted[rz::offset::kCrc] ^= 0x01;
    CHECK(rz::parseBatteryReply(corrupted).kind == rz::ReplyKind::Failed);
    const std::array<std::uint8_t, 10> shortReply{};
    CHECK(rz::parseBatteryReply(shortReply).kind == rz::ReplyKind::Failed);
}

void headsetLevelCountsOnlyWhileConnected() {
    using trayring::LinkState;
    using trayring::bluetooth::interpret;
    const auto online = interpret({true, true, 20});
    CHECK(online.state == LinkState::Online);
    CHECK(online.percent == 20);
    // Windows keeps the last level of disconnected headsets (seen: 1 % and 60 % on devices that were off).
    CHECK(interpret({true, false, 60}).state == LinkState::DeviceOffline);
    CHECK(interpret({false, false, std::nullopt}).state == LinkState::ReceiverNotFound);
    CHECK(interpret({true, true, std::nullopt}).state == LinkState::Error);
    CHECK(interpret({true, true, 101}).state == LinkState::Error);
}

}  // namespace

int main() {
    statusRequestUsesBit7Checksum();
    checksumCoversOnlyBytesZeroToSix();
    parsesOnlineReplyFromThisReceiver();
    parsesOfflineReplyFromThisReceiver();
    parsesUpstreamSample();
    rejectsRepliesThatAreNotStatus();
    batteryRangeIsValidated();
    ringColourFollowsThresholds();
    pulseStaysVisibleAndStartsBright();
    chargingIsInferredFromASettledRise();
    staleOrFallingLevelsAreNotCharging();
    receiverLossForgetsHistory();
    razerBatteryRequestMatchesOpenRazerLayout();
    razerParsesReplyCapturedFromDeathAdderV2X();
    razerClassifiesOtherReplies();
    headsetLevelCountsOnlyWhileConnected();

    if (gFailures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", gFailures);
        return EXIT_FAILURE;
    }
    std::puts("All tests passed");
    return EXIT_SUCCESS;
}
