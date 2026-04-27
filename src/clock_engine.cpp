#include "clock_engine.h"
#include "tz.h"
#include <windows.h>

namespace clock_engine {

namespace {

int64_t gOffsetMs = 0;

int64_t SystemUtcMs() {
    FILETIME ft;
    GetSystemTimePreciseAsFileTime(&ft);
    ULARGE_INTEGER u;
    u.LowPart = ft.dwLowDateTime;
    u.HighPart = ft.dwHighDateTime;
    // FILETIME is 100-ns since 1601-01-01; epoch delta to 1970 in 100-ns:
    constexpr uint64_t kEpochDelta = 116444736000000000ULL;
    return static_cast<int64_t>((u.QuadPart - kEpochDelta) / 10000ULL);
}

} // namespace

int64_t NowUtcMs() { return SystemUtcMs() + gOffsetMs; }
int64_t OffsetMs() { return gOffsetMs; }
bool IsOverridden() { return gOffsetMs != 0; }

bool SetFromLocal(const char* zoneId,
                  int year, int month, int day,
                  int hour, int minute, int second) {
    int64_t targetUtc = 0;
    if (!tz::MillisFromLocal(zoneId, year, month, day, hour, minute, second, targetUtc))
        return false;
    gOffsetMs = targetUtc - SystemUtcMs();
    return true;
}

void Reset() { gOffsetMs = 0; }

} // namespace clock_engine
