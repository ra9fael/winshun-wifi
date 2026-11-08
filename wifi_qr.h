// Wi-Fi sharing rules and image rendering, independent of the WLAN APIs.
#pragma once

#include "logic.h"
#include "qrcode.h"

namespace wifiname {

enum class QrStatus { Ready, Enterprise, NeedsKey, Unsupported, NoSsid };

struct WifiQr {
    QrStatus status = QrStatus::Unsupported;
    std::string payload;
};

inline std::string escapeWifi(const std::string& text)
{
    std::string out;
    for (char c : text) {
        if (c == '\\' || c == ';' || c == ',' || c == ':' || c == '"')
            out += '\\';
        out += c;
    }
    return out;
}

inline WifiQr makeWifiQr(const Profile& profile)
{
    if (profile.ssid.empty())
        return {QrStatus::NoSsid, {}};
    if (profile.oneX || profile.auth == Auth::Enterprise)
        return {QrStatus::Enterprise, {}};
    const char* type = nullptr;
    switch (profile.auth) {
    case Auth::Open: type = "nopass"; break;
    case Auth::Wep: type = "WEP"; break;
    case Auth::Wpa:
    case Auth::Wpa2:
    case Auth::Wpa3: type = "WPA"; break;
    default: return {QrStatus::Unsupported, {}};
    }
    if (profile.auth != Auth::Open && (profile.keyProtected || profile.keyMaterial.empty()))
        return {QrStatus::NeedsKey, {}};
    // A malformed/unrecognized encrypted profile must not become an open QR.
    if (profile.auth == Auth::Open && (profile.hasKey || profile.cipherText != "none"))
        return {QrStatus::Unsupported, {}};
    std::string payload = "WIFI:T:" + std::string(type) + ";S:" + escapeWifi(profile.ssid) + ";";
    if (profile.auth != Auth::Open)
        payload += "P:" + escapeWifi(profile.keyMaterial) + ";";
    if (profile.hidden)
        payload += "H:true;";
    return {QrStatus::Ready, payload + ";"};
}

inline std::string qrProblem(QrStatus status, bool zh)
{
    switch (status) {
    case QrStatus::Enterprise:
        return zh ? "企业网络（802.1X）不支持共享密码二维码" : "Enterprise (802.1X) networks cannot share a password QR code";
    case QrStatus::NeedsKey:
        return zh ? "无法读取明文密码，请以管理员身份运行 Win顺后重试" : "Could not read the plaintext key; run WinShun as administrator and retry";
    case QrStatus::NoSsid:
        return zh ? "配置没有有效的 SSID" : "The profile has no valid SSID";
    default:
        return zh ? "此网络的认证方式不支持二维码分享" : "This network's authentication cannot be shared as a QR code";
    }
}

struct QrBitmap {
    int size = 0;
    std::vector<uint32_t> pixels; // top-down, opaque BGRA
};

inline QrBitmap renderWifiQr(const std::string& payload)
{
    const auto code = ra9fael::qr::encode(payload, ra9fael::qr::Ecc::M);
    if (!code.ok)
        return {};
    constexpr int scale = 8, border = 4;
    QrBitmap image;
    image.size = (code.size + 2 * border) * scale;
    image.pixels.assign(static_cast<size_t>(image.size) * image.size, 0xffffffffu);
    for (int y = 0; y < code.size; ++y)
        for (int x = 0; x < code.size; ++x) {
            if (code.rows[static_cast<size_t>(y) * (code.size + 1) + x] != '1')
                continue;
            for (int dy = 0; dy < scale; ++dy) {
                auto first = image.pixels.begin() + static_cast<size_t>((y + border) * scale + dy) * image.size + (x + border) * scale;
                std::fill(first, first + scale, 0xff000000u);
            }
        }
    return image;
}

} // namespace wifiname
