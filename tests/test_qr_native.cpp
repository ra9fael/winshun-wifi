// Fixture-only native tests: no real wireless credentials or connections.
#define WIFI_TEST
#include "../main.cpp"
#include <fstream>

int wmain(int argc, wchar_t** argv)
{
    const bool interactive = argc > 1 && std::wstring(argv[1]) == L"--interactive";
    const bool ui = interactive || (argc > 1 && std::wstring(argv[1]) == L"--ui");
    Profile profile;
    profile.ssid = "Cafe ; 中文";
    profile.auth = Auth::Wpa2;
    profile.cipherText = "AES";
    profile.hasKey = true;
    profile.keyMaterial = "fixture:pass,word";
    const auto payload = makeWifiQr(profile).payload;
    const auto image = renderWifiQr(payload);
    Network network;
    network.ssid = profile.ssid;
    network.profileName = "different-profile-name";
    network.interfaceName = "fixture-adapter";
    network.saved = true;
    network.auth = Auth::Wpa2;
    const auto row = rowToItem(network, Settings {}, true).toJson();
    if (row["data"]["profile"].toString() != network.profileName || !row["data"]["payload"].isNull()
        || !row["data"]["password"].isNull())
        return 1;
    const auto actions = actionsFor(network, true);
    if (std::count_if(actions.begin(), actions.end(), [](const Action& a) { return a.id == "previewQr"; }) != 1)
        return 2;
    network.saved = false;
    for (const auto& a : actionsFor(network, true))
        if (a.id == "copyQr" || a.id == "previewQr" || a.id == "saveQr")
            return 3;
    network.saved = true;
    network.auth = Auth::Enterprise;
    for (const auto& a : actionsFor(network, true))
        if (a.id == "copyQr" || a.id == "previewQr" || a.id == "saveQr")
            return 4;

    wchar_t temp[MAX_PATH];
    GetTempPathW(MAX_PATH, temp);
    const std::wstring path = argc > 2 ? argv[2] : std::wstring(temp) + L"wifi-qr-test-" + std::to_wstring(GetCurrentProcessId()) + L".png";
    if (!saveQrImage(image, path) || saveQrImage(image, path)) // never overwrite
        return 5;
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    unsigned char signature[8] {};
    DWORD read = 0;
    const bool png = file != INVALID_HANDLE_VALUE && ReadFile(file, signature, 8, &read, nullptr) && read == 8
        && std::memcmp(signature, "\x89PNG\r\n\x1a\n", 8) == 0;
    if (file != INVALID_HANDLE_VALUE)
        CloseHandle(file);
    if (!png)
        return 6;
    if (argc <= 2)
        DeleteFileW(path.c_str());
    if (ui) {
        Sleep(300); // allow the harness to grant foreground activation, as the host does
        if (!copyQrImage(image) || !IsClipboardFormatAvailable(CF_DIB))
            return 7;
        QrPreview preview;
        if (interactive) {
            if (!preview.show(image, profile.ssid, true)) return 8;
            while (preview.window()) Sleep(50);
            return 0;
        }
        for (int i = 0; i < 3; ++i) {
            if (!preview.show(image, profile.ssid, true))
                return 8;
            Sleep(150);
            HWND window = preview.window();
            GUITHREADINFO info {sizeof(GUITHREADINFO)};
            if (!window || GetForegroundWindow() != window || !GetGUIThreadInfo(GetWindowThreadProcessId(window, nullptr), &info)
                || info.hwndFocus != window) {
                std::printf("focus mismatch: window=%p foreground=%p focus=%p\n", window, GetForegroundWindow(), info.hwndFocus);
                return 9;
            }
            if (i == 2)
                Sleep(2500); // let a local validation harness capture the fixture window
            PostMessageW(window, WM_KEYDOWN, VK_ESCAPE, 0);
            for (int j = 0; j < 40 && preview.window(); ++j)
                Sleep(25);
            if (preview.window())
                return 10;
        }
        preview.close();
    }
    std::puts("PASS: QR actions, PNG signature, no overwrite, and optional clipboard/focus/Esc checks");
    return 0;
}
