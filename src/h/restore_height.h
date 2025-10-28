#ifndef RESTORE_HEIGHT_H
#define RESTORE_HEIGHT_H

#include <cstdint>
#include <ctime>
#include <string>
#include <stdexcept>
#include <cstdio>
#include <algorithm>
#include <cctype>

namespace restore_height {

// 120s target block time (Monero)
static constexpr int64_t kTargetSecsPerBlock = 120;

static constexpr int64_t kConservativeNonMainnetSecsPerBlock = 150;


struct Anchor { uint64_t height; std::time_t ts; };

inline Anchor anchor_for_network(int nettype /* 0=MAINNET, 1=TESTNET, 2=STAGENET */) {
    switch (nettype) {
    case 0: // MAINNET
        // v2 fork anchor: height 1,009,827 at unix time 1458748658
        return Anchor{1009827, static_cast<std::time_t>(1458748658)};
    case 1: // TESTNET

        return Anchor{2862744, static_cast<std::time_t>(1761436800)};
    case 2: // STAGENET
    default:

        return Anchor{1977817, static_cast<std::time_t>(1761436800)};
    }
}

// Parse YYYY-MM-DD (UTC) -> time_t at 00:00:00.
inline std::time_t parse_yyyy_mm_dd(const std::string& s) {
    int y, m, d;
    if (std::sscanf(s.c_str(), "%d-%d-%d", &y, &m, &d) != 3)
        throw std::invalid_argument("Bad date, expected YYYY-MM-DD");
    std::tm tm{};
    tm.tm_year = y - 1900;
    tm.tm_mon  = m - 1;
    tm.tm_mday = d;
    tm.tm_hour = 0; tm.tm_min = 0; tm.tm_sec = 0;
// Force UTC: use timegm if available; otherwise, adjust as needed in your codebase.
#if defined(_GNU_SOURCE) || defined(__USE_MISC)
    return timegm(&tm);
#else
    // Fallback: treat as local time; adjust if you need strict UTC semantics.
    return std::mktime(&tm);
#endif
}

// Core estimator: from unix timestamp -> height (with safety buffer).
inline uint64_t estimate_from_timestamp(std::time_t t,
                                        int nettype /* 0=MAINNET,1=TESTNET,2=STAGENET */,
                                        uint64_t safety_blocks = 7 * 720 /* ≈ one week */) {
    const Anchor a = anchor_for_network(nettype);

    // Choose secs-per-block: keep mainnet at 120s, bias non-mainnet to 150s.
    const int64_t spb = (nettype == 0) ? kTargetSecsPerBlock
                                       : kConservativeNonMainnetSecsPerBlock;


    const int64_t dt = static_cast<int64_t>(t) - static_cast<int64_t>(a.ts);
    // blocks since anchor (floor division)
    const int64_t since_anchor = dt >= 0 ? dt / spb
                                         : -(((-dt) + (spb - 1)) / spb);
    int64_t h = static_cast<int64_t>(a.height) + since_anchor - static_cast<int64_t>(safety_blocks);
    if (h < 0) h = 0;
    return static_cast<uint64_t>(h);
}

// Accept either a height (digits) or a date (YYYY-MM-DD) or an epoch seconds string.
inline uint64_t parse_height_or_date(const std::string& s,
                                     int nettype,
                                     uint64_t safety_blocks = 7 * 720) {
    bool is_num = !s.empty() && std::all_of(s.begin(), s.end(), ::isdigit);
    if (is_num) {
        // Treat short 10-digit/13-digit as epoch seconds/millis; longer purely as a height.
        if (s.size() == 10) { // seconds
            return estimate_from_timestamp(static_cast<std::time_t>(std::stoll(s)), nettype, safety_blocks);
        } else if (s.size() == 13) { // ms
            return estimate_from_timestamp(static_cast<std::time_t>(std::stoll(s) / 1000), nettype, safety_blocks);
        } else {
            return static_cast<uint64_t>(std::stoull(s)); // already a height
        }
    }
    // Otherwise, assume YYYY-MM-DD
    return estimate_from_timestamp(parse_yyyy_mm_dd(s), nettype, safety_blocks);
}

} // namespace restore_height

#endif // RESTORE_HEIGHT_H
