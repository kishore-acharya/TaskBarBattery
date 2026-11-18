// Developer tool: renders every icon state at tray sizes into a PNG contact sheet.
// Usage: icon-preview <out.png>

#include "icon_renderer.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

using namespace trayring;
using namespace trayring::ui;

namespace {

constexpr int kZoom = 3;
constexpr std::array kSizes{16, 20, 24, 32};

struct Image {
    int width, height;
    std::vector<std::uint32_t> argb;

    // Alpha-blends a straight-alpha BGRA icon, magnified by `zoom` with nearest-neighbour.
    void blit(const std::vector<std::uint32_t>& icon, int size, int x0, int y0, int zoom) {
        for (int y = 0; y < size * zoom; ++y) {
            for (int x = 0; x < size * zoom; ++x) {
                const auto src = icon[static_cast<std::size_t>(y / zoom) * size + x / zoom];
                auto& dst = argb[static_cast<std::size_t>(y0 + y) * width + x0 + x];
                const unsigned a = src >> 24;
                std::uint32_t out = 0xFF000000u;
                for (int shift = 0; shift <= 16; shift += 8) {
                    const unsigned s = (src >> shift) & 0xFF;
                    const unsigned d = (dst >> shift) & 0xFF;
                    out |= ((s * a + d * (255 - a) + 127) / 255) << shift;
                }
                dst = out;
            }
        }
    }
};

std::uint32_t crc32(const std::uint8_t* data, std::size_t length, std::uint32_t crc = 0) {
    crc = ~crc;
    for (std::size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (int k = 0; k < 8; ++k) {
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
    }
    return ~crc;
}

void put32(std::vector<std::uint8_t>& out, std::uint32_t v) {
    for (int shift = 24; shift >= 0; shift -= 8) {
        out.push_back(static_cast<std::uint8_t>(v >> shift));
    }
}

void chunk(std::ofstream& file, const char* type, const std::vector<std::uint8_t>& data) {
    std::vector<std::uint8_t> body(type, type + 4);
    body.insert(body.end(), data.begin(), data.end());
    std::vector<std::uint8_t> head;
    put32(head, static_cast<std::uint32_t>(data.size()));
    std::vector<std::uint8_t> tail;
    put32(tail, crc32(body.data(), body.size()));
    file.write(reinterpret_cast<const char*>(head.data()), 4);
    file.write(reinterpret_cast<const char*>(body.data()), static_cast<std::streamsize>(body.size()));
    file.write(reinterpret_cast<const char*>(tail.data()), 4);
}

// Uncompressed (stored-block) PNG: no zlib dependency for a developer tool.
bool writePng(const std::string& path, const Image& image) {
    std::vector<std::uint8_t> raw;
    for (int y = 0; y < image.height; ++y) {
        raw.push_back(0);
        for (int x = 0; x < image.width; ++x) {
            const auto p = image.argb[static_cast<std::size_t>(y) * image.width + x];
            raw.push_back(static_cast<std::uint8_t>(p >> 16));
            raw.push_back(static_cast<std::uint8_t>(p >> 8));
            raw.push_back(static_cast<std::uint8_t>(p));
        }
    }
    std::vector<std::uint8_t> z{0x78, 0x01};
    for (std::size_t offset = 0; offset < raw.size();) {
        const auto n = static_cast<std::uint16_t>(std::min<std::size_t>(65535, raw.size() - offset));
        z.push_back(offset + n == raw.size() ? 1 : 0);
        z.push_back(static_cast<std::uint8_t>(n));
        z.push_back(static_cast<std::uint8_t>(n >> 8));
        z.push_back(static_cast<std::uint8_t>(~n));
        z.push_back(static_cast<std::uint8_t>(~n >> 8));
        z.insert(z.end(), raw.begin() + static_cast<std::ptrdiff_t>(offset),
                 raw.begin() + static_cast<std::ptrdiff_t>(offset + n));
        offset += n;
    }
    std::uint32_t a = 1, b = 0;
    for (const auto byte : raw) {
        a = (a + byte) % 65521;
        b = (b + a) % 65521;
    }
    put32(z, b << 16 | a);

    std::ofstream file(path, std::ios::binary);
    const std::uint8_t signature[] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    file.write(reinterpret_cast<const char*>(signature), sizeof(signature));
    std::vector<std::uint8_t> header;
    put32(header, static_cast<std::uint32_t>(image.width));
    put32(header, static_cast<std::uint32_t>(image.height));
    header.insert(header.end(), {8, 2, 0, 0, 0});
    chunk(file, "IHDR", header);
    chunk(file, "IDAT", z);
    chunk(file, "IEND", {});
    return static_cast<bool>(file);
}

struct State {
    std::optional<int> level;
    bool charging;
    bool dimmed;
    float arcOpacity;
};

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "Usage: icon-preview <out.png> [light]\n");
        return 1;
    }
    const bool light = argc > 2 && std::string{argv[2]} == "light";

    const std::array states{
        State{100, false, false, 1.0f}, State{75, false, false, 1.0f}, State{50, false, false, 1.0f},
        State{20, false, false, 1.0f},  State{10, false, false, 1.0f}, State{60, true, false, 1.0f},
        State{60, true, false, 0.2f},   State{80, false, true, 1.0f},  State{std::nullopt, false, true, 1.0f},
    };
    const std::array kinds{DeviceKind::Keyboard, DeviceKind::Mouse, DeviceKind::Headset};

    int cellWidth = 8;
    for (const int size : kSizes) {
        cellWidth += size * kZoom + 8;
    }
    const int rowHeight = kSizes.back() * (kZoom + 1) + 16;
    Image image{cellWidth * static_cast<int>(states.size()) + 8, rowHeight * static_cast<int>(kinds.size()) + 8, {}};
    image.argb.assign(static_cast<std::size_t>(image.width) * image.height, light ? 0xFFEEEEEEu : 0xFF1C1C1Cu);

    for (std::size_t k = 0; k < kinds.size(); ++k) {
        for (std::size_t s = 0; s < states.size(); ++s) {
            const auto& state = states[s];
            IconSpec spec{kinds[k], state.level, state.charging, state.dimmed, light, state.arcOpacity};
            int x = 4 + static_cast<int>(s) * cellWidth + 8;
            const int y = 4 + static_cast<int>(k) * rowHeight + 8;
            for (const int size : kSizes) {
                const auto icon = renderIcon(spec, size);
                image.blit(icon, size, x, y, kZoom);
                image.blit(icon, size, x, y + size * kZoom + 2, 1);
                x += size * kZoom + 8;
            }
        }
    }
    return writePng(argv[1], image) ? 0 : 1;
}
