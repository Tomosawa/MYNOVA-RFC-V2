#include "OokFrameDecoder.h"
#include <algorithm>
#include <cstring>

namespace {
uint32_t difference(uint32_t a, uint32_t b) { return a >= b ? a - b : b - a; }

bool near(uint16_t measured, uint16_t nominal) {
    return difference(measured, nominal) <= std::max(60UL, nominal * 35UL / 100);
}

bool plausibleTiming(uint16_t shortUs, uint16_t longUs) {
    return shortUs && 2UL * longUs >= 3UL * shortUs && longUs <= 8UL * shortUs;
}
}

void OokFrameDecoder::reset() {
    resetStream();
    repeats = 0;
}

uint8_t OokFrameDecoder::feed(const uint16_t* widths, const bool* levels, uint16_t count,
                              bool continuous, bool final, Frame& output) {
    if (!continuous) resetStream();
    uint8_t found = 0;
    for (uint16_t i = 0; i < count; ++i) {
        if (!widths[i] || widths[i] > MaxWidth) {
            inspect(true, found, output);
            resetStream();
            continue;
        }
        if (used && level[used - 1] == levels[i]) {
            if (!combine(used - 1, uint32_t(width[used - 1]) + widths[i], found, output)) continue;
        } else {
            if (used == Capacity) {
                inspect(false, found, output);
                keepTail();
                if (used == Capacity) dropPrefix(1);
            }
            width[used] = widths[i];
            level[used++] = levels[i];
        }
        while (used >= 3 && width[used - 2] < 120 && level[used - 3] == level[used - 1]) {
            const uint16_t first = used - 3;
            if (!combine(first, uint32_t(width[first]) + width[first + 1] + width[first + 2],
                         found, output)) break;
            used -= 2;
        }
    }
    inspect(final, found, output);
    if (final) resetStream();
    else keepTail();
    return found;
}

bool OokFrameDecoder::combine(uint16_t index, uint32_t sum, uint8_t& found, Frame& output) {
    if (sum <= MaxWidth) {
        width[index] = sum;
        return true;
    }
    used = index;
    inspect(true, found, output);
    resetStream();
    return false;
}

void OokFrameDecoder::dropPrefix(uint16_t count) {
    if (!count) return;
    std::move(width + count, width + used, width);
    std::move(level + count, level + used, level);
    used -= count;
    checked = checked > count ? checked - count : 0;
}

void OokFrameDecoder::keepTail() {
    const uint16_t drop = lastSync >= 0 ? lastSync : (used > 3 ? used - 3 : 0);
    dropPrefix(drop);
}

void OokFrameDecoder::estimateStreamTiming(uint16_t& shortUs, uint16_t& longUs) const {
    uint16_t sorted[Capacity];
    uint16_t count = 0;
    for (uint16_t i = 0; i < used; ++i) if (width[i] >= 100) sorted[count++] = width[i];
    if (count < 4) return;
    std::sort(sorted, sorted + count);
    uint32_t shortCenter = sorted[count / 4], longCenter = sorted[count * 3 / 4];
    const uint32_t ceiling = longCenter * 2;
    for (uint8_t pass = 0; pass < 6; ++pass) {
        const uint32_t middle = (shortCenter + longCenter) / 2;
        uint32_t shortSum = 0, longSum = 0;
        uint16_t shortCount = 0, longCount = 0;
        for (uint16_t i = 0; i < count && sorted[i] <= ceiling; ++i) {
            if (sorted[i] <= middle) { shortSum += sorted[i]; ++shortCount; }
            else { longSum += sorted[i]; ++longCount; }
        }
        if (!shortCount || !longCount) return;
        shortCenter = shortSum / shortCount;
        longCenter = longSum / longCount;
    }
    shortUs = shortCenter;
    longUs = longCenter;
}

bool OokFrameDecoder::decode(uint16_t start, uint16_t end, Frame& frame) const {
    if (end <= start || (end - start) % 2) return false;
    frame.bits = (end - start) / 2;
    if (frame.bits < 8 || frame.bits > sizeof(frame.data) * 8) return false;
    uint32_t periods[Capacity / 2];
    uint32_t shortSum = 0, longSum = 0;
    for (uint16_t bit = 0; bit < frame.bits; ++bit) {
        const uint16_t i = start + bit * 2;
        const uint16_t high = width[i], low = width[i + 1];
        if (!level[i] || level[i + 1] || std::min(high, low) < 100) return false;
        shortSum += std::min(high, low);
        longSum += std::max(high, low);
        periods[bit] = uint32_t(high) + low;
        if (high > low) frame.data[bit / 8] |= 1U << (7 - bit % 8);
    }
    std::sort(periods, periods + frame.bits);
    const uint32_t period = periods[frame.bits / 2];
    bool widthsOk = true, periodsOk = true;
    for (uint16_t i = start; i < end; i += 2) {
        const uint16_t high = width[i], low = width[i + 1];
        const uint32_t sum = uint32_t(high) + low;
        const bool zero = near(high, frame.shortUs) && near(low, frame.longUs);
        const bool one = near(high, frame.longUs) && near(low, frame.shortUs);
        widthsOk &= zero != one;
        periodsOk &= difference(sum, period) <= std::max(period / 5, uint32_t(80)) &&
                     difference(high, low) * 100 >= sum * 15;
    }
    if (!widthsOk && periodsOk) {
        frame.shortUs = shortSum / frame.bits;
        frame.longUs = longSum / frame.bits;
    }
    return widthsOk || periodsOk;
}

void OokFrameDecoder::inspect(bool final, uint8_t& found, Frame& output) {
    const uint16_t stable = final ? used : (used > 2 ? used - 2 : 0);
    uint16_t streamShort = 0, streamLong = 0;
    estimateStreamTiming(streamShort, streamLong);
    lastSync = -1;
    if (!plausibleTiming(streamShort, streamLong)) {
        checked = stable;
        return;
    }
    const uint32_t minGap = std::max(streamShort * 8UL, streamLong * 3UL);
    for (uint16_t i = 0; i < stable; ++i) {
        if (level[i] || width[i] < minGap ||
            (i && (!level[i - 1] || !near(width[i - 1], streamShort)))) continue;
        const int previousSync = lastSync;
        lastSync = i;
        if (previousSync < 0 || i <= previousSync + 2 || i < checked) continue;
        Frame frame;
        frame.shortUs = streamShort;
        frame.longUs = streamLong;
        if (!decode(previousSync + 1, i - 1, frame)) continue;
        frame.syncUs = width[i];
        if (repeats && previous.bits == frame.bits &&
            std::memcmp(previous.data, frame.data, (frame.bits + 7) / 8) == 0 &&
            near(frame.shortUs, previous.shortUs) && near(frame.longUs, previous.longUs) &&
            near(frame.syncUs, previous.syncUs)) {
            if (repeats < 255) ++repeats;
            output = frame;
            found = repeats;
        } else {
            previous = frame;
            repeats = 1;
            if (!found) output = frame;  // 供扫频器保留当前频点，等下一帧确认
        }
    }
    checked = stable;
}
