#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace tz {

struct Fields {
    int year, month, day;    // month 1..12
    int hour, minute, second;
    int dayOfWeek;           // 1=Sun..7=Sat (ICU convention)
    int gmtOffsetMinutes;
    bool isDst;
    std::wstring displayName; // Short, generic or localized.
};

// Initialize ICU (loads icu.dll via static import). Returns false if unavailable.
bool Initialize();
void Shutdown();

// Convert UTC-millis-since-epoch to local fields in `zoneId` (IANA).
// Returns false if zoneId invalid.
bool FormatMillis(const std::string& zoneId, int64_t utcMs, Fields& out);

// Compute the UTC millis that, when interpreted in `zoneId`, yields the given
// wall-clock fields (useful for "user sets time in this zone").
bool MillisFromLocal(const std::string& zoneId,
                     int year, int month, int day,
                     int hour, int minute, int second,
                     int64_t& outUtcMs);

// Enumerate canonical IANA zone ids (ASCII). Sorted alphabetically.
const std::vector<std::string>& AllZones();

// Pretty short city name from an IANA id ("America/New_York" -> "New York").
std::wstring FriendlyCity(const std::string& zoneId);

// Friendly row label with country context ("America/New_York" -> "New York, United States").
std::wstring FriendlyLocation(const std::string& zoneId);

} // namespace tz
