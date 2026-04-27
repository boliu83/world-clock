#pragma once
#include <cstdint>

namespace clock_engine {

// Returns current "effective" UTC millis = system_now_utc + offsetMs.
int64_t NowUtcMs();

// Returns 0 when live, non-zero when user-overridden.
int64_t OffsetMs();

// User set time in `zoneId` to the given wall clock. Computes the UTC that would
// yield those fields in the zone and stores as offset from current system time.
bool SetFromLocal(const char* zoneId,
                  int year, int month, int day,
                  int hour, int minute, int second);

// Clear override.
void Reset();

bool IsOverridden();

} // namespace clock_engine
