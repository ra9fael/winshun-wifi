// Native QR actions and the shared preview window.
#pragma once
#include "wifi_qr.h"
#include "qr_preview_window.h"

namespace wifiname {
using qrpreview::focusQrWindow;
inline qrpreview::Bitmap previewBitmap(const QrBitmap& image)
{
    return {image.size, image.pixels, image.size / 8};
}
inline bool copyQrImage(const QrBitmap& image) { return qrpreview::copyQrImage(previewBitmap(image)); }
inline bool saveQrImage(const QrBitmap& image, const std::wstring& path)
{
    return qrpreview::saveQrImage(previewBitmap(image), path);
}
inline std::wstring saveQrInPictures(const QrBitmap& image)
{
    return qrpreview::saveQrInPictures(previewBitmap(image));
}
class QrPreview {
public:
    bool show(QrBitmap image, const Profile& profile, bool zh)
    {
        const std::string detail = profile.auth == Auth::Open
            ? (zh ? "无需密码" : "No password required")
            : (zh ? "密码：" : "Password: ") + profile.keyMaterial;
        return m_preview.show(previewBitmap(image), profile.ssid, zh, L"Wi-Fi QR", L"wifi-qr", detail);
    }
    void close() { m_preview.close(); }
    HWND window() const { return m_preview.window(); }
private:
    qrpreview::QrPreview m_preview;
};
} // namespace wifiname
