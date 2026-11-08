// Native clipboard, PNG and focused preview. No Qt or QR-plugin dependency.
#pragma once

#include "wifi_qr.h"
#include <objidl.h>
#include <gdiplus.h>
#include <atomic>
#include <future>
#include <thread>
#include <cmath>
#include <windowsx.h>

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
        if (m_thread.joinable() && m_thread.get_id() != std::this_thread::get_id())
            m_thread.join();
    }

    bool show(QrBitmap image, const std::string& ssid, bool zh)
    {
        close();
        m_image = std::move(image);
        m_ssid = winshun::wide(ssid);
        m_zh = zh;
        m_hint = zh ? L"右键菜单 · Esc / Ctrl+D / Ctrl+W 关闭" : L"Right-click menu · Esc / Ctrl+D / Ctrl+W to close";
        std::promise<bool> ready;
        auto result = ready.get_future();
        m_thread = std::thread([this, ready = std::move(ready)]() mutable {
            SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
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
            const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME;
            AdjustWindowRectEx(&rect, style, FALSE, WS_EX_TOPMOST);
            m_default = {rect.right - rect.left, rect.bottom - rect.top};
            const int width = m_geometry.right > m_geometry.left ? std::min(m_geometry.right - m_geometry.left, work.right - work.left) : m_default.cx;
            const int height = m_geometry.bottom > m_geometry.top ? std::min(m_geometry.bottom - m_geometry.top, work.bottom - work.top) : m_default.cy;
            const int x = m_geometry.right > m_geometry.left ? std::clamp(m_geometry.left, work.left, work.right - width) : work.left + (work.right - work.left - width) / 2;
            const int y = m_geometry.bottom > m_geometry.top ? std::clamp(m_geometry.top, work.top, work.bottom - height) : work.top + (work.bottom - work.top - height) / 2;
            HWND window = CreateWindowExW(WS_EX_TOPMOST, wc.lpszClassName, L"Wi-Fi QR", style,
                x, y,
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
    void zoomWindow(HWND window, int delta)
    {
        if (!delta) return;
        RECT rect {}, client {};
        GetWindowRect(window, &rect);
        GetClientRect(window, &client);
        MONITORINFO monitor {sizeof(MONITORINFO)};
        GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &monitor);
        const auto& work = monitor.rcWork;
        const int dpi = static_cast<int>(GetDpiForWindow(window));
        const int marginX = MulDiv(32, dpi, 96), marginY = MulDiv(96, dpi, 96);
        const double factor = std::pow(1.15, std::clamp(delta / 120.0, -4.0, 4.0));
        const int frameX = rect.right - rect.left - client.right;
        const int frameY = rect.bottom - rect.top - client.bottom;
        const int width = std::clamp<int>(static_cast<int>((client.right - marginX) * factor) + marginX + frameX,
            MulDiv(320, dpi, 96), work.right - work.left);
        const int height = std::clamp<int>(static_cast<int>((client.bottom - marginY) * factor) + marginY + frameY,
            MulDiv(300, dpi, 96), work.bottom - work.top);
        SetWindowPos(window, nullptr,
            std::clamp<LONG>((rect.left + rect.right - width) / 2, work.left, work.right - width),
            std::clamp<LONG>((rect.top + rect.bottom - height) / 2, work.top, work.bottom - height),
            width, height, SWP_NOZORDER | SWP_NOACTIVATE);
    }

    inline static thread_local HWND s_menuWindow = nullptr;
    static bool closeChord(WPARAM key)
    {
        return (key == 'D' || key == 'W') && (GetKeyState(VK_CONTROL) & 0x8000);
    }
    static LRESULT CALLBACK menuFilter(int code, WPARAM wparam, LPARAM lparam)
    {
        const auto* message = reinterpret_cast<const MSG*>(lparam);
        if (code == MSGF_MENU && message->message == WM_KEYDOWN && closeChord(message->wParam)) {
            EndMenu();
            PostMessageW(s_menuWindow, WM_CLOSE, 0, 0);
            return 1;
        }
        return CallNextHookEx(nullptr, code, wparam, lparam);
    }
    void contextMenu(HWND window, POINT point)
    {
        HMENU menu = CreatePopupMenu();
        if (!menu) return;
        AppendMenuW(menu, MF_STRING, 1, m_zh ? L"复制图片" : L"Copy image");
        AppendMenuW(menu, MF_STRING, 2, m_zh ? L"保存 PNG" : L"Save PNG");
        AppendMenuW(menu, MF_STRING, 3, m_zh ? L"恢复默认大小" : L"Restore default size");
        AppendMenuW(menu, MF_STRING, 4, m_zh ? L"关闭" : L"Close");
        s_menuWindow = window;
        HHOOK hook = SetWindowsHookExW(WH_MSGFILTER, menuFilter, nullptr, GetCurrentThreadId());
        const UINT command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, point.x, point.y, 0, window, nullptr);
        if (hook) UnhookWindowsHookEx(hook);
        s_menuWindow = nullptr;
        DestroyMenu(menu);
        if (command == 1) {
            const bool ok = copyQrImage(m_image);
            m_hint = ok ? (m_zh ? L"已复制图片" : L"Image copied") : (m_zh ? L"复制失败" : L"Copy failed");
        } else if (command == 2) {
            const auto path = saveQrInPictures(m_image);
            m_hint = path.empty() ? (m_zh ? L"保存失败" : L"Save failed") : (m_zh ? L"已保存 " : L"Saved ") + path;
        } else if (command == 3) {
            MONITORINFO monitor {sizeof(MONITORINFO)};
            GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &monitor);
            RECT rect {}; GetWindowRect(window, &rect);
            const auto& work = monitor.rcWork;
            const int width = std::min(m_default.cx, work.right - work.left);
            const int height = std::min(m_default.cy, work.bottom - work.top);
            SetWindowPos(window, nullptr, std::clamp(rect.left, work.left, work.right - width),
                std::clamp(rect.top, work.top, work.bottom - height), width, height, SWP_NOZORDER);
        } else if (command == 4) {
            PostMessageW(window, WM_CLOSE, 0, 0);
        }
        InvalidateRect(window, nullptr, TRUE);
    }
    static LRESULT CALLBACK windowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
    {
        if (message == WM_NCCREATE) {
            const auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
        }
        auto* self = reinterpret_cast<QrPreview*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (!self)
            return DefWindowProcW(window, message, wparam, lparam);
        if (message == WM_KEYDOWN && GetForegroundWindow() == window && (wparam == VK_ESCAPE || closeChord(wparam))) {
            DestroyWindow(window);
            return 0;
        }
        if (message == WM_LBUTTONDOWN) {
            ReleaseCapture();
            SendMessageW(window, WM_NCLBUTTONDOWN, HTCAPTION, lparam);
            return 0;
        }
        if (message == WM_MOUSEWHEEL) {
            if (GetForegroundWindow() == window)
                self->zoomWindow(window, GET_WHEEL_DELTA_WPARAM(wparam));
            return 0;
        }
        if (message == WM_CONTEXTMENU) {
            POINT point {GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
            if (point.x == -1 && point.y == -1) { point = {24, 24}; ClientToScreen(window, &point); }
            self->contextMenu(window, point);
            return 0;
        }
        if (message == WM_GETMINMAXINFO) {
            auto* limits = reinterpret_cast<MINMAXINFO*>(lparam);
            const int dpi = static_cast<int>(GetDpiForWindow(window));
            limits->ptMinTrackSize = {MulDiv(320, dpi ? dpi : 96, 96), MulDiv(300, dpi ? dpi : 96, 96)};
            return 0;
        }
        if (message == WM_DPICHANGED) {
            const auto* rect = reinterpret_cast<RECT*>(lparam);
            SetWindowPos(window, nullptr, rect->left, rect->top, rect->right - rect->left,
                rect->bottom - rect->top, SWP_NOZORDER | SWP_NOACTIVATE);
            return 0;
        }
        if (message == WM_SIZE) { InvalidateRect(window, nullptr, TRUE); return 0; }
        if (message == WM_DESTROY) {
            GetWindowRect(window, &self->m_geometry);
            self->m_window.store(nullptr);
            PostQuitMessage(0);
            return 0;
        }
        if (message == WM_PAINT) {
            PAINTSTRUCT ps {};
            HDC dc = BeginPaint(window, &ps);
            RECT client {};
            GetClientRect(window, &client);
            const int dpi = static_cast<int>(GetDpiForWindow(window));
            const int padding = MulDiv(16, dpi, 96);
            const int labelHeight = MulDiv(64, dpi, 96);
            const int modules = self->m_image.size / 8;
            const int available = std::max<int>(modules, std::min(client.right - 2 * padding, client.bottom - labelHeight - 2 * padding));
            const int display = available / modules * modules;
            const int top = padding + std::max<int>(0, (client.bottom - labelHeight - 2 * padding - display) / 2);
            const auto info = qrBitmapInfo(self->m_image);
            SetStretchBltMode(dc, COLORONCOLOR);
            StretchDIBits(dc, (client.right - display) / 2, top, display, display,
                0, 0, self->m_image.size, self->m_image.size, self->m_image.pixels.data(), &info, DIB_RGB_COLORS, SRCCOPY);
            auto previous = SelectObject(dc, GetStockObject(DEFAULT_GUI_FONT));
            SetBkMode(dc, TRANSPARENT);
            RECT label {padding, client.bottom - labelHeight, client.right - padding, client.bottom - labelHeight / 2};
            DrawTextW(dc, self->m_ssid.c_str(), -1, &label, DT_CENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
            label.top += labelHeight / 2;
            label.bottom += labelHeight / 2;
            DrawTextW(dc, self->m_hint.c_str(), -1, &label, DT_CENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
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
    bool m_zh = true;
    RECT m_geometry {};
    SIZE m_default {};
};

} // namespace wifiname
