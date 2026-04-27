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
    int popupHeightRows = 0;          // 0 = automatic, 1-20 = fixed row count
};

Data& Get();
void Load();
void Save();

// HKCU run-key helpers.
void ApplyRunAtStartup(bool enabled);

} // namespace settings
