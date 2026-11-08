// native.h — ra9fael.wifi's Win32 side: the wlanapi calls, the SSID/IE
// decoding that only a live scan can supply, and the clipboard write.
// Everything here needs a real adapter; logic.h holds the rules that do not.
//
// English code/comments per project convention; user-facing strings follow
// the host locale, chosen by the caller.
//
// Elevation: WlanGetProfile returns the plaintext key only to an
// administrator (WLAN_PROFILE_GET_PLAINTEXT_KEY). This plugin does NOT
// self-elevate. Win顺's `elevated` permission means the plugin runs as
// administrator exactly when Win顺 does, and the host deliberately never
// starts plugin-folder code elevated on its own, because any program with
// normal rights could swap the exe first. A plugin that ran
// ShellExecuteExW("runas") on its own exe would recreate that hole. So the
// password read works when Win顺 is elevated and reports honestly when it is
// not; see docs/spec.md §4.

#pragma once

#include "logic.h"

#include <winsock2.h>
#include <windows.h>
#include <wlanapi.h>

#include <string>
#include <vector>

namespace wifiname {

// ---- elevation ---------------------------------------------------------------------

inline bool isElevated()
{
    HANDLE token = nullptr;
    if (!::OpenProcessToken(::GetCurrentProcess(), TOKEN_QUERY, &token))
        return false;
    TOKEN_ELEVATION elevation {};
    DWORD size = sizeof elevation;
    const bool ok = ::GetTokenInformation(token, TokenElevation, &elevation, size, &size)
        && elevation.TokenIsElevated != 0;
    ::CloseHandle(token);
    return ok;
}

// ---- the wlanapi handle ------------------------------------------------------------

// One WlanOpenHandle for the length of one query or action, closed after
// (spec §5: a resident plugin does not sit on the handle).
class WlanSession {
public:
    WlanSession() = default;
    ~WlanSession() { close(); }
    WlanSession(const WlanSession&) = delete;
    WlanSession& operator=(const WlanSession&) = delete;

    // ERROR_SUCCESS, or the code WlanOpenHandle gave.
    DWORD open()
    {
        DWORD negotiated = 0;
        return ::WlanOpenHandle(2, nullptr, &negotiated, &m_handle);
    }
    void close()
    {
        if (m_handle) {
            ::WlanCloseHandle(m_handle, nullptr);
            m_handle = nullptr;
        }
    }
    HANDLE handle() const { return m_handle; }
    explicit operator bool() const { return m_handle != nullptr; }

private:
    HANDLE m_handle = nullptr;
};

// WlanOpenHandle's failure when the WLAN AutoConfig service is not running.
inline bool isServiceDown(DWORD error)
{
    return error == ERROR_SERVICE_NOT_ACTIVE || error == ERROR_SERVICE_DISABLED
        || error == ERROR_INVALID_HANDLE;
}

// ---- small conversions -------------------------------------------------------------

inline std::string ssidText(const DOT11_SSID& ssid)
{
    std::size_t n = ssid.uSSIDLength;
    if (n > DOT11_SSID_MAX_LENGTH)
        n = DOT11_SSID_MAX_LENGTH;
    std::string out(reinterpret_cast<const char*>(ssid.ucSSID), n);
    while (!out.empty() && out.back() == '\0') // some drivers pad with NULs
        out.pop_back();
    return out;
}

inline std::string bssidText(const UCHAR bssid[6])
{
    char buffer[32];
    std::snprintf(buffer, sizeof buffer, "%02x:%02x:%02x:%02x:%02x:%02x", bssid[0], bssid[1], bssid[2],
        bssid[3], bssid[4], bssid[5]);
    return buffer;
}

// The band and channel of a centre frequency, in the form the rows show.
inline void setFrequency(Network& n, unsigned long mhz)
{
    n.frequency = static_cast<unsigned>(mhz);
}

// ---- security from the information elements ----------------------------------------

// A scan result carries no authentication name, only the capability's privacy
// bit and the raw IEs. RSN (48) and the Microsoft WPA vendor IE (221) say
// which generation; the AKM suite type 8 is SAE, which is WPA3. A privacy bit
// with neither IE is WEP (spec §3's `WEP` label).
inline void applySecurityIes(Network& n, const UCHAR* ie, unsigned long size, bool privacyBit)
{
    bool sawRsn = false;
    bool sawWpa = false;
    bool sawSae = false;
    bool sawPsk = false;
    bool sawOneX = false;

    unsigned long at = 0;
    while (ie && at + 2 <= size) {
        const unsigned char id = ie[at];
        const unsigned long len = ie[at + 1];
        if (at + 2 + len > size)
            break;
        const UCHAR* body = ie + at + 2;

        if (id == 48 && len >= 2) { // RSN
            sawRsn = true;
            // version(2) group cipher(4) pairwise count(2) pairwise(4n)
            // akm count(2) akm(4n)
            unsigned long p = 2 + 4;
            if (p + 2 <= len) {
                const unsigned pairCount = (body[p] | (body[p + 1] << 8));
                p += 2 + 4ul * pairCount;
                if (p + 2 <= len) {
                    const unsigned akmCount = (body[p] | (body[p + 1] << 8));
                    p += 2;
                    for (unsigned i = 0; i < akmCount && p + 4 <= len; ++i, p += 4) {
                        // 00-0F-AC:<type>
                        if (body[p] == 0x00 && body[p + 1] == 0x0F && body[p + 2] == 0xAC) {
                            const unsigned char type = body[p + 3];
                            if (type == 8 || type == 9 || type == 11)
                                sawSae = true;
                            else if (type == 2 || type == 4 || type == 6)
                                sawPsk = true;
                            else if (type == 1 || type == 3 || type == 5)
                                sawOneX = true;
                        }
                    }
                }
            }
        } else if (id == 221 && len >= 4 && body[0] == 0x00 && body[1] == 0x50 && body[2] == 0xF2
            && body[3] == 0x01) {
            sawWpa = true;
        }
        at += 2 + len;
    }

    if (sawRsn) {
        if (sawSae) {
            n.auth = Auth::Wpa3;
            n.authText = sawOneX ? "WPA3ENT" : "WPA3SAE";
        } else if (sawOneX) {
            n.auth = Auth::Enterprise;
            n.authText = "WPA2ENT";
        } else {
            n.auth = Auth::Wpa2;
            n.authText = "WPA2PSK";
        }
    } else if (sawWpa) {
        n.auth = sawOneX ? Auth::Enterprise : Auth::Wpa;
        n.authText = sawOneX ? "WPAENT" : "WPAPSK";
    } else if (privacyBit) {
        n.auth = Auth::Wep;
        n.authText = "WEP";
    } else {
        n.auth = Auth::Open;
        n.authText = "open";
    }
    (void)sawPsk;
}

// ---- one interface -----------------------------------------------------------------

struct InterfaceInfo {
    GUID guid {};
    std::string name; // the adapter's description, as the rows show it
    bool connected = false;
    bool radioOff = false; // software or hardware radio switch is off
};

inline std::vector<InterfaceInfo> listInterfaces(HANDLE client)
{
    std::vector<InterfaceInfo> out;
    PWLAN_INTERFACE_INFO_LIST list = nullptr;
    if (::WlanEnumInterfaces(client, nullptr, &list) != ERROR_SUCCESS || !list)
        return out;
    for (DWORD i = 0; i < list->dwNumberOfItems; ++i) {
        const WLAN_INTERFACE_INFO& info = list->InterfaceInfo[i];
        InterfaceInfo one;
        one.guid = info.InterfaceGuid;
        one.name = winshun::utf8(std::wstring(info.strInterfaceDescription));
        one.connected = info.isState == wlan_interface_state_connected;
        // The radio state is a separate query; a failure leaves it "on" so a
        // row is never wrongly labelled as switched off.
        DWORD size = 0;
        WLAN_OPCODE_VALUE_TYPE type = wlan_opcode_value_type_invalid;
        PWLAN_RADIO_STATE state = nullptr;
        if (::WlanQueryInterface(client, &one.guid, wlan_intf_opcode_radio_state, nullptr, &size,
                reinterpret_cast<PVOID*>(&state), &type)
                == ERROR_SUCCESS
            && state) {
            // The state is per PHY; the adapter is off when any PHY's radio is.
            for (DWORD phy = 0; phy < state->dwNumberOfPhys && phy < WLAN_MAX_PHY_INDEX; ++phy) {
                const WLAN_PHY_RADIO_STATE& radio = state->PhyRadioState[phy];
                if (radio.dot11SoftwareRadioState == dot11_radio_state_off
                    || radio.dot11HardwareRadioState == dot11_radio_state_off)
                    one.radioOff = true;
            }
            ::WlanFreeMemory(state);
        }
        out.push_back(std::move(one));
    }
    ::WlanFreeMemory(list);
    return out;
}

// The interface's current connection, as a Network with connected and saved
// set. `ok` is false when the interface is not connected.
inline bool currentConnection(HANDLE client, const InterfaceInfo& adapter, Network& out)
{
    DWORD size = 0;
    WLAN_OPCODE_VALUE_TYPE type = wlan_opcode_value_type_invalid;
    PWLAN_CONNECTION_ATTRIBUTES attrs = nullptr;
    const DWORD rc = ::WlanQueryInterface(client, &adapter.guid, wlan_intf_opcode_current_connection, nullptr,
        &size, reinterpret_cast<PVOID*>(&attrs), &type);
    if (rc != ERROR_SUCCESS || !attrs)
        return false;
    const bool connected = attrs->isState == wlan_interface_state_connected;
    if (connected) {
        out = Network {};
        out.ssid = ssidText(attrs->wlanAssociationAttributes.dot11Ssid);
        out.bssid = bssidText(attrs->wlanAssociationAttributes.dot11Bssid);
        out.interfaceName = adapter.name;
        out.connected = true;
        out.saved = true;
        out.visible = true;
        // wlanSignalQuality is 0-100; the rows carry dBm, so invert the
        // WiFi.signalQuality formula the rest of the plugin uses.
        const int quality = static_cast<int>(attrs->wlanAssociationAttributes.wlanSignalQuality);
        out.rssi = quality > 0 ? quality / 2 - 100 : 0;
        out.rxKbps = attrs->wlanAssociationAttributes.ulRxRate;
        out.txKbps = attrs->wlanAssociationAttributes.ulTxRate;
        const DOT11_AUTH_ALGORITHM auth = attrs->wlanSecurityAttributes.dot11AuthAlgorithm;
        const bool oneX = attrs->wlanSecurityAttributes.bOneXEnabled != 0;
        switch (auth) {
        case DOT11_AUTH_ALGO_80211_OPEN:
            out.auth = Auth::Open;
            out.authText = "open";
            break;
        case DOT11_AUTH_ALGO_80211_SHARED_KEY:
            out.auth = Auth::Wep;
            out.authText = "WEP";
            break;
        case DOT11_AUTH_ALGO_WPA:
            out.auth = oneX ? Auth::Enterprise : Auth::Wpa;
            out.authText = oneX ? "WPAENT" : "WPAPSK";
            break;
        case DOT11_AUTH_ALGO_WPA_PSK:
            out.auth = Auth::Wpa;
            out.authText = "WPAPSK";
            break;
        case DOT11_AUTH_ALGO_WPA_NONE:
            out.auth = Auth::Wpa;
            out.authText = "WPA";
            break;
        case DOT11_AUTH_ALGO_RSNA:
            out.auth = oneX ? Auth::Enterprise : Auth::Wpa2;
            out.authText = oneX ? "WPA2ENT" : "WPA2PSK";
            break;
        case DOT11_AUTH_ALGO_RSNA_PSK:
            out.auth = Auth::Wpa2;
            out.authText = "WPA2PSK";
            break;
        default:
            out.auth = Auth::Unknown;
            break;
        }
    }
    ::WlanFreeMemory(attrs);
    return connected;
}

// One saved profile: its name, whether group policy owns it, and its XML.
struct SavedProfile {
    std::string name;
    bool groupPolicy = false;
};

inline std::vector<SavedProfile> listProfiles(HANDLE client, const GUID& guid)
{
    std::vector<SavedProfile> out;
    PWLAN_PROFILE_INFO_LIST list = nullptr;
    if (::WlanGetProfileList(client, &guid, nullptr, &list) != ERROR_SUCCESS || !list)
        return out;
    for (DWORD i = 0; i < list->dwNumberOfItems; ++i) {
        SavedProfile one;
        one.name = winshun::utf8(std::wstring(list->ProfileInfo[i].strProfileName));
        one.groupPolicy = (list->ProfileInfo[i].dwFlags & WLAN_PROFILE_GROUP_POLICY) != 0;
        out.push_back(std::move(one));
    }
    ::WlanFreeMemory(list);
    return out;
}

// A profile's XML. `plaintext` asks for the key material in the clear, which
// only an administrator gets; without it the keyMaterial stays the encrypted
// blob and the caller says so.
inline bool profileXml(HANDLE client, const GUID& guid, const std::string& name, bool plaintext,
    std::string& xml, DWORD* error = nullptr)
{
    DWORD flags = plaintext ? WLAN_PROFILE_GET_PLAINTEXT_KEY : 0;
    DWORD granted = 0;
    LPWSTR text = nullptr;
    const DWORD rc = ::WlanGetProfile(client, &guid, winshun::wide(name).c_str(), nullptr, &text, &flags, &granted);
    if (error)
        *error = rc;
    if (rc != ERROR_SUCCESS || !text)
        return false;
    xml = winshun::utf8(std::wstring(text));
    ::WlanFreeMemory(text);
    return true;
}

// The scan list for one interface: visible networks with signal, band and
// the security the IEs carry.
inline std::vector<Network> scanList(HANDLE client, const InterfaceInfo& adapter)
{
    std::vector<Network> out;
    PWLAN_BSS_LIST list = nullptr;
    if (::WlanGetNetworkBssList(client, &adapter.guid, nullptr, dot11_BSS_type_any, FALSE, nullptr, &list)
            != ERROR_SUCCESS
        || !list)
        return out;
    for (DWORD i = 0; i < list->dwNumberOfItems; ++i) {
        const WLAN_BSS_ENTRY& entry = list->wlanBssEntries[i];
        Network n;
        n.ssid = ssidText(entry.dot11Ssid);
        n.bssid = bssidText(entry.dot11Bssid);
        n.interfaceName = adapter.name;
        n.visible = true;
        n.hidden = n.ssid.empty();
        n.rssi = static_cast<int>(entry.lRssi);
        setFrequency(n, entry.ulChCenterFrequency);
        // A scan entry carries no link rate (WLAN_BSS_ENTRY has no ulRxRate);
        // only the current association reports one, so a visible-only row
        // shows no rate rather than a made-up one.
        const bool privacy = (entry.usCapabilityInformation & 0x0010) != 0; // ESS privacy bit
        const UCHAR* ie = entry.ulIeSize > 0 ? reinterpret_cast<const UCHAR*>(&entry) + entry.ulIeOffset : nullptr;
        applySecurityIes(n, ie, entry.ulIeSize, privacy);
        out.push_back(std::move(n));
    }
    ::WlanFreeMemory(list);
    return out;
}

// ---- the whole picture -------------------------------------------------------------

struct Snapshot {
    bool noAdapter = false;
    bool serviceDown = false;
    std::string problem; // a wlanapi error that is neither of the two above
    std::vector<Network> networks;
};

// Ask for a fresh scan, at most once every `throttleMs`, so a query per
// keystroke does not hammer the radio. The cached list is read either way.
inline void maybeScan(HANDLE client, const GUID& guid, unsigned throttleMs = 10000)
{
    static ULONGLONG last = 0;
    const ULONGLONG now = ::GetTickCount64();
    if (last != 0 && now - last < throttleMs)
        return;
    last = now;
    ::WlanScan(client, &guid, nullptr, nullptr, nullptr);
}

inline Snapshot collect(bool wantScan)
{
    Snapshot snap;
    WlanSession session;
    const DWORD opened = session.open();
    if (opened != ERROR_SUCCESS) {
        snap.serviceDown = isServiceDown(opened);
        if (!snap.serviceDown)
            snap.problem = "WlanOpenHandle error " + std::to_string(opened);
        return snap;
    }

    const std::vector<InterfaceInfo> adapters = listInterfaces(session.handle());
    if (adapters.empty()) {
        snap.noAdapter = true;
        return snap;
    }

    for (const InterfaceInfo& adapter : adapters) {
        Network connected;
        if (currentConnection(session.handle(), adapter, connected))
            snap.networks.push_back(connected);

        for (const SavedProfile& profile : listProfiles(session.handle(), adapter.guid)) {
            Network n;
            n.saved = true;
            n.interfaceName = adapter.name;
            n.radioOff = adapter.radioOff;
            std::string xml;
            if (profileXml(session.handle(), adapter.guid, profile.name, false, xml)) {
                const Profile parsed = parseProfile(xml);
                n.ssid = parsed.ssid;
                n.auth = parsed.auth;
                n.authText = parsed.authText;
                n.cipherText = parsed.cipherText;
                n.passwordKnown = !parsed.keyMaterial.empty() && !parsed.keyProtected;
                if (n.passwordKnown)
                    n.password = parsed.keyMaterial;
            }
            if (n.ssid.empty())
                n.ssid = profile.name;
            snap.networks.push_back(std::move(n));
        }

        if (wantScan)
            maybeScan(session.handle(), adapter.guid);
        for (Network& n : scanList(session.handle(), adapter)) {
            // A saved profile already knows its own auth; keep that one.
            snap.networks.push_back(std::move(n));
        }
    }
    return snap;
}

// ---- actions -----------------------------------------------------------------------

struct ActionResult {
    bool ok = false;
    bool denied = false; // the profile's key needs an administrator
    std::string problem;
};

// WlanConnect to a saved profile.
inline ActionResult connectProfile(HANDLE client, const GUID& guid, const std::string& profile)
{
    ActionResult r;
    const std::wstring name = winshun::wide(profile);
    WLAN_CONNECTION_PARAMETERS params {};
    params.wlanConnectionMode = wlan_connection_mode_profile;
    params.strProfile = name.c_str();
    params.dot11BssType = dot11_BSS_type_any;
    const DWORD rc = ::WlanConnect(client, &guid, &params, nullptr);
    r.ok = rc == ERROR_SUCCESS;
    if (!r.ok)
        r.problem = "WlanConnect error " + std::to_string(rc);
    return r;
}

// WlanConnect to a visible open network with no profile: an unsecure
// discovery connection, which Windows allows without a stored profile.
inline ActionResult connectOpen(HANDLE client, const GUID& guid, const std::string& ssid)
{
    ActionResult r;
    DOT11_SSID target {};
    target.uSSIDLength = static_cast<ULONG>(std::min<std::size_t>(ssid.size(), DOT11_SSID_MAX_LENGTH));
    std::memcpy(target.ucSSID, ssid.data(), target.uSSIDLength);
    WLAN_CONNECTION_PARAMETERS params {};
    params.wlanConnectionMode = wlan_connection_mode_discovery_unsecure;
    params.pDot11Ssid = &target;
    params.dot11BssType = dot11_BSS_type_any;
    const DWORD rc = ::WlanConnect(client, &guid, &params, nullptr);
    r.ok = rc == ERROR_SUCCESS;
    if (!r.ok)
        r.problem = "WlanConnect error " + std::to_string(rc);
    return r;
}

inline ActionResult disconnect(HANDLE client, const GUID& guid)
{
    ActionResult r;
    const DWORD rc = ::WlanDisconnect(client, &guid, nullptr);
    r.ok = rc == ERROR_SUCCESS;
    if (!r.ok)
        r.problem = "WlanDisconnect error " + std::to_string(rc);
    return r;
}

inline ActionResult deleteProfile(HANDLE client, const GUID& guid, const std::string& profile)
{
    ActionResult r;
    const DWORD rc = ::WlanDeleteProfile(client, &guid, winshun::wide(profile).c_str(), nullptr);
    r.ok = rc == ERROR_SUCCESS;
    if (!r.ok)
        r.problem = "WlanDeleteProfile error " + std::to_string(rc);
    return r;
}

// The interface a network lives on, by its description; the first adapter
// when the name is unknown (a single-adapter machine, the common case).
inline bool findInterface(HANDLE client, const std::string& name, GUID& out)
{
    const std::vector<InterfaceInfo> adapters = listInterfaces(client);
    if (adapters.empty())
        return false;
    for (const InterfaceInfo& adapter : adapters) {
        if (adapter.name == name) {
            out = adapter.guid;
            return true;
        }
    }
    out = adapters.front().guid;
    return true;
}

// ---- clipboard ---------------------------------------------------------------------

inline bool copyText(const std::string& text)
{
    const std::wstring wide = winshun::wide(text);
    if (!::OpenClipboard(nullptr))
        return false;
    ::EmptyClipboard();
    const std::size_t bytes = (wide.size() + 1) * sizeof(wchar_t);
    HGLOBAL memory = ::GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (!memory) {
        ::CloseClipboard();
        return false;
    }
    if (void* target = ::GlobalLock(memory)) {
        std::memcpy(target, wide.c_str(), bytes);
        ::GlobalUnlock(memory);
    }
    // SetClipboardData takes ownership on success; on failure it stays ours.
    if (!::SetClipboardData(CF_UNICODETEXT, memory))
        ::GlobalFree(memory);
    ::CloseClipboard();
    return true;
}

} // namespace wifiname
