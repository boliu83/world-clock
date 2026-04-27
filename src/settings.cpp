#include "settings.h"
#include <windows.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <string>
#include <fstream>
#include <sstream>
#include <filesystem>

namespace settings {

namespace fs = std::filesystem;

namespace {

Data gData;

fs::path AppDataPath() {
    PWSTR roaming = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &roaming))) {
        return {};
    }
    fs::path p = roaming;
    CoTaskMemFree(roaming);
    p /= L"WorldClock";
    std::error_code ec;
    fs::create_directories(p, ec);
    return p / L"settings.json";
}

// ------- Minimal JSON writer (schema is fixed/flat) -------
std::string JsonEscape(const std::string& s) {
    std::string o; o.reserve(s.size() + 2);
    for (char c : s) {
        switch (c) {
            case '"': o += "\\\""; break;
            case '\\': o += "\\\\"; break;
            case '\n': o += "\\n"; break;
            case '\r': o += "\\r"; break;
            case '\t': o += "\\t"; break;
            default:
                if ((unsigned char)c < 0x20) {
                    char buf[8]; sprintf_s(buf, "\\u%04x", c);
                    o += buf;
                } else {
                    o += c;
                }
        }
    }
    return o;
}

// ------- Minimal JSON reader (tolerant, flat object) -------
// Extracts: "key": "string" or "key": true/false
// and an array of strings under "zones".
void SkipWs(const std::string& s, size_t& i) {
    while (i < s.size() && (s[i]==' '||s[i]=='\t'||s[i]=='\r'||s[i]=='\n')) ++i;
}

bool ReadString(const std::string& s, size_t& i, std::string& out) {
    SkipWs(s, i);
    if (i >= s.size() || s[i] != '"') return false;
    ++i;
    out.clear();
    while (i < s.size() && s[i] != '"') {
        if (s[i] == '\\' && i+1 < s.size()) {
            char n = s[i+1];
            if (n == 'n') out += '\n';
            else if (n == 'r') out += '\r';
            else if (n == 't') out += '\t';
            else out += n;
            i += 2;
        } else {
            out += s[i++];
        }
    }
    if (i < s.size()) ++i; // consume closing quote
    return true;
}

bool ReadBool(const std::string& s, size_t& i, bool& out) {
    SkipWs(s, i);
    if (s.compare(i, 4, "true") == 0)  { out = true;  i += 4; return true; }
    if (s.compare(i, 5, "false") == 0) { out = false; i += 5; return true; }
    return false;
}

void Parse(const std::string& s, Data& d) {
    size_t i = 0;
    SkipWs(s, i);
    if (i >= s.size() || s[i] != '{') return;
    ++i;
    while (i < s.size()) {
        SkipWs(s, i);
        if (i < s.size() && s[i] == '}') break;
        std::string key;
        if (!ReadString(s, i, key)) break;
        SkipWs(s, i);
        if (i >= s.size() || s[i] != ':') break;
        ++i;
        SkipWs(s, i);
        if (key == "zones") {
            if (i < s.size() && s[i] == '[') {
                ++i; d.zones.clear();
                while (i < s.size()) {
                    SkipWs(s, i);
                    if (i < s.size() && s[i] == ']') { ++i; break; }
                    std::string v;
                    if (!ReadString(s, i, v)) break;
                    d.zones.push_back(std::move(v));
                    SkipWs(s, i);
                    if (i < s.size() && s[i] == ',') ++i;
                }
            }
        } else {
            bool b = false;
            if (ReadBool(s, i, b)) {
                if      (key == "use24h")       d.use24h = b;
                else if (key == "showSeconds")  d.showSeconds = b;
                else if (key == "alwaysOnTop")  d.alwaysOnTop = b;
                else if (key == "runAtStartup") d.runAtStartup = b;
            } else {
                // skip value to next comma or end
                int depth = 0;
                while (i < s.size()) {
                    char c = s[i];
                    if (c == '{' || c == '[') ++depth;
                    else if (c == '}' || c == ']') {
                        if (depth == 0) break;
                        --depth;
                    } else if (c == ',' && depth == 0) break;
                    ++i;
                }
            }
        }
        SkipWs(s, i);
        if (i < s.size() && s[i] == ',') ++i;
    }
}

void ApplyDefaults(Data& d) {
    if (d.zones.empty()) {
        d.zones = {"America/Los_Angeles", "America/New_York", "Europe/London", "Asia/Tokyo"};
    }
}

} // namespace

Data& Get() { return gData; }

void Load() {
    fs::path p = AppDataPath();
    if (p.empty()) { ApplyDefaults(gData); return; }
    std::ifstream in(p, std::ios::binary);
    if (!in) { ApplyDefaults(gData); return; }
    std::stringstream ss; ss << in.rdbuf();
    Parse(ss.str(), gData);
    ApplyDefaults(gData);
}

void Save() {
    fs::path p = AppDataPath();
    if (p.empty()) return;
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    if (!out) return;
    out << "{\n";
    out << "  \"zones\": [";
    for (size_t i = 0; i < gData.zones.size(); ++i) {
        if (i) out << ", ";
        out << "\"" << JsonEscape(gData.zones[i]) << "\"";
    }
    out << "],\n";
    out << "  \"use24h\": "       << (gData.use24h       ? "true" : "false") << ",\n";
    out << "  \"showSeconds\": "  << (gData.showSeconds  ? "true" : "false") << ",\n";
    out << "  \"alwaysOnTop\": "  << (gData.alwaysOnTop  ? "true" : "false") << ",\n";
    out << "  \"runAtStartup\": " << (gData.runAtStartup ? "true" : "false") << "\n";
    out << "}\n";
}

void ApplyRunAtStartup(bool enabled) {
    HKEY hk;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
            L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
            0, KEY_SET_VALUE, &hk) != ERROR_SUCCESS) return;
    if (enabled) {
        wchar_t path[MAX_PATH];
        GetModuleFileNameW(nullptr, path, MAX_PATH);
        std::wstring quoted = L"\"";
        quoted += path; quoted += L"\"";
        RegSetValueExW(hk, L"WorldClock", 0, REG_SZ,
            reinterpret_cast<const BYTE*>(quoted.c_str()),
            static_cast<DWORD>((quoted.size() + 1) * sizeof(wchar_t)));
    } else {
        RegDeleteValueW(hk, L"WorldClock");
    }
    RegCloseKey(hk);
}

} // namespace settings
