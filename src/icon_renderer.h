#pragma once

#include "battery_device.h"

#include <cstdint>
#include <optional>
#include <vector>

namespace trayring::ui {

struct Rgb {
    std::uint8_t r, g, b;
    friend bool operator==(const Rgb&, const Rgb&) = default;
};

inline constexpr Rgb kGreen{16, 196, 80};
inline constexpr Rgb kYellow{255, 185, 0};
inline constexpr Rgb kRed{232, 17, 35};

inline constexpr int kRingSections = 10;

// Arc colour for a battery level: red at or below 10 %, yellow at or below 20 %, green otherwise.
[[nodiscard]] Rgb levelColor(int percent) noexcept;

struct IconSpec {
    DeviceKind kind{DeviceKind::Keyboard};
    std::optional<int> level;  // filled part of the ring; nullopt shows only the track
    bool charging{false};      // arc drawn green regardless of level
    bool dimmed{false};        // device asleep or unavailable: whole icon translucent
    bool lightTaskbar{false};  // dark ink on a light taskbar, white ink on a dark one
    float arcOpacity{1.0f};    // charging pulse, 0..1

    friend bool operator==(const IconSpec&, const IconSpec&) = default;
};

// A segmented battery ring around a device glyph, filling clockwise from the top.
// Returns size*size BGRA pixels with straight alpha, top-down rows, as a 32-bpp icon DIB expects.
[[nodiscard]] std::vector<std::uint32_t> renderIcon(const IconSpec& spec, int size);

// Arc opacity for frame `frame` of a `frameCount`-frame charging pulse: bright, slowly dimming, back.
[[nodiscard]] float pulseOpacity(int frame, int frameCount) noexcept;

}  // namespace trayring::ui
