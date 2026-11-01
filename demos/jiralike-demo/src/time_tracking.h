#pragma once

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <sstream>
#include <string>
#include <string_view>

namespace time_tracking {

// Standard Jira time tracking conversion constants:
// 1 minute = 60 seconds
// 1 hour   = 60 minutes = 3,600 seconds
// 1 day    = 8 hours    = 28,800 seconds
// 1 week   = 5 days     = 40 hours = 144,000 seconds
inline constexpr int64_t SECONDS_PER_MINUTE = 60;
inline constexpr int64_t SECONDS_PER_HOUR   = 3600;
inline constexpr int64_t HOURS_PER_DAY      = 8;
inline constexpr int64_t DAYS_PER_WEEK      = 5;
inline constexpr int64_t SECONDS_PER_DAY    = HOURS_PER_DAY * SECONDS_PER_HOUR;       // 28800
inline constexpr int64_t SECONDS_PER_WEEK   = DAYS_PER_WEEK * SECONDS_PER_DAY;        // 144000

struct DurationBreakdown {
    int64_t weeks   = 0;
    int64_t days    = 0;
    int64_t hours   = 0;
    int64_t minutes = 0;
    int64_t seconds = 0;
    int64_t total_seconds = 0;
};

inline DurationBreakdown decompose_seconds(int64_t total_sec) {
    DurationBreakdown d;
    if (total_sec <= 0) {
        return d;
    }
    d.total_seconds = total_sec;
    int64_t rem = total_sec;

    d.weeks = rem / SECONDS_PER_WEEK;
    rem %= SECONDS_PER_WEEK;

    d.days = rem / SECONDS_PER_DAY;
    rem %= SECONDS_PER_DAY;

    d.hours = rem / SECONDS_PER_HOUR;
    rem %= SECONDS_PER_HOUR;

    d.minutes = rem / SECONDS_PER_MINUTE;
    d.seconds = rem % SECONDS_PER_MINUTE;
    return d;
}

// Formats a duration in seconds into Jira human-readable string: "2w 3d 4h 30m"
inline std::string format_seconds_to_duration(int64_t total_sec, bool empty_when_zero = false) {
    if (total_sec <= 0) {
        return empty_when_zero ? "" : "0m";
    }
    DurationBreakdown d = decompose_seconds(total_sec);
    std::ostringstream oss;
    bool first = true;
    auto append_part = [&](int64_t val, const char *suffix) {
        if (val > 0) {
            if (!first) oss << ' ';
            oss << val << suffix;
            first = false;
        }
    };
    append_part(d.weeks, "w");
    append_part(d.days, "d");
    append_part(d.hours, "h");
    append_part(d.minutes, "m");
    if (first && d.seconds > 0) {
        oss << d.seconds << "s";
    }
    return oss.str();
}

// Parses a Jira duration string such as "2w 3d 4h 30m", "1.5h", "45m", "2d" into seconds.
// If the input is empty or "0", sets out_seconds = 0 and returns true.
// Unitless numbers (e.g. "90") are interpreted according to default_unit ('m' = minutes by default).
inline bool parse_duration_to_seconds(std::string_view raw,
                                      int64_t &out_seconds,
                                      std::string &err_msg,
                                      char default_unit = 'm') {
    out_seconds = 0;
    err_msg.clear();

    // Trim whitespace
    size_t start = 0;
    while (start < raw.size() && std::isspace(static_cast<unsigned char>(raw[start]))) {
        ++start;
    }
    size_t end = raw.size();
    while (end > start && std::isspace(static_cast<unsigned char>(raw[end - 1]))) {
        --end;
    }
    if (start >= end) {
        return true;
    }

    std::string_view sv = raw.substr(start, end - start);
    if (sv == "0") {
        out_seconds = 0;
        return true;
    }

    double total = 0.0;
    size_t i = 0;
    bool found_any_token = false;

    while (i < sv.size()) {
        // Skip whitespace and commas
        while (i < sv.size() && (std::isspace(static_cast<unsigned char>(sv[i])) || sv[i] == ',')) {
            ++i;
        }
        if (i >= sv.size()) break;

        // Parse numeric value (integer or decimal)
        size_t num_start = i;
        bool seen_dot = false;
        while (i < sv.size()) {
            unsigned char c = static_cast<unsigned char>(sv[i]);
            if (std::isdigit(c)) {
                ++i;
            } else if (c == '.' && !seen_dot) {
                seen_dot = true;
                ++i;
            } else {
                break;
            }
        }

        if (i == num_start) {
            err_msg = "Invalid duration format near '" + std::string(sv.substr(i)) +
                      "'. Expected format like '2w 3d 4h 30m'.";
            return false;
        }

        double val = 0.0;
        try {
            val = std::stod(std::string(sv.substr(num_start, i - num_start)));
        } catch (...) {
            err_msg = "Invalid number in duration '" + std::string(sv.substr(num_start, i - num_start)) + "'.";
            return false;
        }
        if (val < 0.0) {
            err_msg = "Duration cannot be negative.";
            return false;
        }

        // Skip optional whitespace between number and unit
        while (i < sv.size() && std::isspace(static_cast<unsigned char>(sv[i]))) {
            ++i;
        }

        // Parse unit letters
        size_t unit_start = i;
        while (i < sv.size() && std::isalpha(static_cast<unsigned char>(sv[i]))) {
            ++i;
        }

        std::string unit_str;
        unit_str.reserve(i - unit_start);
        for (size_t k = unit_start; k < i; ++k) {
            unit_str.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(sv[k]))));
        }

        int64_t multiplier = SECONDS_PER_MINUTE;
        if (unit_str.empty()) {
            switch (std::tolower(static_cast<unsigned char>(default_unit))) {
            case 'w': multiplier = SECONDS_PER_WEEK; break;
            case 'd': multiplier = SECONDS_PER_DAY; break;
            case 'h': multiplier = SECONDS_PER_HOUR; break;
            case 's': multiplier = 1; break;
            case 'm':
            default:  multiplier = SECONDS_PER_MINUTE; break;
            }
        } else if (unit_str == "w" || unit_str == "wk" || unit_str == "wks" ||
                   unit_str == "week" || unit_str == "weeks") {
            multiplier = SECONDS_PER_WEEK;
        } else if (unit_str == "d" || unit_str == "day" || unit_str == "days") {
            multiplier = SECONDS_PER_DAY;
        } else if (unit_str == "h" || unit_str == "hr" || unit_str == "hrs" ||
                   unit_str == "hour" || unit_str == "hours") {
            multiplier = SECONDS_PER_HOUR;
        } else if (unit_str == "m" || unit_str == "min" || unit_str == "mins" ||
                   unit_str == "minute" || unit_str == "minutes") {
            multiplier = SECONDS_PER_MINUTE;
        } else if (unit_str == "s" || unit_str == "sec" || unit_str == "secs" ||
                   unit_str == "second" || unit_str == "seconds") {
            multiplier = 1;
        } else {
            err_msg = "Unknown duration unit '" + unit_str +
                      "'. Supported units: w (weeks), d (days), h (hours), m (minutes), s (seconds).";
            return false;
        }

        total += val * static_cast<double>(multiplier);
        found_any_token = true;
    }

    if (!found_any_token) {
        out_seconds = 0;
        return true;
    }

    out_seconds = static_cast<int64_t>(std::llround(total));
    return true;
}

// Computes the new remaining_estimate_seconds after logging work according to Jira's 4 adjustment modes:
// - "auto"   (or "auto_adjust"): Subtract time_spent_seconds from current_remaining (clamped >= 0)
// - "leave"  (or "leave_unchanged"): Keep current_remaining unchanged
// - "set"    (or "set_to"): Set remaining_estimate_seconds to custom_seconds (clamped >= 0)
// - "reduce" (or "reduce_by"): Subtract custom_seconds from current_remaining (clamped >= 0)
inline int64_t compute_adjusted_remaining(int64_t current_remaining,
                                          int64_t time_spent_seconds,
                                          std::string_view mode,
                                          int64_t custom_seconds = 0) {
    int64_t cur = std::max<int64_t>(0, current_remaining);
    int64_t spent = std::max<int64_t>(0, time_spent_seconds);
    int64_t cust = std::max<int64_t>(0, custom_seconds);

    if (mode == "leave" || mode == "leave_unchanged" || mode == "unchanged") {
        return cur;
    }
    if (mode == "set" || mode == "set_to" || mode == "manual") {
        return cust;
    }
    if (mode == "reduce" || mode == "reduce_by") {
        return std::max<int64_t>(0, cur - cust);
    }
    // Default: "auto" / "auto_adjust"
    return std::max<int64_t>(0, cur - spent);
}

inline std::string normalize_adjustment_mode(std::string_view raw) {
    if (raw == "leave" || raw == "leave_unchanged" || raw == "unchanged") return "leave";
    if (raw == "set" || raw == "set_to" || raw == "manual") return "set";
    if (raw == "reduce" || raw == "reduce_by") return "reduce";
    return "auto";
}

} // namespace time_tracking
