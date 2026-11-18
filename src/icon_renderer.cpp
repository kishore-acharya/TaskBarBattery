#include "icon_renderer.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace trayring::ui {

namespace {

// Geometry is designed on a 64-unit grid and scaled to the icon size; 4 units = 1 px at 16 px.
constexpr float kUnits = 64.0f;
constexpr float kCenter = 32.0f;
constexpr float kRingOuter = 32.0f;
constexpr float kRingWidth = 6.0f;
// The keyboard is wide, so it grows mostly in height to keep clear of the ring's sides.
constexpr float kKeyboardScaleX = 1.03f;
constexpr float kKeyboardScaleY = 1.22f;
constexpr float kMouseScale = 1.3f;
constexpr float kHeadsetScale = 1.12f;
constexpr float kSectionGap = 3.5f;
constexpr float kStroke = 4.0f;
constexpr int kSupersample = 4;

constexpr float kTrackOpacity = 0.38f;
constexpr float kInkOpacity = 0.92f;
constexpr float kDimmedOpacity = 0.45f;
constexpr float kTwoPi = 2.0f * std::numbers::pi_v<float>;

struct Vec {
    float x, y;
};

float sdRoundRect(Vec p, Vec center, Vec half, float radius) {
    const float qx = std::abs(p.x - center.x) - half.x + radius;
    const float qy = std::abs(p.y - center.y) - half.y + radius;
    return std::hypot(std::max(qx, 0.0f), std::max(qy, 0.0f)) + std::min(std::max(qx, qy), 0.0f) - radius;
}

float sdCircle(Vec p, Vec center, float radius) {
    return std::hypot(p.x - center.x, p.y - center.y) - radius;
}

float sdCapsule(Vec p, Vec a, Vec b, float halfWidth) {
    const Vec pa{p.x - a.x, p.y - a.y};
    const Vec ba{b.x - a.x, b.y - a.y};
    const float h = std::clamp((pa.x * ba.x + pa.y * ba.y) / (ba.x * ba.x + ba.y * ba.y), 0.0f, 1.0f);
    return std::hypot(pa.x - ba.x * h, pa.y - ba.y * h) - halfWidth;
}

// A stroke of `width` lying just inside the shape's edge.
bool inStroke(float distance, float width) {
    return distance <= 0.0f && distance > -width;
}

// Angle from 12 o'clock, clockwise, in [0, 2pi).
float clockAngle(Vec p) {
    const float angle = std::atan2(p.x - kCenter, kCenter - p.y);
    return angle < 0.0f ? angle + kTwoPi : angle;
}

// Parallel-sided gaps between sections, centred on each section boundary.
bool inSectionGap(Vec p) {
    const float step = kTwoPi / kRingSections;
    const float boundary = std::round(clockAngle(p) / step) * step;
    const Vec direction{std::sin(boundary), -std::cos(boundary)};
    const Vec q{p.x - kCenter, p.y - kCenter};
    return q.x * direction.x + q.y * direction.y > 0.0f &&
           std::abs(q.x * direction.y - q.y * direction.x) < kSectionGap / 2.0f;
}

bool inRing(Vec p) {
    const float radius = std::hypot(p.x - kCenter, p.y - kCenter);
    return radius <= kRingOuter && radius >= kRingOuter - kRingWidth && !inSectionGap(p);
}

// Glyphs are filled silhouettes with cut-outs: at 16-20 px outlines blur into grey, solid shapes stay crisp.
bool keyboardGlyph(Vec p) {
    if (sdRoundRect(p, {32.0f, 32.0f}, {20.0f, 12.0f}, 3.5f) > 0.0f) {
        return false;
    }
    for (const float x : {22.25f, 28.75f, 35.25f, 41.75f}) {
        if (sdRoundRect(p, {x, 27.5f}, {2.0f, 2.0f}, 0.5f) <= 0.0f) {
            return false;
        }
    }
    return sdRoundRect(p, {32.0f, 35.75f}, {9.75f, 2.0f}, 0.5f) > 0.0f;
}

bool mouseGlyph(Vec p) {
    return sdRoundRect(p, {32.0f, 32.0f}, {11.0f, 17.0f}, 11.0f) <= 0.0f &&
           sdCapsule(p, {32.0f, 21.0f}, {32.0f, 27.0f}, 2.0f) > 0.0f;
}

bool headsetGlyph(Vec p) {
    constexpr Vec bandCenter{32.0f, 34.0f};
    const bool band = p.y <= bandCenter.y && inStroke(sdCircle(p, bandCenter, 15.5f), kStroke + 1.0f);
    const bool leftCup = sdRoundRect(p, {19.5f, 39.5f}, {4.5f, 7.5f}, 2.5f) <= 0.0f;
    const bool rightCup = sdRoundRect(p, {44.5f, 39.5f}, {4.5f, 7.5f}, 2.5f) <= 0.0f;
    return band || leftCup || rightCup;
}

// Enlarges a glyph about the centre so it fills the ring, leaving a small gap to the ring's inner edge.
Vec unscaled(Vec p, float scaleX, float scaleY) {
    return {kCenter + (p.x - kCenter) / scaleX, kCenter + (p.y - kCenter) / scaleY};
}

bool deviceGlyph(DeviceKind kind, Vec p) {
    switch (kind) {
        case DeviceKind::Keyboard: return keyboardGlyph(unscaled(p, kKeyboardScaleX, kKeyboardScaleY));
        case DeviceKind::Mouse: return mouseGlyph(unscaled(p, kMouseScale, kMouseScale));
        case DeviceKind::Headset: return headsetGlyph(unscaled(p, kHeadsetScale, kHeadsetScale));
    }
    return false;
}

// Premultiplied float RGBA, painted with supersampled coverage of boolean shapes.
class Canvas {
public:
    explicit Canvas(int size) : size_(size), pixels_(static_cast<std::size_t>(size) * size) {}

    template <typename Shape>
    void paint(Shape&& inside, Rgb color, float opacity) {
        const float scale = kUnits / static_cast<float>(size_);
        constexpr int samples = kSupersample * kSupersample;
        for (int y = 0; y < size_; ++y) {
            for (int x = 0; x < size_; ++x) {
                int hits = 0;
                for (int sy = 0; sy < kSupersample; ++sy) {
                    for (int sx = 0; sx < kSupersample; ++sx) {
                        const Vec p{(static_cast<float>(x) + (static_cast<float>(sx) + 0.5f) / kSupersample) * scale,
                                    (static_cast<float>(y) + (static_cast<float>(sy) + 0.5f) / kSupersample) * scale};
                        hits += inside(p) ? 1 : 0;
                    }
                }
                if (hits == 0) {
                    continue;
                }
                const float a = opacity * static_cast<float>(hits) / samples;
                auto& px = pixels_[static_cast<std::size_t>(y) * size_ + x];
                px.r = color.r / 255.0f * a + px.r * (1.0f - a);
                px.g = color.g / 255.0f * a + px.g * (1.0f - a);
                px.b = color.b / 255.0f * a + px.b * (1.0f - a);
                px.a = a + px.a * (1.0f - a);
            }
        }
    }

    [[nodiscard]] std::vector<std::uint32_t> toBgra() const {
        std::vector<std::uint32_t> out(pixels_.size(), 0u);
        for (std::size_t i = 0; i < pixels_.size(); ++i) {
            const auto& px = pixels_[i];
            if (px.a <= 0.0f) {
                continue;
            }
            const auto channel = [&](float premultiplied) {
                return static_cast<std::uint32_t>(std::lround(std::clamp(premultiplied / px.a, 0.0f, 1.0f) * 255.0f));
            };
            const auto alpha = static_cast<std::uint32_t>(std::lround(std::clamp(px.a, 0.0f, 1.0f) * 255.0f));
            out[i] = alpha << 24 | channel(px.r) << 16 | channel(px.g) << 8 | channel(px.b);
        }
        return out;
    }

private:
    struct Pixel {
        float r{}, g{}, b{}, a{};
    };

    int size_;
    std::vector<Pixel> pixels_;
};

}  // namespace

Rgb levelColor(int percent) noexcept {
    if (percent <= 10) {
        return kRed;
    }
    if (percent <= 20) {
        return kYellow;
    }
    return kGreen;
}

std::vector<std::uint32_t> renderIcon(const IconSpec& spec, int size) {
    Canvas canvas{size};
    const Rgb ink = spec.lightTaskbar ? Rgb{0, 0, 0} : Rgb{255, 255, 255};
    const float opacity = spec.dimmed ? kDimmedOpacity : 1.0f;

    canvas.paint(inRing, ink, kTrackOpacity * opacity);

    if (spec.level && *spec.level > 0) {
        const float end = kTwoPi * static_cast<float>(std::min(*spec.level, 100)) / 100.0f;
        const Rgb color = spec.charging ? kGreen : levelColor(*spec.level);
        canvas.paint([end](Vec p) { return inRing(p) && clockAngle(p) < end; }, color,
                     std::clamp(spec.arcOpacity, 0.0f, 1.0f) * opacity);
    }

    canvas.paint([kind = spec.kind](Vec p) { return deviceGlyph(kind, p); }, ink, kInkOpacity * opacity);
    return canvas.toBgra();
}

float pulseOpacity(int frame, int frameCount) noexcept {
    if (frameCount <= 0) {
        return 1.0f;
    }
    const float phase = static_cast<float>(frame % frameCount) / static_cast<float>(frameCount);
    const float wave = std::pow(std::max(0.0f, 0.5f + 0.5f * std::cos(kTwoPi * phase)), 0.7f);
    return 0.2f + 0.8f * wave;
}

}  // namespace trayring::ui
