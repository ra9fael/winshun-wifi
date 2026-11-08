// Native clipboard, PNG and focused preview. No Qt or QR-plugin dependency.
#pragma once

#include "wifi_qr.h"
#include <objidl.h>
#include <gdiplus.h>
#include <atomic>
#include <future>
#include <thread>

namespace wifiname {

inline void focusQrWindow(HWND window)
{
    // The host grants foreground activation for user actions. If Windows still
    // associates input with the previous foreground thread, join its input queue
    // only for activation and detach immediately afterwards (also used by Qt).
    const DWORD current = GetCurrentThreadId();
    const HWND foreground = GetForegroundWindow();
    const DWORD previous = foreground ? GetWindowThreadProcessId(foreground, nullptr) : 0;
    const bool attached = previous && previous != current && AttachThreadInput(current, previous, TRUE);
    SetForegroundWindow(window);
    SetActiveWindow(window);
    SetFocus(window);
    if (attached)
        AttachThreadInput(current, previous, FALSE);
}

inline BITMAPINFO qrBitmapInfo(const QrBitmap& image)
{
    BITMAPINFO info {};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = image.size;
    info.bmiHeader.biHeight = -image.size;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    return info;
}

inline bool copyQrImage(const QrBitmap& image)
{
    if (image.size <= 0)
        return false;
    const auto info = qrBitmapInfo(image);
    const size_t bytes = image.pixels.size() * sizeof(uint32_t);
    HGLOBAL data = GlobalAlloc(GMEM_MOVEABLE, sizeof(BITMAPINFOHEADER) + bytes);
    if (!data)
        return false;
    auto* dst = static_cast<unsigned char*>(GlobalLock(data));
    if (!dst) {
        GlobalFree(data);
        return false;
    }
    std::memcpy(dst, &info.bmiHeader, sizeof(BITMAPINFOHEADER));
    std::memcpy(dst + sizeof(BITMAPINFOHEADER), image.pixels.data(), bytes);
    GlobalUnlock(data);
    // A real owner is required: EmptyClipboard after OpenClipboard(nullptr)
    // leaves the owner null and SetClipboardData can fail.
    HWND owner = CreateWindowExW(0, L"STATIC", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, nullptr, nullptr);
    bool opened = false;
    if (owner) {
        for (int attempt = 0; attempt < 5 && !opened; ++attempt) {
            opened = OpenClipboard(owner) != FALSE;
            if (!opened)
                Sleep(20);
        }
    }
    bool ok = false;
    if (opened) {
        ok = EmptyClipboard() && SetClipboardData(CF_DIB, data);
        CloseClipboard();
    }
    if (!ok)
        GlobalFree(data);
    if (owner)
        DestroyWindow(owner);
    return ok;
}

// Encode in memory, then create a new file atomically. A name collision never
// truncates another image. No intermediate file ever contains the password.
inline bool saveQrImage(const QrBitmap& image, const std::wstring& path)
{
    if (image.size <= 0)
        return false;
    Gdiplus::GdiplusStartupInput input;
    ULONG_PTR token = 0;
    if (Gdiplus::GdiplusStartup(&token, &input, nullptr) != Gdiplus::Ok)
        return false;
    bool ok = false;
    {
        Gdiplus::Bitmap bitmap(image.size, image.size, image.size * 4, PixelFormat32bppARGB,
            reinterpret_cast<BYTE*>(const_cast<uint32_t*>(image.pixels.data())));
        IStream* stream = nullptr;
        const CLSID png = {0x557cf406, 0x1a04, 0x11d3, {0x9a, 0x73, 0x00, 0x00, 0xf8, 0x1e, 0xf3, 0x2e}};
        if (SUCCEEDED(CreateStreamOnHGlobal(nullptr, TRUE, &stream))) {
            if (bitmap.Save(stream, &png, nullptr) == Gdiplus::Ok) {
                HGLOBAL buffer = nullptr;
                STATSTG stat {};
                if (SUCCEEDED(GetHGlobalFromStream(stream, &buffer)) && SUCCEEDED(stream->Stat(&stat, STATFLAG_NONAME))
                    && stat.cbSize.HighPart == 0) {
                    const void* bytes = GlobalLock(buffer);
                    if (bytes) {
                        HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
                        if (file != INVALID_HANDLE_VALUE) {
                            DWORD written = 0;
                            ok = WriteFile(file, bytes, stat.cbSize.LowPart, &written, nullptr) && written == stat.cbSize.LowPart;
                            CloseHandle(file);
                            if (!ok)
                                DeleteFileW(path.c_str());
                        }
                        GlobalUnlock(buffer);
                    }
                }
            }
            stream->Release();
        }
    }
    Gdiplus::GdiplusShutdown(token);
    return ok;
}

inline std::wstring saveQrInPictures(const QrBitmap& image)
{
    wchar_t user[32768];
    const DWORD length = GetEnvironmentVariableW(L"USERPROFILE", user, 32768);
    if (!length || length >= 32768)
        return {};
    const std::wstring directory = std::wstring(user) + L"\\Pictures";
    if (!CreateDirectoryW(directory.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS)
        return {};
    SYSTEMTIME now {};
    GetLocalTime(&now);
    for (int suffix = 0; suffix < 1000; ++suffix) {
        wchar_t name[100];
        swprintf_s(name, L"\\wifi-qr-%04u%02u%02u-%02u%02u%02u-%d.png", now.wYear, now.wMonth, now.wDay,
            now.wHour, now.wMinute, now.wSecond, suffix);
        const auto path = directory + name;
        if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES)
            continue;
        if (saveQrImage(image, path))
            return path;
        return {};
    }
    return {};
}

class QrPreview {
public:
    ~QrPreview() { close(); }

    void close()
    {
        if (HWND window = m_window.load())
            PostMessageW(window, WM_CLOSE, 0, 0);
        if (m_thread.joinable())
            m_thread.join();
    }

    bool show(QrBitmap image, const std::string& ssid, bool zh)
    {
        close();
        m_image = std::move(image);
        m_ssid = winshun::wide(ssid);
        m_hint = zh ? L"扫码连接 · Esc 关闭" : L"Scan to join · Esc to close";
        std::promise<bool> ready;
        auto result = ready.get_future();
        m_thread = std::thread([this, ready = std::move(ready)]() mutable {
            WNDCLASSW wc {};
            wc.lpfnWndProc = windowProc;
            wc.hInstance = GetModuleHandleW(nullptr);
            wc.lpszClassName = L"WinShunWifiQrPreview";
            wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
            wc.hIcon = LoadIconW(wc.hInstance, L"IDI_ICON1");
            wc.hbrBackground = static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH));
            if (!RegisterClassW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
                ready.set_value(false);
                return;
            }
            POINT point {};
            GetCursorPos(&point);
            MONITORINFO monitor {sizeof(MONITORINFO)};
            GetMonitorInfoW(MonitorFromPoint(point, MONITOR_DEFAULTTONEAREST), &monitor);
            const auto& work = monitor.rcWork;
            const int modules = m_image.size / 8;
            const int scale = std::max(1, std::min({8, static_cast<int>((work.right - work.left - 64) / modules),
                static_cast<int>((work.bottom - work.top - 150) / modules)}));
            m_displaySize = modules * scale;
            RECT rect {0, 0, std::max(320, m_displaySize + 32), m_displaySize + 90};
            const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU;
            AdjustWindowRectEx(&rect, style, FALSE, WS_EX_TOPMOST);
            const int width = rect.right - rect.left, height = rect.bottom - rect.top;
            HWND window = CreateWindowExW(WS_EX_TOPMOST, wc.lpszClassName, L"Wi-Fi QR", style,
                work.left + (work.right - work.left - width) / 2, work.top + (work.bottom - work.top - height) / 2,
                width, height, nullptr, nullptr, wc.hInstance, this);
            m_window.store(window);
            if (!window) {
                ready.set_value(false);
                return;
            }
            SendMessageW(window, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(wc.hIcon));
            SendMessageW(window, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(wc.hIcon));
            ShowWindow(window, SW_SHOW);
            focusQrWindow(window);
            UpdateWindow(window);
            ready.set_value(true);
            MSG msg {};
            while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
        });
        return result.get();
    }

    HWND window() const { return m_window.load(); }

private:
    static LRESULT CALLBACK windowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
    {
        if (message == WM_NCCREATE) {
            const auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
        }
        auto* self = reinterpret_cast<QrPreview*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (!self)
            return DefWindowProcW(window, message, wparam, lparam);
        if (message == WM_KEYDOWN && wparam == VK_ESCAPE) {
            DestroyWindow(window);
            return 0;
        }
        if (message == WM_DESTROY) {
            self->m_window.store(nullptr);
            PostQuitMessage(0);
            return 0;
        }
        if (message == WM_PAINT) {
            PAINTSTRUCT ps {};
            HDC dc = BeginPaint(window, &ps);
            RECT client {};
            GetClientRect(window, &client);
            const auto info = qrBitmapInfo(self->m_image);
            SetStretchBltMode(dc, COLORONCOLOR);
            StretchDIBits(dc, (client.right - self->m_displaySize) / 2, 12, self->m_displaySize, self->m_displaySize,
                0, 0, self->m_image.size, self->m_image.size, self->m_image.pixels.data(), &info, DIB_RGB_COLORS, SRCCOPY);
            auto previous = SelectObject(dc, GetStockObject(DEFAULT_GUI_FONT));
            SetBkMode(dc, TRANSPARENT);
            RECT label {16, self->m_displaySize + 24, client.right - 16, self->m_displaySize + 48};
            DrawTextW(dc, self->m_ssid.c_str(), -1, &label, DT_CENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
            label.top += 26;
            label.bottom += 26;
            DrawTextW(dc, self->m_hint.c_str(), -1, &label, DT_CENTER | DT_SINGLELINE | DT_NOPREFIX);
            SelectObject(dc, previous);
            EndPaint(window, &ps);
            return 0;
        }
        return DefWindowProcW(window, message, wparam, lparam);
    }

    std::thread m_thread;
    std::atomic<HWND> m_window {nullptr};
    QrBitmap m_image;
    std::wstring m_ssid, m_hint;
    int m_displaySize = 0;
};

} // namespace wifiname
