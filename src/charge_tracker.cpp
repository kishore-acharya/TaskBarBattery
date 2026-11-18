#include "charge_tracker.h"

namespace trayring {

bool ChargeTracker::update(const BatteryReading& reading, bool settled, Clock::time_point now) {
    if (reading.state == LinkState::ReceiverNotFound) {
        reference_.reset();
        lastRise_.reset();
        return false;
    }

    if (reading.state == LinkState::Online && settled) {
        const int percent = reading.percent;
        if (!reference_ || percent < *reference_ || percent > *reference_ + kMaxRise) {
            reference_ = percent;
            lastRise_.reset();
        } else if (percent >= *reference_ + kMinRise) {
            reference_ = percent;
            lastRise_ = now;
        }
    }

    return lastRise_ && now - *lastRise_ <= kHoldTime;
}

}  // namespace trayring
