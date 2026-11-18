#pragma once

#include "battery_device.h"

#include <chrono>
#include <optional>

namespace trayring {

// Infers charging for devices whose protocol has no charging flag: a level that keeps rising can
// only come from a charger. Callers pass settled = false for the first reading after a link-up,
// when receivers may still report a stale value. Jumps larger than kMaxRise are treated as such a
// correction rather than charge.
class ChargeTracker {
public:
    using Clock = std::chrono::steady_clock;

    static constexpr int kMinRise = 2;
    static constexpr int kMaxRise = 10;
    static constexpr auto kHoldTime = std::chrono::minutes{10};

    [[nodiscard]] bool update(const BatteryReading& reading, bool settled, Clock::time_point now);

private:
    std::optional<int> reference_;
    std::optional<Clock::time_point> lastRise_;
};

}  // namespace trayring
