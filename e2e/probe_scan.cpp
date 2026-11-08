// probe_scan.cpp — print what WlanGetNetworkBssList really returns.
#include "native.h"
#include <cstdio>

int main()
{
    wifiname::WlanSession session;
    const DWORD rc = session.open();
    if (rc != ERROR_SUCCESS) {
        printf("open fail %lu\n", rc);
        return 1;
    }
    auto adapters = wifiname::listInterfaces(session.handle());
    printf("adapters: %zu\n", adapters.size());
    for (const auto& a : adapters) {
        printf("iface %s radioOff=%d\n", a.name.c_str(), a.radioOff);
        wifiname::maybeScan(session.handle(), a.guid, 0);
        Sleep(3000); // give the scan a moment
        PWLAN_BSS_LIST list = nullptr;
        DWORD e = ::WlanGetNetworkBssList(session.handle(), &a.guid, nullptr,
            dot11_BSS_type_any, FALSE, nullptr, &list);
        printf("BssList rc=%lu list=%p\n", e, (void*)list);
        if (e == ERROR_SUCCESS && list) {
            printf("entries: %lu\n", list->dwNumberOfItems);
            for (DWORD i = 0; i < list->dwNumberOfItems && i < 25; ++i) {
                const WLAN_BSS_ENTRY& en = list->wlanBssEntries[i];
                std::string ssid = wifiname::ssidText(en.dot11Ssid);
                printf("  ssid='%s' rssi=%ld ch=%lu ieSize=%lu cap=0x%04x\n",
                    ssid.c_str(), en.lRssi, en.ulChCenterFrequency,
                    en.ulIeSize, en.usCapabilityInformation);
            }
            ::WlanFreeMemory(list);
        }
    }
    return 0;
}
