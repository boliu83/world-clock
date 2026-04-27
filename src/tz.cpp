#include "tz.h"
#include <windows.h>

// Windows 10 SDK umbrella header for ICU C API.
#include <icu.h>

#include <algorithm>
#include <string>
#include <vector>
#include <mutex>

namespace tz {

namespace {

bool gInit = false;
std::vector<std::string> gAllZones;
std::once_flag gZonesFlag;

// Convert UTF-8 ASCII zoneId to UChar (UTF-16).
std::vector<UChar> ToUChar(const std::string& s) {
    std::vector<UChar> r; r.reserve(s.size() + 1);
    for (char c : s) r.push_back(static_cast<UChar>(static_cast<unsigned char>(c)));
    r.push_back(0);
    return r;
}

std::wstring UCharsToW(const UChar* p, int32_t len) {
    std::wstring w; w.reserve(len);
    for (int32_t i = 0; i < len; ++i) w.push_back(static_cast<wchar_t>(p[i]));
    return w;
}

void BuildZoneList() {
    UErrorCode ec = U_ZERO_ERROR;
    UEnumeration* e = ucal_openTimeZones(&ec);
    if (U_FAILURE(ec) || !e) return;
    int32_t len = 0;
    while (true) {
        const UChar* s = uenum_unext(e, &len, &ec);
        if (U_FAILURE(ec) || !s) break;
        std::string ascii; ascii.reserve(len);
        for (int32_t i = 0; i < len; ++i) ascii.push_back(static_cast<char>(s[i]));
        // Keep only canonical region/city style (contains '/').
        if (ascii.find('/') != std::string::npos) {
            gAllZones.push_back(std::move(ascii));
        }
    }
    uenum_close(e);
    std::sort(gAllZones.begin(), gAllZones.end());
    gAllZones.erase(std::unique(gAllZones.begin(), gAllZones.end()), gAllZones.end());
}

} // namespace

bool Initialize() {
    // icu.dll is implicitly linked; just mark initialized.
    gInit = true;
    return true;
}

void Shutdown() { gInit = false; }

bool FormatMillis(const std::string& zoneId, int64_t utcMs, Fields& out) {
    if (!gInit) return false;
    auto zid = ToUChar(zoneId);
    UErrorCode ec = U_ZERO_ERROR;
    UCalendar* cal = ucal_open(zid.data(), -1, "en_US", UCAL_DEFAULT, &ec);
    if (U_FAILURE(ec) || !cal) return false;
    ucal_setMillis(cal, static_cast<UDate>(utcMs), &ec);
    if (U_FAILURE(ec)) { ucal_close(cal); return false; }

    out.year         = ucal_get(cal, UCAL_YEAR, &ec);
    out.month        = ucal_get(cal, UCAL_MONTH, &ec) + 1;
    out.day          = ucal_get(cal, UCAL_DAY_OF_MONTH, &ec);
    out.hour         = ucal_get(cal, UCAL_HOUR_OF_DAY, &ec);
    out.minute       = ucal_get(cal, UCAL_MINUTE, &ec);
    out.second       = ucal_get(cal, UCAL_SECOND, &ec);
    out.dayOfWeek    = ucal_get(cal, UCAL_DAY_OF_WEEK, &ec);
    int32_t rawOff   = ucal_get(cal, UCAL_ZONE_OFFSET, &ec);
    int32_t dstOff   = ucal_get(cal, UCAL_DST_OFFSET, &ec);
    out.gmtOffsetMinutes = (rawOff + dstOff) / 60000;
    out.isDst        = dstOff != 0;

    UChar name[128]; UErrorCode ec2 = U_ZERO_ERROR;
    int32_t nlen = ucal_getTimeZoneDisplayName(cal, UCAL_SHORT_STANDARD, "en_US",
                                               name, 128, &ec2);
    if (U_SUCCESS(ec2) && nlen > 0) {
        out.displayName = UCharsToW(name, nlen);
    } else {
        out.displayName.clear();
    }

    ucal_close(cal);
    return U_SUCCESS(ec);
}

bool MillisFromLocal(const std::string& zoneId,
                     int year, int month, int day,
                     int hour, int minute, int second,
                     int64_t& outUtcMs) {
    if (!gInit) return false;
    auto zid = ToUChar(zoneId);
    UErrorCode ec = U_ZERO_ERROR;
    UCalendar* cal = ucal_open(zid.data(), -1, "en_US", UCAL_DEFAULT, &ec);
    if (U_FAILURE(ec) || !cal) return false;
    ucal_clear(cal);
    ucal_setDateTime(cal, year, month - 1, day, hour, minute, second, &ec);
    if (U_FAILURE(ec)) { ucal_close(cal); return false; }
    UDate ms = ucal_getMillis(cal, &ec);
    ucal_close(cal);
    if (U_FAILURE(ec)) return false;
    outUtcMs = static_cast<int64_t>(ms);
    return true;
}

const std::vector<std::string>& AllZones() {
    std::call_once(gZonesFlag, BuildZoneList);
    return gAllZones;
}

std::wstring FriendlyCity(const std::string& zoneId) {
    auto slash = zoneId.rfind('/');
    std::string city = (slash == std::string::npos) ? zoneId : zoneId.substr(slash + 1);
    std::wstring w;
    w.reserve(city.size());
    for (char c : city) {
        if (c == '_') w.push_back(L' ');
        else w.push_back(static_cast<wchar_t>(static_cast<unsigned char>(c)));
    }
    return w;
}

std::wstring FriendlyLocation(const std::string& zoneId) {
    std::wstring city = FriendlyCity(zoneId);

    struct CountryRule { const char* prefix; const wchar_t* country; };
    static const CountryRule kCountries[] = {
        {"Africa/Abidjan", L"Cote d'Ivoire"},
        {"Africa/Accra", L"Ghana"},
        {"Africa/Addis_Ababa", L"Ethiopia"},
        {"Africa/Algiers", L"Algeria"},
        {"Africa/Asmara", L"Eritrea"},
        {"Africa/Bamako", L"Mali"},
        {"Africa/Bangui", L"Central African Republic"},
        {"Africa/Banjul", L"Gambia"},
        {"Africa/Bissau", L"Guinea-Bissau"},
        {"Africa/Blantyre", L"Malawi"},
        {"Africa/Brazzaville", L"Republic of the Congo"},
        {"Africa/Bujumbura", L"Burundi"},
        {"Africa/Cairo", L"Egypt"},
        {"Africa/Casablanca", L"Morocco"},
        {"Africa/Ceuta", L"Spain"},
        {"Africa/Conakry", L"Guinea"},
        {"Africa/Dakar", L"Senegal"},
        {"Africa/Dar_es_Salaam", L"Tanzania"},
        {"Africa/Djibouti", L"Djibouti"},
        {"Africa/Douala", L"Cameroon"},
        {"Africa/El_Aaiun", L"Western Sahara"},
        {"Africa/Freetown", L"Sierra Leone"},
        {"Africa/Gaborone", L"Botswana"},
        {"Africa/Harare", L"Zimbabwe"},
        {"Africa/Johannesburg", L"South Africa"},
        {"Africa/Juba", L"South Sudan"},
        {"Africa/Kampala", L"Uganda"},
        {"Africa/Khartoum", L"Sudan"},
        {"Africa/Kigali", L"Rwanda"},
        {"Africa/Kinshasa", L"Democratic Republic of the Congo"},
        {"Africa/Lagos", L"Nigeria"},
        {"Africa/Libreville", L"Gabon"},
        {"Africa/Lome", L"Togo"},
        {"Africa/Luanda", L"Angola"},
        {"Africa/Lubumbashi", L"Democratic Republic of the Congo"},
        {"Africa/Lusaka", L"Zambia"},
        {"Africa/Malabo", L"Equatorial Guinea"},
        {"Africa/Maputo", L"Mozambique"},
        {"Africa/Maseru", L"Lesotho"},
        {"Africa/Mbabane", L"Eswatini"},
        {"Africa/Mogadishu", L"Somalia"},
        {"Africa/Monrovia", L"Liberia"},
        {"Africa/Nairobi", L"Kenya"},
        {"Africa/Ndjamena", L"Chad"},
        {"Africa/Niamey", L"Niger"},
        {"Africa/Nouakchott", L"Mauritania"},
        {"Africa/Ouagadougou", L"Burkina Faso"},
        {"Africa/Porto-Novo", L"Benin"},
        {"Africa/Sao_Tome", L"Sao Tome and Principe"},
        {"Africa/Tripoli", L"Libya"},
        {"Africa/Tunis", L"Tunisia"},
        {"Africa/Windhoek", L"Namibia"},
        {"America/Argentina/", L"Argentina"},
        {"America/Indiana/", L"United States"},
        {"America/Kentucky/", L"United States"},
        {"America/North_Dakota/", L"United States"},
        {"America/Adak", L"United States"},
        {"America/Anchorage", L"United States"},
        {"America/Anguilla", L"Anguilla"},
        {"America/Antigua", L"Antigua and Barbuda"},
        {"America/Araguaina", L"Brazil"},
        {"America/Aruba", L"Aruba"},
        {"America/Asuncion", L"Paraguay"},
        {"America/Atikokan", L"Canada"},
        {"America/Bahia", L"Brazil"},
        {"America/Bahia_Banderas", L"Mexico"},
        {"America/Barbados", L"Barbados"},
        {"America/Belem", L"Brazil"},
        {"America/Belize", L"Belize"},
        {"America/Blanc-Sablon", L"Canada"},
        {"America/Boa_Vista", L"Brazil"},
        {"America/Bogota", L"Colombia"},
        {"America/Boise", L"United States"},
        {"America/Cambridge_Bay", L"Canada"},
        {"America/Campo_Grande", L"Brazil"},
        {"America/Cancun", L"Mexico"},
        {"America/Caracas", L"Venezuela"},
        {"America/Cayenne", L"French Guiana"},
        {"America/Cayman", L"Cayman Islands"},
        {"America/Chicago", L"United States"},
        {"America/Chihuahua", L"Mexico"},
        {"America/Ciudad_Juarez", L"Mexico"},
        {"America/Costa_Rica", L"Costa Rica"},
        {"America/Creston", L"Canada"},
        {"America/Cuiaba", L"Brazil"},
        {"America/Curacao", L"Curacao"},
        {"America/Danmarkshavn", L"Greenland"},
        {"America/Dawson", L"Canada"},
        {"America/Dawson_Creek", L"Canada"},
        {"America/Denver", L"United States"},
        {"America/Detroit", L"United States"},
        {"America/Dominica", L"Dominica"},
        {"America/Edmonton", L"Canada"},
        {"America/Eirunepe", L"Brazil"},
        {"America/El_Salvador", L"El Salvador"},
        {"America/Fort_Nelson", L"Canada"},
        {"America/Fortaleza", L"Brazil"},
        {"America/Glace_Bay", L"Canada"},
        {"America/Goose_Bay", L"Canada"},
        {"America/Grand_Turk", L"Turks and Caicos Islands"},
        {"America/Grenada", L"Grenada"},
        {"America/Guadeloupe", L"Guadeloupe"},
        {"America/Guatemala", L"Guatemala"},
        {"America/Guayaquil", L"Ecuador"},
        {"America/Guyana", L"Guyana"},
        {"America/Halifax", L"Canada"},
        {"America/Havana", L"Cuba"},
        {"America/Hermosillo", L"Mexico"},
        {"America/Inuvik", L"Canada"},
        {"America/Iqaluit", L"Canada"},
        {"America/Jamaica", L"Jamaica"},
        {"America/Juneau", L"United States"},
        {"America/La_Paz", L"Bolivia"},
        {"America/Lima", L"Peru"},
        {"America/Los_Angeles", L"United States"},
        {"America/Lower_Princes", L"Sint Maarten"},
        {"America/Maceio", L"Brazil"},
        {"America/Managua", L"Nicaragua"},
        {"America/Manaus", L"Brazil"},
        {"America/Marigot", L"Saint Martin"},
        {"America/Martinique", L"Martinique"},
        {"America/Matamoros", L"Mexico"},
        {"America/Mazatlan", L"Mexico"},
        {"America/Menominee", L"United States"},
        {"America/Merida", L"Mexico"},
        {"America/Metlakatla", L"United States"},
        {"America/Mexico_City", L"Mexico"},
        {"America/Miquelon", L"Saint Pierre and Miquelon"},
        {"America/Moncton", L"Canada"},
        {"America/Monterrey", L"Mexico"},
        {"America/Montevideo", L"Uruguay"},
        {"America/Montserrat", L"Montserrat"},
        {"America/Nassau", L"Bahamas"},
        {"America/New_York", L"United States"},
        {"America/Nome", L"United States"},
        {"America/Noronha", L"Brazil"},
        {"America/Ojinaga", L"Mexico"},
        {"America/Panama", L"Panama"},
        {"America/Paramaribo", L"Suriname"},
        {"America/Phoenix", L"United States"},
        {"America/Port-au-Prince", L"Haiti"},
        {"America/Port_of_Spain", L"Trinidad and Tobago"},
        {"America/Porto_Velho", L"Brazil"},
        {"America/Puerto_Rico", L"Puerto Rico"},
        {"America/Punta_Arenas", L"Chile"},
        {"America/Rankin_Inlet", L"Canada"},
        {"America/Recife", L"Brazil"},
        {"America/Regina", L"Canada"},
        {"America/Resolute", L"Canada"},
        {"America/Rio_Branco", L"Brazil"},
        {"America/Santarem", L"Brazil"},
        {"America/Santiago", L"Chile"},
        {"America/Santo_Domingo", L"Dominican Republic"},
        {"America/Sao_Paulo", L"Brazil"},
        {"America/Scoresbysund", L"Greenland"},
        {"America/Sitka", L"United States"},
        {"America/St_Barthelemy", L"Saint Barthelemy"},
        {"America/St_Johns", L"Canada"},
        {"America/St_Kitts", L"Saint Kitts and Nevis"},
        {"America/St_Lucia", L"Saint Lucia"},
        {"America/St_Thomas", L"U.S. Virgin Islands"},
        {"America/St_Vincent", L"Saint Vincent and the Grenadines"},
        {"America/Swift_Current", L"Canada"},
        {"America/Tegucigalpa", L"Honduras"},
        {"America/Thule", L"Greenland"},
        {"America/Tijuana", L"Mexico"},
        {"America/Toronto", L"Canada"},
        {"America/Tortola", L"British Virgin Islands"},
        {"America/Vancouver", L"Canada"},
        {"America/Whitehorse", L"Canada"},
        {"America/Winnipeg", L"Canada"},
        {"America/Yakutat", L"United States"},
        {"Arctic/Longyearbyen", L"Norway"},
        {"Asia/Aden", L"Yemen"},
        {"Asia/Almaty", L"Kazakhstan"},
        {"Asia/Amman", L"Jordan"},
        {"Asia/Anadyr", L"Russia"},
        {"Asia/Aqtau", L"Kazakhstan"},
        {"Asia/Aqtobe", L"Kazakhstan"},
        {"Asia/Ashgabat", L"Turkmenistan"},
        {"Asia/Atyrau", L"Kazakhstan"},
        {"Asia/Baghdad", L"Iraq"},
        {"Asia/Bahrain", L"Bahrain"},
        {"Asia/Baku", L"Azerbaijan"},
        {"Asia/Bangkok", L"Thailand"},
        {"Asia/Barnaul", L"Russia"},
        {"Asia/Beirut", L"Lebanon"},
        {"Asia/Bishkek", L"Kyrgyzstan"},
        {"Asia/Brunei", L"Brunei"},
        {"Asia/Chita", L"Russia"},
        {"Asia/Choibalsan", L"Mongolia"},
        {"Asia/Colombo", L"Sri Lanka"},
        {"Asia/Damascus", L"Syria"},
        {"Asia/Dhaka", L"Bangladesh"},
        {"Asia/Dili", L"Timor-Leste"},
        {"Asia/Dubai", L"United Arab Emirates"},
        {"Asia/Dushanbe", L"Tajikistan"},
        {"Asia/Famagusta", L"Cyprus"},
        {"Asia/Gaza", L"Palestine"},
        {"Asia/Hebron", L"Palestine"},
        {"Asia/Ho_Chi_Minh", L"Vietnam"},
        {"Asia/Hong_Kong", L"Hong Kong"},
        {"Asia/Hovd", L"Mongolia"},
        {"Asia/Irkutsk", L"Russia"},
        {"Asia/Jakarta", L"Indonesia"},
        {"Asia/Jayapura", L"Indonesia"},
        {"Asia/Jerusalem", L"Israel"},
        {"Asia/Kabul", L"Afghanistan"},
        {"Asia/Kamchatka", L"Russia"},
        {"Asia/Karachi", L"Pakistan"},
        {"Asia/Kathmandu", L"Nepal"},
        {"Asia/Khandyga", L"Russia"},
        {"Asia/Kolkata", L"India"},
        {"Asia/Krasnoyarsk", L"Russia"},
        {"Asia/Kuala_Lumpur", L"Malaysia"},
        {"Asia/Kuching", L"Malaysia"},
        {"Asia/Kuwait", L"Kuwait"},
        {"Asia/Macau", L"Macau"},
        {"Asia/Magadan", L"Russia"},
        {"Asia/Makassar", L"Indonesia"},
        {"Asia/Manila", L"Philippines"},
        {"Asia/Muscat", L"Oman"},
        {"Asia/Nicosia", L"Cyprus"},
        {"Asia/Novokuznetsk", L"Russia"},
        {"Asia/Novosibirsk", L"Russia"},
        {"Asia/Omsk", L"Russia"},
        {"Asia/Oral", L"Kazakhstan"},
        {"Asia/Phnom_Penh", L"Cambodia"},
        {"Asia/Pontianak", L"Indonesia"},
        {"Asia/Pyongyang", L"North Korea"},
        {"Asia/Qatar", L"Qatar"},
        {"Asia/Qostanay", L"Kazakhstan"},
        {"Asia/Qyzylorda", L"Kazakhstan"},
        {"Asia/Riyadh", L"Saudi Arabia"},
        {"Asia/Sakhalin", L"Russia"},
        {"Asia/Samarkand", L"Uzbekistan"},
        {"Asia/Seoul", L"South Korea"},
        {"Asia/Shanghai", L"China"},
        {"Asia/Singapore", L"Singapore"},
        {"Asia/Srednekolymsk", L"Russia"},
        {"Asia/Taipei", L"Taiwan"},
        {"Asia/Tashkent", L"Uzbekistan"},
        {"Asia/Tbilisi", L"Georgia"},
        {"Asia/Tehran", L"Iran"},
        {"Asia/Thimphu", L"Bhutan"},
        {"Asia/Tokyo", L"Japan"},
        {"Asia/Tomsk", L"Russia"},
        {"Asia/Ulaanbaatar", L"Mongolia"},
        {"Asia/Urumqi", L"China"},
        {"Asia/Ust-Nera", L"Russia"},
        {"Asia/Vientiane", L"Laos"},
        {"Asia/Vladivostok", L"Russia"},
        {"Asia/Yakutsk", L"Russia"},
        {"Asia/Yangon", L"Myanmar"},
        {"Asia/Yekaterinburg", L"Russia"},
        {"Asia/Yerevan", L"Armenia"},
        {"Australia/", L"Australia"},
        {"Europe/Amsterdam", L"Netherlands"},
        {"Europe/Andorra", L"Andorra"},
        {"Europe/Astrakhan", L"Russia"},
        {"Europe/Athens", L"Greece"},
        {"Europe/Belgrade", L"Serbia"},
        {"Europe/Berlin", L"Germany"},
        {"Europe/Bratislava", L"Slovakia"},
        {"Europe/Brussels", L"Belgium"},
        {"Europe/Bucharest", L"Romania"},
        {"Europe/Budapest", L"Hungary"},
        {"Europe/Busingen", L"Germany"},
        {"Europe/Chisinau", L"Moldova"},
        {"Europe/Copenhagen", L"Denmark"},
        {"Europe/Dublin", L"Ireland"},
        {"Europe/Gibraltar", L"Gibraltar"},
        {"Europe/Guernsey", L"Guernsey"},
        {"Europe/Helsinki", L"Finland"},
        {"Europe/Isle_of_Man", L"Isle of Man"},
        {"Europe/Istanbul", L"Turkey"},
        {"Europe/Jersey", L"Jersey"},
        {"Europe/Kaliningrad", L"Russia"},
        {"Europe/Kirov", L"Russia"},
        {"Europe/Kyiv", L"Ukraine"},
        {"Europe/Lisbon", L"Portugal"},
        {"Europe/Ljubljana", L"Slovenia"},
        {"Europe/London", L"United Kingdom"},
        {"Europe/Luxembourg", L"Luxembourg"},
        {"Europe/Madrid", L"Spain"},
        {"Europe/Malta", L"Malta"},
        {"Europe/Mariehamn", L"Finland"},
        {"Europe/Minsk", L"Belarus"},
        {"Europe/Monaco", L"Monaco"},
        {"Europe/Moscow", L"Russia"},
        {"Europe/Oslo", L"Norway"},
        {"Europe/Paris", L"France"},
        {"Europe/Podgorica", L"Montenegro"},
        {"Europe/Prague", L"Czechia"},
        {"Europe/Riga", L"Latvia"},
        {"Europe/Rome", L"Italy"},
        {"Europe/Samara", L"Russia"},
        {"Europe/San_Marino", L"San Marino"},
        {"Europe/Sarajevo", L"Bosnia and Herzegovina"},
        {"Europe/Saratov", L"Russia"},
        {"Europe/Simferopol", L"Ukraine"},
        {"Europe/Skopje", L"North Macedonia"},
        {"Europe/Sofia", L"Bulgaria"},
        {"Europe/Stockholm", L"Sweden"},
        {"Europe/Tallinn", L"Estonia"},
        {"Europe/Tirane", L"Albania"},
        {"Europe/Ulyanovsk", L"Russia"},
        {"Europe/Vaduz", L"Liechtenstein"},
        {"Europe/Vatican", L"Vatican City"},
        {"Europe/Vienna", L"Austria"},
        {"Europe/Vilnius", L"Lithuania"},
        {"Europe/Volgograd", L"Russia"},
        {"Europe/Warsaw", L"Poland"},
        {"Europe/Zagreb", L"Croatia"},
        {"Europe/Zurich", L"Switzerland"},
        {"UTC", L"UTC"},
    };

    for (const auto& rule : kCountries) {
        const std::string prefix = rule.prefix;
        if (zoneId.compare(0, prefix.size(), prefix) == 0) {
            return city + L", " + rule.country;
        }
    }
    return city;
}

} // namespace tz
