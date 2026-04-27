#pragma once
#include <string>
#include <vector>

namespace settings {

struct Data {
    std::vector<std::string> zones;   // IANA ids (UTF-8 ASCII)
    bool use24h = true;
    bool showSeconds = false;
    bool alwaysOnTop = false;
    bool runAtStartup = false;
};

Data& Get();
void Load();
void Save();

// HKCU run-key helpers.
void ApplyRunAtStartup(bool enabled);

} // namespace settings
