// main.cpp — ra9fael.wifi: see the current Wi-Fi connection, saved profiles
// and nearby networks; connect, disconnect, reveal the password, forget a
// network, share a Wi-Fi QR code, copy the profile XML. A resident JSON-RPC
// plugin on stdio; native Windows APIs and CRT, with no Qt dependency.
//
// English code/comments per project convention; user-facing strings follow
// the host locale (plugin.chinese()).

#include "native.h"
#include "qr_image.h"

using namespace winshun;
using namespace wifiname;

namespace {

// A hint row: no action, Enter does nothing.
Item hintItem(Hint h, bool zh)
{
    return Item("wifi:hint:" + std::to_string(static_cast<int>(h)), hintTitle(h, zh), hintSubtitle(h, zh))
        .invalid();
}

// A one-line notice row for an action's outcome that the footer cannot carry
// on its own (a refusal, a wlanapi error).
Item noticeItem(const std::string& id, const std::string& title, const std::string& subtitle = {})
{
    return Item("wifi:notice:" + id, title, subtitle).invalid();
}


// The actions a row offers. A connected row offers disconnect; a row that can
// connect offers connect; a saved profile offers the password, XML and forget
// actions.
std::vector<Action> actionsFor(const Network& n, bool zh)
{
    std::vector<Action> actions;
    if (n.connected) {
        actions.push_back(Action {"disconnect", zh ? "断开" : "Disconnect", "Ctrl+Enter"});
    } else if (canConnect(n)) {
        actions.push_back(Action {"connect", zh ? "连接" : "Connect", "Enter"});
    }
    if (n.saved) {
        actions.push_back(Action {"password", zh ? "查看密码" : "Reveal password", "Shift+Enter"});
        actions.push_back(Action {"copyPassword", zh ? "复制密码" : "Copy password", ""});
        if (n.auth != Auth::Enterprise) {
            actions.push_back(Action {"copyQr", zh ? "复制 Wi-Fi 二维码" : "Copy Wi-Fi QR code", ""});
            actions.push_back(Action {"previewQr", zh ? "预览 Wi-Fi 二维码" : "Preview Wi-Fi QR code", ""});
            actions.push_back(Action {"saveQr", zh ? "保存 Wi-Fi 二维码 PNG" : "Save Wi-Fi QR PNG", ""});
        }
        actions.push_back(Action {"copyXml", zh ? "复制配置 XML" : "Copy profile XML", ""});
        actions.push_back(Action {"forget", zh ? "忘记网络" : "Forget network", ""});
    }
    return actions;
}

// The confirmation row for a forget: the only row the confirm query shows, so
// it is the first row and the keyboard lands on it. Enter here deletes.
Item forgetConfirmItem(const Network& n, bool zh)
{
    Item item;
    item.id = "wifi:forgetConfirm:" + n.ssid;
    item.title = zh ? "⚠ 忘记 " + rowTitle(n, zh) + "？" : "⚠ Forget " + rowTitle(n, zh) + "?";
    item.subtitle = zh ? "Enter 删除这个配置；Esc 返回列表" : "Enter deletes this profile; Esc goes back";
    item.icon = "color:#dc2626";
    item.withData(Json::Object {{"ssid", n.ssid},
        {"profile", n.profileName},
        {"bssid", n.bssid},
        {"interface", n.interfaceName},
        {"saved", n.saved},
        {"connected", n.connected},
        {"confirmForget", true}});
    item.withActions({Action {"forget", zh ? "确认忘记" : "Confirm forget", "Enter"}});
    return item;
}

// One network → one row. `revealed` is the key this session has already shown
// for the network, so the row can display it (masked or not) without
// re-reading the profile.
Item rowToItem(const Network& n, const Settings& st, bool zh, const std::string& revealed = {})
{
    Item item;
    item.id = rowId(n);
    item.title = rowTitle(n, zh);
    item.subtitle = rowSubtitle(n, zh, st.showPasswordMasked);
    if (!revealed.empty()) {
        // A key this session revealed: shown in the row, masked or plainly
        // per the setting. It lives in memory only, never on disk.
        const std::string shown = passwordDisplay(revealed, true, st.showPasswordMasked);
        item.subtitle += (zh ? " · 密码 " : " · key ") + shown;
    }
    if (n.connected)
        item.icon = "color:#16a34a";
    else if (n.saved)
        item.icon = "color:#2563eb";
    else if (isOpen(n.auth))
        item.icon = "color:#d97706";
    else
        item.icon = "color:#64748b";
    item.withCopy(n.ssid);
    item.withData(Json::Object {{"ssid", n.ssid},
        {"profile", n.profileName},
        {"bssid", n.bssid},
        {"interface", n.interfaceName},
        {"saved", n.saved},
        {"connected", n.connected},
        {"visible", n.visible},
        {"auth", authLabel(n.auth, n.authText)},
        {"open", isOpen(n.auth)},
        {"confirmForget", false}});
    item.withActions(actionsFor(n, zh));
    return item;
}

class WifiPlugin final : public Plugin {
public:
    std::string name() const override { return "ra9fael.wifi"; }

    void initialized() override { m_settings = readSettings(settings()); }

    void settingsChanged(const Json& settings) override { m_settings = readSettings(settings); }

    Items query(const Query& q) override { return build(q, true); }

    Json queryResult(const Query& q) override
    {
        Json::Object result {{"items", itemsJson(build(q, true))}};
        // A connection is asynchronous (spec §5): the page re-asks so the
        // state catches up without the user typing again.
        result["rerun"] = 2.0;
        return Json(std::move(result));
    }

    Effects action(const ActionRequest& a) override
    {
        const bool zh = chinese();
        const Json& data = a.item["data"];
        const std::string ssid = data["ssid"].toString();
        const std::string profile = data["profile"].toString().empty() ? ssid : data["profile"].toString();
        const std::string interfaceName = data["interface"].toString();
        const bool saved = data["saved"].toBool();
        const bool connected = data["connected"].toBool();
        const bool open = data["open"].toBool();

        if (a.action == "connect")
            return doConnect(saved ? profile : ssid, interfaceName, saved, connected, open, zh);
        if (a.action == "disconnect")
            return doDisconnect(interfaceName, zh);
        if (a.action == "password")
            return doReveal(profile, interfaceName, zh);
        if (a.action == "copyPassword")
            return doCopyPassword(profile, interfaceName, zh);
        if (a.action == "copyXml")
            return doCopyXml(profile, interfaceName, zh);
        if (a.action == "copyQr" || a.action == "previewQr" || a.action == "saveQr")
            return doQr(ssid, profile, interfaceName, a.action, zh);
        if (a.action == "forget")
            return doForget(a, ssid, interfaceName, zh);

        return {effect::keep()};
    }

private:
    Settings m_settings;
    QrPreview m_preview;

    // Keys this session has revealed, by WLAN profile name. Memory only: never written to
    // disk, never logged, cleared when the process ends (spec §7).
    std::map<std::string, std::string> m_revealed;

    std::string revealedFor(const std::string& ssid) const
    {
        const auto it = m_revealed.find(ssid);
        return it == m_revealed.end() ? std::string() : it->second;
    }

    // The rows for one query. `wantScan` asks the radio for a fresh scan
    // (throttled inside native.h); an action's follow-up query does not.
    //
    // A query whose words start with "forget " is the confirmation step: it
    // shows that one network's delete row and nothing else. Carrying the step
    // in the query text (not in plugin state) is what makes Esc a real way
    // back: the launcher's Esc clears the words after the keyword and re-asks
    // the plain list (spec §4.2).
    Items build(const Query& q, bool wantScan)
    {
        const bool zh = chinese();
        const std::string text = trim(q.text);

        if (const std::string target = forgetTarget(text); !target.empty()) {
            const Snapshot snap = collect(false);
            for (const Network& n : snap.networks) {
                if (n.saved && n.ssid == target)
                    return {forgetConfirmItem(n, zh)};
            }
            // The profile went between the two presses: say so instead of
            // showing a delete row for something that is not there.
            return {noticeItem("gone",
                zh ? "这个网络已经没有保存的配置" : "No saved profile for this network",
                zh ? "按 Esc 返回列表" : "Press Esc to go back")};
        }

        const Snapshot snap = collect(wantScan);

        Items items;
        if (snap.noAdapter || snap.serviceDown) {
            items.push_back(hintItem(snap.noAdapter ? Hint::NoAdapter : Hint::ServiceDown, zh));
            return items;
        }
        if (!snap.problem.empty()) {
            items.push_back(noticeItem("error", zh ? "读取 Wi-Fi 状态失败" : "Could not read the Wi-Fi state",
                snap.problem));
            return items;
        }

        const std::string filter = text;
        std::vector<Network> rows = buildRows(snap.networks, m_settings, filter);

        // The current connection stays on top even when it does not match the
        // filter (spec §3): the user is asking about the network they are on.
        if (!filter.empty()) {
            for (const Network& n : snap.networks) {
                if (!n.connected || matchesFilter(n.ssid, filter))
                    continue;
                rows.insert(rows.begin(), n);
                break;
            }
        }

        if (rows.empty()) {
            items.push_back(hintItem(Hint::NoResults, zh));
            return items;
        }

        const int limit = q.limit > 0 ? q.limit : 200;
        for (const Network& n : rows) {
            if (static_cast<int>(items.size()) >= limit)
                break;
            items.push_back(rowToItem(n, m_settings, zh, revealedFor(n.profileName.empty() ? n.ssid : n.profileName)));
        }
        return items;
    }

    // The interface a network lives on, opened for one action.
    static bool openFor(const std::string& interfaceName, WlanSession& session, GUID& guid)
    {
        if (session.open() != ERROR_SUCCESS)
            return false;
        return findInterface(session.handle(), interfaceName, guid);
    }

    Effects doConnect(const std::string& ssid, const std::string& interfaceName, bool saved, bool connected,
        bool open, bool zh)
    {
        if (connected)
            return {effect::notify(zh ? "已经连上了" : "Already connected"), effect::keep()};
        if (!saved && !open)
            return {effect::notify(zh ? "请在系统设置输入密码" : "Enter the password in Windows Settings"),
                effect::keep()};

        WlanSession session;
        GUID guid {};
        if (!openFor(interfaceName, session, guid))
            return {effect::notify(zh ? "找不到无线网卡" : "No wireless adapter"), effect::keep()};

        const ActionResult r = saved ? connectProfile(session.handle(), guid, ssid)
                                     : connectOpen(session.handle(), guid, ssid);
        if (!r.ok) {
            log("warn", "connect failed: " + r.problem);
            return {effect::notify(zh ? "连接失败" : "Could not connect"), effect::keep()};
        }
        refresh(); // the page re-asks; the row turns connected
        return {effect::notify(zh ? "已发起连接" : "Connecting"), effect::keep()};
    }

    Effects doDisconnect(const std::string& interfaceName, bool zh)
    {
        WlanSession session;
        GUID guid {};
        if (!openFor(interfaceName, session, guid))
            return {effect::notify(zh ? "找不到无线网卡" : "No wireless adapter"), effect::keep()};
        const ActionResult r = disconnect(session.handle(), guid);
        if (!r.ok) {
            log("warn", "disconnect failed: " + r.problem);
            return {effect::notify(zh ? "断开失败" : "Could not disconnect"), effect::keep()};
        }
        refresh();
        return {effect::notify(zh ? "已断开" : "Disconnected"), effect::keep()};
    }

    // The plaintext key, or why it could not be had. Never written to a log.
    bool readPlaintextKey(const std::string& ssid, const std::string& interfaceName, std::string& key,
        std::string& problem, bool& denied)
    {
        WlanSession session;
        GUID guid {};
        if (!openFor(interfaceName, session, guid)) {
            problem = "no adapter";
            return false;
        }
        std::string xml;
        DWORD error = 0;
        if (!profileXml(session.handle(), guid, ssid, true, xml, &error)) {
            problem = "WlanGetProfile error " + std::to_string(error);
            denied = error == ERROR_ACCESS_DENIED;
            return false;
        }
        const Profile parsed = parseProfile(xml);
        if (parsed.keyMaterial.empty() || parsed.keyProtected) {
            // No key at all (an open network), or the blob came back still
            // encrypted because the caller is not an administrator.
            denied = parsed.keyProtected;
            problem = parsed.keyMaterial.empty() ? "no key" : "key still protected";
            return false;
        }
        key = parsed.keyMaterial;
        return true;
    }

    Effects doReveal(const std::string& ssid, const std::string& interfaceName, bool zh)
    {
        std::string key;
        std::string problem;
        bool denied = false;
        if (!readPlaintextKey(ssid, interfaceName, key, problem, denied)) {
            if (denied)
                return {effect::notify(zh ? "读取密码需要管理员权限（以管理员身份运行 Win顺）"
                                          : "Reading the password needs administrator rights (run WinShun as administrator)"),
                    effect::keep()};
            if (problem == "no key")
                return {effect::notify(zh ? "这个网络没有密码" : "This network has no password"), effect::keep()};
            log("warn", "reveal failed: " + problem);
            return {effect::notify(zh ? "读取密码失败" : "Could not read the password"), effect::keep()};
        }
        // Kept in memory so the row can show it, and copied so it is at hand.
        // A fresh query re-reads the profile; nothing is written anywhere.
        m_revealed[ssid] = key;
        refresh();
        return {effect::copy(key),
            effect::notify(zh ? "密码已复制" : "Password copied")};
    }

    Effects doCopyPassword(const std::string& ssid, const std::string& interfaceName, bool zh)
    {
        std::string key;
        std::string problem;
        bool denied = false;
        if (!readPlaintextKey(ssid, interfaceName, key, problem, denied)) {
            if (denied)
                return {effect::notify(zh ? "复制密码需要以管理员身份运行 Win顺"
                                          : "Copying the password needs WinShun running as administrator"),
                    effect::keep()};
            if (problem == "no key")
                return {effect::notify(zh ? "这个网络没有密码" : "This network has no password"), effect::keep()};
            log("warn", "copyPassword failed: " + problem);
            return {effect::notify(zh ? "读取密码失败" : "Could not read the password"), effect::keep()};
        }
        return {effect::copy(key), effect::notify(zh ? "密码已复制" : "Password copied")};
    }

    Effects doCopyXml(const std::string& ssid, const std::string& interfaceName, bool zh)
    {
        WlanSession session;
        GUID guid {};
        if (!openFor(interfaceName, session, guid))
            return {effect::notify(zh ? "找不到无线网卡" : "No wireless adapter"), effect::keep()};

        // Ask for the plaintext key and judge by what comes back, not by
        // whether this process looks elevated: on current Windows a
        // non-elevated process often does get the plaintext key for the
        // current user's own profile (measured 2026-11-08, see
        // docs/spec.md §4). When the key does come back still protected, the
        // row says so rather than pretending the copy is complete.
        std::string xml;
        DWORD error = 0;
        if (!profileXml(session.handle(), guid, ssid, true, xml, &error)) {
            log("warn", "copyXml failed: WlanGetProfile error " + std::to_string(error));
            return {effect::notify(zh ? "读取配置失败" : "Could not read the profile"), effect::keep()};
        }
        const Profile parsed = parseProfile(xml);
        const bool keyMissing = parsed.hasKey && (parsed.keyMaterial.empty() || parsed.keyProtected);
        if (keyMissing)
            return {effect::copy(xml),
                effect::notify(zh ? "已复制配置（密码段需管理员）" : "Profile copied (the key needs an administrator)")};
        return {effect::copy(xml), effect::notify(zh ? "配置已复制" : "Profile copied")};
    }

    Effects doQr(const std::string& ssid, const std::string& profile, const std::string& interfaceName,
        const std::string& action, bool zh)
    {
        WlanSession session;
        GUID guid {};
        if (!openFor(interfaceName, session, guid))
            return {effect::notify(zh ? "找不到无线网卡" : "No wireless adapter"), effect::keep()};
        std::string xml;
        DWORD error = 0;
        if (!profileXml(session.handle(), guid, profile, true, xml, &error))
            return {effect::notify(error == ERROR_ACCESS_DENIED ? qrProblem(QrStatus::NeedsKey, zh)
                : (zh ? "无法读取已保存的 Wi-Fi 配置" : "Could not read the saved Wi-Fi profile")), effect::keep()};
        const Profile parsed = parseProfile(xml);
        if (parsed.ssid != ssid)
            return {effect::notify(zh ? "网络配置已变化，请刷新后重试" : "The profile changed; refresh and retry"), effect::keep()};
        const auto qr = makeWifiQr(parsed);
        if (qr.status != QrStatus::Ready)
            return {effect::notify(qrProblem(qr.status, zh)), effect::keep()};
        auto image = renderWifiQr(qr.payload);
        if (image.size == 0)
            return {effect::notify(zh ? "二维码生成失败" : "Could not generate the QR code"), effect::keep()};
        if (action == "copyQr") {
            const bool copied = copyQrImage(image);
            return {effect::notify(copied ? (zh ? "Wi-Fi 二维码已复制" : "Wi-Fi QR code copied")
                : (zh ? "剪贴板忙，请重试" : "Clipboard busy; please retry")), effect::keep()};
        }
        if (action == "previewQr") {
            if (!m_preview.show(std::move(image), parsed, zh))
                return {effect::notify(zh ? "预览窗口打开失败" : "Could not open the preview"), effect::keep()};
            return {effect::hide()};
        }
        const auto path = saveQrInPictures(image);
        return {effect::notify(path.empty() ? (zh ? "保存 PNG 失败" : "Could not save the PNG")
            : (zh ? "已保存 " : "Saved ") + winshun::utf8(path)), effect::keep()};
    }

    Effects doForget(const ActionRequest& a, const std::string& ssid, const std::string& interfaceName, bool zh)
    {
        if (!a.item["data"]["confirmForget"].toBool()) {
            // First press: move the confirmation into the query text. The host
            // re-asks with those words, the new list's first row is selected
            // (m_replacePending), and Esc clears the words back to the plain
            // list — a real way back, which a `replace` list does not have
            // (spec §4.2). Nothing is deleted until the second press.
            const Snapshot snap = collect(false);
            bool exists = false;
            for (const Network& n : snap.networks) {
                if (n.saved && n.ssid == ssid) {
                    exists = true;
                    break;
                }
            }
            if (!exists)
                return {effect::notify(zh ? "这个网络没有保存的配置" : "No saved profile for this network"),
                    effect::keep()};
            return {effect::setQuery(forgetQuery(ssid)), effect::keep()};
        }
        WlanSession session;
        GUID guid {};
        if (!openFor(interfaceName, session, guid))
            return {effect::notify(zh ? "找不到无线网卡" : "No wireless adapter"), effect::keep()};
        const std::string profile = a.item["data"]["profile"].toString();
        const ActionResult r = deleteProfile(session.handle(), guid, profile.empty() ? ssid : profile);
        if (!r.ok) {
            log("warn", "forget failed: " + r.problem);
            // A group-policy profile cannot be deleted by the user.
            const bool policy = r.problem.find("error 5") != std::string::npos
                || r.problem.find("error 87") != std::string::npos;
            return {effect::notify(policy ? (zh ? "删除失败：可能由组策略管理" : "Could not delete: the profile may be managed by group policy")
                                          : (zh ? "删除失败" : "Could not delete")),
                effect::keep()};
        }
        refresh();
        // Back to the plain list: the row this query was about is gone, so
        // staying on the confirm query would show a stale delete row.
        return {effect::setQuery(""), effect::notify(zh ? "已忘记这个网络" : "Network forgotten")};
    }
};

} // namespace

#ifndef WIFI_TEST
int main()
{
    return WifiPlugin().serve();
}
#endif
