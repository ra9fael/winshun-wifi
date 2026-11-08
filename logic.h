// logic.h — ra9fael.wifi's pure logic: profile XML parsing, SSID filtering,
// row ordering and de-duplication, RSSI→quality mapping, band/channel maths,
// auth classification, and the hint-row choice. No Win32 calls here, so
// tests/test_wifi.cpp runs every branch without a wireless adapter.
//
// English code/comments per project convention; user-facing strings follow
// the host locale, chosen by the `zh` flag the caller passes in.

#pragma once

#include "winshun.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

namespace wifiname {

// ---- settings ---------------------------------------------------------------------

struct Settings {
    bool includeHidden = true;
    bool showPasswordMasked = true;
};

inline Settings readSettings(const winshun::Json& settings)
{
    Settings st;
    const winshun::Json& hidden = settings["includeHidden"];
    st.includeHidden = hidden.isBool() ? hidden.toBool() : true;
    const winshun::Json& masked = settings["showPasswordMasked"];
    st.showPasswordMasked = masked.isBool() ? masked.toBool() : true;
    return st;
}

// ---- auth -------------------------------------------------------------------------

enum class Auth { Open, Wep, Wpa, Wpa2, Wpa3, Enterprise, Unknown };

// The `authentication` element of a profile, or the BSS type of a scan result,
// to the class the rows show. WPA3SAE and WPA3ENT both read as WPA3; anything
// with useOneX reads as Enterprise (the rows say which generation).
inline Auth authFromText(const std::string& raw, bool oneX = false)
{
    std::string t;
    t.reserve(raw.size());
    for (const char c : raw)
        t += static_cast<char>(::toupper(static_cast<unsigned char>(c)));
    if (t.empty())
        return Auth::Unknown;
    if (t == "OPEN" || t == "NONE")
        return Auth::Open;
    if (t == "WEP")
        return Auth::Wep;
    const bool wpa3 = t.find("WPA3") != std::string::npos || t.find("SAE") != std::string::npos;
    const bool wpa2 = t.find("WPA2") != std::string::npos;
    const bool wpa = !wpa2 && !wpa3 && t.find("WPA") != std::string::npos;
    const bool ent = oneX || t.find("ENT") != std::string::npos || t.find("EAP") != std::string::npos
        || t.find("802.1X") != std::string::npos;
    if (wpa3)
        return ent ? Auth::Enterprise : Auth::Wpa3;
    if (wpa2)
        return ent ? Auth::Enterprise : Auth::Wpa2;
    if (wpa)
        return ent ? Auth::Enterprise : Auth::Wpa;
    if (ent)
        return Auth::Enterprise;
    return Auth::Unknown;
}

inline bool isOpen(Auth a) { return a == Auth::Open; }

// The label a row carries: "open", "WEP", "WPA", "WPA2", "WPA3",
// "WPA2-Enterprise"… `generation` is the raw text, used only to tell the
// enterprise generations apart.
inline std::string authLabel(Auth a, const std::string& raw = {})
{
    switch (a) {
    case Auth::Open: return "open";
    case Auth::Wep: return "WEP";
    case Auth::Wpa: return "WPA";
    case Auth::Wpa2: return "WPA2";
    case Auth::Wpa3: return "WPA3";
    case Auth::Enterprise: {
        std::string t;
        for (const char c : raw)
            t += static_cast<char>(::toupper(static_cast<unsigned char>(c)));
        if (t.find("WPA3") != std::string::npos || t.find("SAE") != std::string::npos)
            return "WPA3-Enterprise";
        if (t.find("WPA2") != std::string::npos)
            return "WPA2-Enterprise";
        return "Enterprise";
    }
    case Auth::Unknown: break;
    }
    return "?";
}

// ---- XML ---------------------------------------------------------------------------

// The text of the first `<tag>…</tag>` in `xml`, entities decoded; empty when
// the tag is absent or self-closing. Namespace prefixes are ignored, so
// `<name>` matches `<name xmlns="…">` and `<wlan:name>`.
inline std::string xmlDecode(const std::string& s);

inline std::string xmlText(const std::string& xml, const std::string& tag)
{
    const auto isSpace = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
    const auto matches = [&](std::size_t at) {
        // "<tag" followed by '>', whitespace, or '/'
        if (at + 1 + tag.size() > xml.size())
            return false;
        if (xml[at] != '<')
            return false;
        if (xml.compare(at + 1, tag.size(), tag) != 0)
            return false;
        const std::size_t after = at + 1 + tag.size();
        if (after >= xml.size())
            return false;
        const char c = xml[after];
        return c == '>' || isSpace(c) || c == '/';
    };
    for (std::size_t at = xml.find('<'); at != std::string::npos; at = xml.find('<', at + 1)) {
        if (!matches(at))
            continue;
        const std::size_t open = xml.find('>', at);
        if (open == std::string::npos)
            return {};
        if (xml[open - 1] == '/') // <tag/>
            return {};
        const std::size_t close = xml.find("</" + tag, open + 1);
        if (close == std::string::npos)
            return {};
        return xmlDecode(xml.substr(open + 1, close - open - 1));
    }
    return {};
}

// XML entities to UTF-8: the five named ones plus numeric character
// references. A bare '&' that starts nothing valid is kept as it is.
inline void appendUtf8(std::string& out, unsigned cp);

inline std::string xmlDecode(const std::string& s)
{
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '&') {
            out += s[i];
            continue;
        }
        const std::size_t semi = s.find(';', i + 1);
        if (semi == std::string::npos || semi - i > 10) {
            out += s[i];
            continue;
        }
        const std::string name = s.substr(i + 1, semi - i - 1);
        if (name == "amp") out += '&';
        else if (name == "lt") out += '<';
        else if (name == "gt") out += '>';
        else if (name == "quot") out += '"';
        else if (name == "apos") out += '\'';
        else if (name.size() > 1 && name[0] == '#') {
            const bool hex = name[1] == 'x' || name[1] == 'X';
            const std::string digits = name.substr(hex ? 2 : 1);
            if (digits.empty() || digits.find_first_not_of(hex ? "0123456789abcdefABCDEF" : "0123456789")
                    != std::string::npos) {
                out += s[i];
                continue;
            }
            unsigned long cp = 0;
            try {
                cp = std::stoul(digits, nullptr, hex ? 16 : 10);
            } catch (...) {
                out += s[i];
                continue;
            }
            appendUtf8(out, static_cast<unsigned>(cp));
        } else {
            out += s[i];
            continue;
        }
        i = semi;
    }
    return out;
}

inline void appendUtf8(std::string& out, unsigned cp)
{
    if (cp < 0x80)
        out += static_cast<char>(cp);
    else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

// A hex-encoded SSID (`<hex>4D794E6574</hex>`) to its bytes. An odd length or
// a non-hex character yields "".
inline std::string hexToBytes(const std::string& hex)
{
    if (hex.empty() || hex.size() % 2 != 0)
        return {};
    std::string out;
    out.reserve(hex.size() / 2);
    for (std::size_t i = 0; i < hex.size(); i += 2) {
        int value = 0;
        for (int k = 0; k < 2; ++k) {
            const char c = hex[i + k];
            value <<= 4;
            if (c >= '0' && c <= '9') value |= c - '0';
            else if (c >= 'a' && c <= 'f') value |= c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') value |= c - 'A' + 10;
            else return {};
        }
        out += static_cast<char>(value);
    }
    return out;
}

// ---- profile XML -------------------------------------------------------------------

struct Profile {
    std::string name;      // the profile's own name (the first <name>)
    std::string ssid;      // the SSID inside <SSID>, hex-decoded when only <hex> is there
    std::string authText;  // raw <authentication>
    std::string cipherText; // raw <encryption>
    std::string keyMaterial; // plaintext only with WLAN_PROFILE_GET_PLAINTEXT_KEY
    bool oneX = false;
    bool hidden = false;
    bool hasKey = false;   // a <sharedKey> section is present
    bool keyProtected = false;
    Auth auth = Auth::Unknown;
};

inline Profile parseProfile(const std::string& xml)
{
    Profile p;
    p.name = xmlText(xml, "name");
    p.authText = xmlText(xml, "authentication");
    p.cipherText = xmlText(xml, "encryption");
    p.keyMaterial = xmlText(xml, "keyMaterial");
    const std::string oneX = xmlText(xml, "useOneX");
    p.oneX = oneX == "true" || oneX == "1";
    p.hasKey = xml.find("<sharedKey") != std::string::npos;
    const std::string prot = xmlText(xml, "protected");
    p.keyProtected = prot == "true" || prot == "1";

    // The SSID element carries either <name> or <hex>; the first <name> in the
    // document is the profile name, so look inside <SSID> for its own.
    const std::size_t ssidAt = xml.find("<SSID>");
    if (ssidAt != std::string::npos) {
        const std::size_t end = xml.find("</SSID>", ssidAt);
        const std::string block = end == std::string::npos ? xml.substr(ssidAt)
                                                          : xml.substr(ssidAt, end - ssidAt);
        p.ssid = xmlText(block, "name");
        if (p.ssid.empty())
            p.ssid = hexToBytes(xmlText(block, "hex"));
    }
    if (p.ssid.empty())
        p.ssid = p.name;
    p.hidden = xmlText(xml, "nonBroadcast") == "true" || xmlText(xml, "nonBroadcast") == "1";
    p.auth = authFromText(p.authText, p.oneX);
    if (!p.oneX && (p.cipherText == "WEP" || p.cipherText == "WEP40" || p.cipherText == "WEP104"))
        p.auth = Auth::Wep;
    return p;
}

// What a row shows where the password goes: the plaintext when it is known and
// masking is off, asterisks when it is known and masking is on, and "" when it
// is not known at all (the caller then offers the UAC action instead).
inline std::string passwordDisplay(const std::string& password, bool known, bool mask)
{
    if (!known)
        return {};
    if (!mask)
        return password;
    return std::string(8, '*');
}

// ---- signal ------------------------------------------------------------------------

// dBm to 0–100, the Windows WiFi.signalQuality formula: 2 × (dBm + 100),
// clamped. 0 dBm (unknown) stays 0.
inline int qualityFromRssi(int rssi)
{
    if (rssi >= 0)
        return 0;
    int q = 2 * (rssi + 100);
    if (q < 0)
        q = 0;
    if (q > 100)
        q = 100;
    return q;
}

// The band a centre frequency belongs to, in MHz: 2400, 5000, 6000, or 0.
inline unsigned bandFromFrequency(unsigned mhz)
{
    if (mhz >= 2400 && mhz <= 2500)
        return 2400;
    if (mhz >= 4900 && mhz <= 5900)
        return 5000;
    if (mhz >= 5925 && mhz <= 7125)
        return 6000;
    return 0;
}

// "2.4 GHz" / "5 GHz" / "6 GHz", or "" when the band is unknown.
inline std::string bandText(unsigned band)
{
    if (band == 2400)
        return "2.4 GHz";
    if (band == 5000)
        return "5 GHz";
    if (band == 6000)
        return "6 GHz";
    return {};
}

// The channel a centre frequency sits on: 14 for 2484, (f−2407)/5 on 2.4 GHz,
// (f−5000)/5 on 5 GHz, (f−5950)/5 on 6 GHz. 0 when it does not line up.
inline unsigned channelFromFrequency(unsigned mhz)
{
    if (mhz == 2484)
        return 14;
    if (mhz >= 2412 && mhz <= 2472)
        return (mhz - 2407) / 5;
    if (mhz >= 5000 && mhz <= 5900)
        return (mhz - 5000) / 5;
    if (mhz >= 5955 && mhz <= 7125)
        return (mhz - 5950) / 5;
    return 0;
}

// "866 Mbps" from kbps; "" when unknown.
inline std::string rateText(unsigned long long kbps)
{
    if (kbps == 0)
        return {};
    if (kbps < 1000)
        return std::to_string(kbps) + " kbps";
    const unsigned long long mbps = (kbps + 500) / 1000;
    return std::to_string(mbps) + " Mbps";
}

// ---- networks ----------------------------------------------------------------------

// One network as the rows see it: a saved profile, a scan result, or both
// merged (the same SSID seen in both is one network).
struct Network {
    std::string ssid;
    std::string profileName; // WLAN profile identity, which can differ from the SSID
    std::string bssid;      // "aa:bb:cc:dd:ee:ff"; empty for a profile with no scan hit
    std::string interfaceName;
    bool connected = false;
    bool saved = false;     // a profile exists
    bool visible = false;   // in the scan list
    bool hidden = false;
    Auth auth = Auth::Unknown;
    std::string authText;   // raw, for the label
    std::string cipherText;
    int rssi = 0;           // dBm; 0 when unknown
    unsigned frequency = 0; // MHz
    unsigned long long rxKbps = 0;
    unsigned long long txKbps = 0;
    std::string password;   // only after a successful reveal
    bool passwordKnown = false;
    bool radioOff = false;  // the adapter's radio is switched off
};

inline int quality(const Network& n) { return qualityFromRssi(n.rssi); }

// Case-insensitive substring match on the SSID. An empty filter matches
// everything.
inline bool matchesFilter(const std::string& ssid, const std::string& filter)
{
    if (filter.empty())
        return true;
    const auto lower = [](const std::string& s) {
        std::string out;
        out.reserve(s.size());
        for (const char c : s)
            out += static_cast<char>(::tolower(static_cast<unsigned char>(c)));
        return out;
    };
    return lower(ssid).find(lower(filter)) != std::string::npos;
}

// The sort key: connected first, then saved-but-not-connected, then visible
// only; inside a group the stronger signal wins. 0 dBm means "no reading",
// so it sorts below every real signal rather than above them.
inline int rankOf(const Network& n)
{
    if (n.connected)
        return 0;
    if (n.saved)
        return 1;
    return 2;
}

// A comparable signal: an unknown reading (0) is weaker than any real one.
inline int signalKey(int rssi) { return rssi == 0 ? -1000 : rssi; }

inline bool strongerFirst(const Network& a, const Network& b)
{
    if (rankOf(a) != rankOf(b))
        return rankOf(a) < rankOf(b);
    if (signalKey(a.rssi) != signalKey(b.rssi))
        return signalKey(a.rssi) > signalKey(b.rssi);
    return a.ssid < b.ssid;
}

// Merge the same SSID seen more than once (several BSSIDs, or a profile plus a
// scan hit) into one row: the strongest signal wins, and the flags are the
// union. A connected entry always keeps its own BSSID and rates.
inline std::vector<Network> mergeBySsid(const std::vector<Network>& in)
{
    std::vector<Network> out;
    std::map<std::string, std::size_t> index; // ssid → position in `out`
    for (const Network& n : in) {
        const auto it = index.find(n.ssid);
        if (it == index.end()) {
            index[n.ssid] = out.size();
            out.push_back(n);
            continue;
        }
        Network& kept = out[it->second];
        if (!kept.saved && n.saved) {
            kept.profileName = n.profileName;
            kept.interfaceName = n.interfaceName;
        } else if (kept.profileName.empty() && n.saved && kept.interfaceName == n.interfaceName) {
            kept.profileName = n.profileName;
        }
        kept.saved = kept.saved || n.saved;
        kept.visible = kept.visible || n.visible;
        kept.hidden = kept.hidden || n.hidden;
        if (n.connected) { // a connected hit is the one to keep in full
            const bool wasConnected = kept.connected;
            Network merged = n;
            if (merged.profileName.empty() && kept.interfaceName == n.interfaceName)
                merged.profileName = kept.profileName;
            merged.saved = kept.saved || n.saved;
            merged.visible = kept.visible || n.visible;
            merged.hidden = kept.hidden || n.hidden;
            merged.password = kept.passwordKnown ? kept.password : n.password;
            merged.passwordKnown = kept.passwordKnown || n.passwordKnown;
            if (wasConnected && kept.bssid != n.bssid)
                merged.bssid = kept.bssid;
            kept = merged;
            continue;
        }
        if (kept.connected)
            continue; // the connected entry keeps its signal and BSSID
        if (signalKey(n.rssi) > signalKey(kept.rssi)) {
            kept.rssi = n.rssi;
            kept.bssid = n.bssid;
            kept.frequency = n.frequency;
            kept.rxKbps = n.rxKbps;
            kept.txKbps = n.txKbps;
        }
        if (kept.auth == Auth::Unknown && n.auth != Auth::Unknown) {
            kept.auth = n.auth;
            kept.authText = n.authText;
            kept.cipherText = n.cipherText;
        }
        if (kept.passwordKnown == false && n.passwordKnown) {
            kept.password = n.password;
            kept.passwordKnown = true;
        }
    }
    return out;
}

// The rows for one query: hidden networks dropped unless the setting keeps
// them, the filter applied, duplicates merged, then ordered.
inline std::vector<Network> buildRows(const std::vector<Network>& in, const Settings& st,
    const std::string& filter)
{
    std::vector<Network> kept;
    kept.reserve(in.size());
    for (const Network& n : in) {
        if (n.hidden && !st.includeHidden)
            continue;
        if (!matchesFilter(n.ssid, filter))
            continue;
        kept.push_back(n);
    }
    std::vector<Network> rows = mergeBySsid(kept);
    std::stable_sort(rows.begin(), rows.end(), strongerFirst);
    return rows;
}

// ---- the confirmation step ---------------------------------------------------------

// The SSID a confirmation query is about: "forget <ssid>" → "<ssid>", else "".
// The keyword itself is not part of the query text (the host strips it), so
// the words after it are all this sees.
//
// The step is carried in the query text rather than in plugin state because
// that is what gives the user a way back: the launcher's Esc clears the words
// after the keyword and re-asks the plain list. A `replace` list has no such
// path (spec §4.2).
//
// Matched case-sensitively, so an SSID searched as "Forget HJJ" stays a
// search. A network whose own name begins with "forget " is still reachable:
// the confirm query matches a saved profile by exact name, so
// "forget forget me" finds the network called "forget me".
inline std::string forgetTarget(const std::string& text)
{
    static const std::string kPrefix = "forget ";
    if (text.size() <= kPrefix.size() || text.compare(0, kPrefix.size(), kPrefix) != 0)
        return {};
    return winshun::trim(text.substr(kPrefix.size()));
}

// The query text that opens the confirmation step for `ssid`.
inline std::string forgetQuery(const std::string& ssid) { return "forget " + ssid; }

// ---- hints -------------------------------------------------------------------------

// Which single hint row a query shows instead of networks, if any.
enum class Hint { None, NoAdapter, ServiceDown, NoResults };

inline Hint hintFor(bool noAdapter, bool serviceDown, std::size_t rowCount)
{
    if (noAdapter)
        return Hint::NoAdapter;
    if (serviceDown)
        return Hint::ServiceDown;
    if (rowCount == 0)
        return Hint::NoResults;
    return Hint::None;
}

inline std::string hintTitle(Hint h, bool zh)
{
    switch (h) {
    case Hint::NoAdapter:
        return zh ? "没有无线网卡" : "No wireless adapter";
    case Hint::ServiceDown:
        return zh ? "WLAN AutoConfig 服务未运行" : "The WLAN AutoConfig service is not running";
    case Hint::NoResults:
        return zh ? "没有匹配的网络" : "No matching networks";
    case Hint::None:
        break;
    }
    return {};
}

inline std::string hintSubtitle(Hint h, bool zh)
{
    switch (h) {
    case Hint::NoAdapter:
        return zh ? "插上无线网卡或启用 WLAN 电台" : "Plug in an adapter or turn the WLAN radio on";
    case Hint::ServiceDown:
        return zh ? "在服务里启动 WlanSvc" : "Start WlanSvc in Services";
    case Hint::NoResults:
        return zh ? "换个 SSID 片段试试" : "Try another part of the SSID";
    case Hint::None:
        break;
    }
    return {};
}

// ---- row text ----------------------------------------------------------------------

// The second line of a row: band, channel, signal, rate, auth, and the
// connected/saved/visible state. Missing pieces are left out rather than
// shown as zero.
inline std::string rowSubtitle(const Network& n, bool zh, bool mask)
{
    std::vector<std::string> parts;
    const std::string band = bandText(bandFromFrequency(n.frequency));
    const unsigned channel = channelFromFrequency(n.frequency);
    if (!band.empty()) {
        std::string b = band;
        if (channel)
            b += (zh ? " 信道 " : " ch ") + std::to_string(channel);
        parts.push_back(b);
    }
    if (n.rssi != 0)
        parts.push_back(std::to_string(quality(n)) + "%");
    const std::string rate = rateText(n.rxKbps);
    if (!rate.empty())
        parts.push_back(rate);
    if (n.auth != Auth::Unknown)
        parts.push_back(authLabel(n.auth, n.authText));
    if (n.connected)
        parts.push_back(zh ? "已连接" : "connected");
    else if (n.saved)
        parts.push_back(zh ? "已保存" : "saved");
    if (!n.visible && n.saved && !n.connected)
        parts.push_back(zh ? "不在范围内" : "out of range");
    if (n.radioOff)
        parts.push_back(zh ? "WLAN 电台已关闭" : "WLAN radio off");
    if (n.passwordKnown) {
        const std::string shown = passwordDisplay(n.password, true, mask);
        parts.push_back((zh ? "密码 " : "key ") + shown);
    }

    std::string out;
    for (const std::string& p : parts) {
        if (!out.empty())
            out += " · ";
        out += p;
    }
    return out;
}

// The title of a row: the SSID, or a placeholder for a hidden one.
inline std::string rowTitle(const Network& n, bool zh)
{
    if (!n.ssid.empty())
        return n.ssid;
    return zh ? "（隐藏网络）" : "(hidden network)";
}

// The stable id of a row: the SSID, which is what the actions address. A
// hidden network has no SSID to name, so its BSSID stands in.
inline std::string rowId(const Network& n)
{
    return "wifi:" + (n.ssid.empty() ? n.bssid : n.ssid);
}

// Whether Enter on this row can connect: a saved profile, or a visible open
// network. A visible encrypted network with no profile needs a password the
// plugin does not ask for.
inline bool canConnect(const Network& n)
{
    if (n.connected)
        return false;
    if (n.saved)
        return true;
    return n.visible && isOpen(n.auth);
}

// The line shown when Enter cannot connect.
inline std::string connectRefusal(const Network& n, bool zh)
{
    if (n.connected)
        return zh ? "已经连上了" : "Already connected";
    if (n.visible && !n.saved && !isOpen(n.auth))
        return zh ? "请在系统设置输入密码" : "Enter the password in Windows Settings";
    return zh ? "这个网络现在连不上" : "This network cannot be connected now";
}

} // namespace wifiname
