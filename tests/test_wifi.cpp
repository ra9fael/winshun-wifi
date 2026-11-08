// test_wifi.cpp — ra9fael.wifi's logic tests (docs/tests.md §1): profile XML
// parsing, SSID filtering, row ordering and de-duplication, RSSI→quality,
// band/channel maths, auth classification, password display, and hint rows.
// Pure logic.h only — no wireless adapter is touched.

#include "../logic.h"

#include <cstdio>

using namespace wifiname;

static int g_checks = 0;
static int g_failed = 0;

#define CHECK(cond)                                                                              \
    do {                                                                                         \
        ++g_checks;                                                                              \
        if (!(cond)) {                                                                           \
            ++g_failed;                                                                          \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                          \
        }                                                                                        \
    } while (0)

#define CHECK_EQ(a, b)                                                                                          \
    do {                                                                                                        \
        ++g_checks;                                                                                             \
        if (!((a) == (b))) {                                                                                    \
            ++g_failed;                                                                                         \
            std::printf("FAIL %s:%d: %s == %s (\"%s\" vs \"%s\")\n", __FILE__, __LINE__, #a, #b,                \
                std::string(a).c_str(), std::string(b).c_str());                                                \
        }                                                                                                       \
    } while (0)

#define CHECK_NUM(a, b)                                                                                         \
    do {                                                                                                        \
        ++g_checks;                                                                                             \
        if (!((a) == (b))) {                                                                                    \
            ++g_failed;                                                                                         \
            std::printf("FAIL %s:%d: %s == %s (%lld vs %lld)\n", __FILE__, __LINE__, #a, #b,                    \
                static_cast<long long>(a), static_cast<long long>(b));                                          \
        }                                                                                                       \
    } while (0)

// ---- fixtures ---------------------------------------------------------------------------

static Network net(const char* ssid, int rssi, bool saved, bool visible, bool connected = false)
{
    Network n;
    n.ssid = ssid;
    n.rssi = rssi;
    n.saved = saved;
    n.visible = visible;
    n.connected = connected;
    n.auth = Auth::Wpa2;
    n.authText = "WPA2PSK";
    return n;
}

// A real-shaped WLAN profile XML, as WlanGetProfile returns it.
static const char* kProfileXml =
    "<?xml version=\"1.0\"?>\n"
    "<WLANProfile xmlns=\"http://www.microsoft.com/networking/WLAN/profile/v1\">\n"
    "\t<name>Cafe &amp; Bar</name>\n"
    "\t<SSIDConfig>\n"
    "\t\t<SSID>\n"
    "\t\t\t<name>Cafe &amp; Bar</name>\n"
    "\t\t</SSID>\n"
    "\t</SSIDConfig>\n"
    "\t<connectionType>ESS</connectionType>\n"
    "\t<connectionMode>auto</connectionMode>\n"
    "\t<MSM>\n"
    "\t\t<security>\n"
    "\t\t\t<authEncryption>\n"
    "\t\t\t\t<authentication>WPA2PSK</authentication>\n"
    "\t\t\t\t<encryption>AES</encryption>\n"
    "\t\t\t\t<useOneX>false</useOneX>\n"
    "\t\t\t</authEncryption>\n"
    "\t\t\t<sharedKey>\n"
    "\t\t\t\t<keyType>passPhrase</keyType>\n"
    "\t\t\t\t<protected>true</protected>\n"
    "\t\t\t\t<keyMaterial>01000000D08C9DDF</keyMaterial>\n"
    "\t\t\t</sharedKey>\n"
    "\t\t</security>\n"
    "\t</MSM>\n"
    "</WLANProfile>\n";

// ---- 1. XML text extraction ---------------------------------------------------------------

static void testXmlText()
{
    CHECK_EQ(xmlText("<a><b>hi</b></a>", "b"), std::string("hi"));
    CHECK_EQ(xmlText("<a><b>hi</b></a>", "c"), std::string(""));
    CHECK_EQ(xmlText("<a><b/></a>", "b"), std::string("")); // self-closing
    CHECK_EQ(xmlText("<a><b xmlns=\"x\">hi</b></a>", "b"), std::string("hi"));
    CHECK_EQ(xmlText("", "b"), std::string(""));
    CHECK_EQ(xmlText("<a><bb>no</bb></a>", "b"), std::string("")); // prefix of a longer tag
    CHECK_EQ(xmlText("<a><b>one</b><b>two</b></a>", "b"), std::string("one")); // first wins
    CHECK_EQ(xmlText("<a>\n\t<b>spaced</b>\n</a>", "b"), std::string("spaced"));
}

// ---- 2. XML entities ----------------------------------------------------------------------

static void testXmlDecode()
{
    CHECK_EQ(xmlDecode("plain"), std::string("plain"));
    CHECK_EQ(xmlDecode("a &amp; b"), std::string("a & b"));
    CHECK_EQ(xmlDecode("&lt;tag&gt;"), std::string("<tag>"));
    CHECK_EQ(xmlDecode("&quot;q&quot;"), std::string("\"q\""));
    CHECK_EQ(xmlDecode("&apos;"), std::string("'"));
    CHECK_EQ(xmlDecode("&#65;"), std::string("A"));
    CHECK_EQ(xmlDecode("&#x41;"), std::string("A"));
    CHECK_EQ(xmlDecode("&#x4E2D;"), std::string("\xE4\xB8\xAD")); // 中
    CHECK_EQ(xmlDecode("bare & here"), std::string("bare & here"));
    CHECK_EQ(xmlDecode("&unknown;"), std::string("&unknown;"));
    CHECK_EQ(xmlDecode("&#;"), std::string("&#;"));
    CHECK_EQ(xmlDecode("&#xZZ;"), std::string("&#xZZ;"));
}

// ---- 3. hex SSID --------------------------------------------------------------------------

static void testHex()
{
    CHECK_EQ(hexToBytes("4D794E6574"), std::string("MyNet"));
    CHECK_EQ(hexToBytes(""), std::string(""));
    CHECK_EQ(hexToBytes("4D7"), std::string(""));   // odd length
    CHECK_EQ(hexToBytes("4D7G"), std::string(""));  // not hex
    CHECK_EQ(hexToBytes("e4b8ad"), std::string("\xE4\xB8\xAD"));
}

// ---- 4. auth classification ---------------------------------------------------------------

static void testAuth()
{
    CHECK(authFromText("open") == Auth::Open);
    CHECK(authFromText("OPEN") == Auth::Open);
    CHECK(authFromText("none") == Auth::Open);
    CHECK(authFromText("WEP") == Auth::Wep);
    CHECK(authFromText("WPAPSK") == Auth::Wpa);
    CHECK(authFromText("WPA2PSK") == Auth::Wpa2);
    CHECK(authFromText("WPA3SAE") == Auth::Wpa3);
    CHECK(authFromText("WPA2") == Auth::Wpa2);
    CHECK(authFromText("WPA2PSK", true) == Auth::Enterprise);
    CHECK(authFromText("WPA3SAE", true) == Auth::Enterprise);
    CHECK(authFromText("WPA2ENT") == Auth::Enterprise);
    CHECK(authFromText("") == Auth::Unknown);
    CHECK(authFromText("garbage") == Auth::Unknown);

    CHECK_EQ(authLabel(Auth::Open), std::string("open"));
    CHECK_EQ(authLabel(Auth::Wep), std::string("WEP"));
    CHECK_EQ(authLabel(Auth::Wpa2), std::string("WPA2"));
    CHECK_EQ(authLabel(Auth::Wpa3), std::string("WPA3"));
    CHECK_EQ(authLabel(Auth::Enterprise, "WPA2ENT"), std::string("WPA2-Enterprise"));
    CHECK_EQ(authLabel(Auth::Enterprise, "WPA3SAE"), std::string("WPA3-Enterprise"));
    CHECK_EQ(authLabel(Auth::Enterprise, "EAP"), std::string("Enterprise"));
    CHECK_EQ(authLabel(Auth::Unknown), std::string("?"));

    CHECK(isOpen(Auth::Open));
    CHECK(!isOpen(Auth::Wpa2));
}

// ---- 5. profile parsing -------------------------------------------------------------------

static void testProfile()
{
    const Profile p = parseProfile(kProfileXml);
    CHECK_EQ(p.name, std::string("Cafe & Bar"));
    CHECK_EQ(p.ssid, std::string("Cafe & Bar")); // entity decoded, not the raw &amp;
    CHECK_EQ(p.authText, std::string("WPA2PSK"));
    CHECK_EQ(p.cipherText, std::string("AES"));
    CHECK(p.auth == Auth::Wpa2);
    CHECK(p.hasKey);
    CHECK(p.keyProtected);
    CHECK(!p.oneX);
    CHECK_EQ(p.keyMaterial, std::string("01000000D08C9DDF")); // still the encrypted blob

    // A hex-only SSID (a non-ASCII network name).
    const std::string hexProfile =
        "<WLANProfile><name>hexnet</name><SSIDConfig><SSID><hex>E4B8AD</hex></SSID></SSIDConfig>"
        "<MSM><security><authEncryption><authentication>open</authentication>"
        "<encryption>none</encryption></authEncryption></security></MSM></WLANProfile>";
    const Profile h = parseProfile(hexProfile);
    CHECK_EQ(h.ssid, std::string("\xE4\xB8\xAD"));
    CHECK(h.auth == Auth::Open);
    CHECK(!h.hasKey);

    // An open profile with no sharedKey at all.
    const std::string openProfile =
        "<WLANProfile><name>Free</name><SSIDConfig><SSID><name>Free</name></SSID></SSIDConfig>"
        "<MSM><security><authEncryption><authentication>open</authentication>"
        "<encryption>none</encryption></authEncryption></security></MSM></WLANProfile>";
    const Profile o = parseProfile(openProfile);
    CHECK_EQ(o.ssid, std::string("Free"));
    CHECK(!o.hasKey);
    CHECK(o.auth == Auth::Open);

    // Garbage in, no crash, empty out.
    const Profile g = parseProfile("not xml at all");
    CHECK_EQ(g.ssid, std::string(""));
    CHECK(g.auth == Auth::Unknown);
}

// ---- 6. password display ------------------------------------------------------------------

static void testPasswordDisplay()
{
    CHECK_EQ(passwordDisplay("hunter2", true, true), std::string("********"));
    CHECK_EQ(passwordDisplay("hunter2", true, false), std::string("hunter2"));
    CHECK_EQ(passwordDisplay("hunter2", false, true), std::string(""));
    CHECK_EQ(passwordDisplay("", true, false), std::string(""));
}

// ---- 7. signal mapping --------------------------------------------------------------------

static void testSignal()
{
    CHECK_NUM(qualityFromRssi(-50), 100);
    CHECK_NUM(qualityFromRssi(-75), 50);
    CHECK_NUM(qualityFromRssi(-100), 0);
    CHECK_NUM(qualityFromRssi(-90), 20);
    CHECK_NUM(qualityFromRssi(-30), 100); // clamped above
    CHECK_NUM(qualityFromRssi(-120), 0);  // clamped below
    CHECK_NUM(qualityFromRssi(0), 0);     // unknown
    CHECK_NUM(qualityFromRssi(10), 0);    // nonsense

    CHECK_NUM(bandFromFrequency(2412), 2400u);
    CHECK_NUM(bandFromFrequency(2484), 2400u);
    CHECK_NUM(bandFromFrequency(5180), 5000u);
    CHECK_NUM(bandFromFrequency(5955), 6000u);
    CHECK_NUM(bandFromFrequency(0), 0u);
    CHECK_NUM(bandFromFrequency(1000), 0u);

    CHECK_EQ(bandText(2400), std::string("2.4 GHz"));
    CHECK_EQ(bandText(5000), std::string("5 GHz"));
    CHECK_EQ(bandText(6000), std::string("6 GHz"));
    CHECK_EQ(bandText(0), std::string(""));

    CHECK_NUM(channelFromFrequency(2412), 1u);
    CHECK_NUM(channelFromFrequency(2437), 6u);
    CHECK_NUM(channelFromFrequency(2472), 13u);
    CHECK_NUM(channelFromFrequency(2484), 14u);
    CHECK_NUM(channelFromFrequency(5180), 36u);
    CHECK_NUM(channelFromFrequency(5745), 149u);
    CHECK_NUM(channelFromFrequency(5955), 1u);
    CHECK_NUM(channelFromFrequency(0), 0u);

    CHECK_EQ(rateText(866000), std::string("866 Mbps"));
    CHECK_EQ(rateText(54000), std::string("54 Mbps"));
    CHECK_EQ(rateText(500), std::string("500 kbps"));
    CHECK_EQ(rateText(0), std::string(""));
}

// ---- 8. filtering -------------------------------------------------------------------------

static void testFilter()
{
    CHECK(matchesFilter("MyNet", ""));
    CHECK(matchesFilter("MyNet", "mynet"));
    CHECK(matchesFilter("MyNet", "MyN"));
    CHECK(matchesFilter("MyNet", "net"));
    CHECK(!matchesFilter("MyNet", "xyz"));
    CHECK(matchesFilter("", ""));
    CHECK(!matchesFilter("", "a"));
    CHECK(matchesFilter("中网络", "中"));
}

// ---- 9. ordering and merging --------------------------------------------------------------

static void testOrdering()
{
    std::vector<Network> in;
    in.push_back(net("weak-visible", -85, false, true));
    in.push_back(net("saved-away", 0, true, false));
    in.push_back(net("connected", -60, true, true, true));
    in.push_back(net("strong-visible", -45, false, true));
    in.push_back(net("saved-near", -70, true, true));

    const Settings st;
    const std::vector<Network> rows = buildRows(in, st, "");
    CHECK_NUM(rows.size(), 5u);
    CHECK_EQ(rows[0].ssid, std::string("connected"));
    CHECK_EQ(rows[1].ssid, std::string("saved-near")); // saved group, stronger first
    CHECK_EQ(rows[2].ssid, std::string("saved-away")); // saved, no signal: last in group
    CHECK_EQ(rows[3].ssid, std::string("strong-visible"));
    CHECK_EQ(rows[4].ssid, std::string("weak-visible"));

    // The filter keeps only what matches, and the connected row is not
    // exempt: the spec's "always keep the current connection" is the
    // caller's job, not the filter's.
    const std::vector<Network> filtered = buildRows(in, st, "visible");
    CHECK_NUM(filtered.size(), 2u);

    // Hidden networks follow the setting.
    std::vector<Network> hidden;
    Network h = net("", -50, false, true);
    h.hidden = true;
    h.bssid = "aa:bb:cc:dd:ee:ff";
    hidden.push_back(h);
    Settings noHidden;
    noHidden.includeHidden = false;
    CHECK_NUM(buildRows(hidden, noHidden, "").size(), 0u);
    CHECK_NUM(buildRows(hidden, st, "").size(), 1u);
}

static void testMerge()
{
    // Two BSSIDs of one SSID: one row, the stronger signal.
    std::vector<Network> in;
    Network a = net("Home", -80, false, true);
    a.bssid = "aa:aa:aa:aa:aa:aa";
    a.frequency = 2412;
    Network b = net("Home", -50, false, true);
    b.bssid = "bb:bb:bb:bb:bb:bb";
    b.frequency = 5180;
    in.push_back(a);
    in.push_back(b);
    std::vector<Network> merged = mergeBySsid(in);
    CHECK_NUM(merged.size(), 1u);
    CHECK_EQ(merged[0].bssid, std::string("bb:bb:bb:bb:bb:bb"));
    CHECK_NUM(merged[0].frequency, 5180u);

    // A profile plus a scan hit: one row, saved and visible both set.
    std::vector<Network> both;
    both.push_back(net("Home", 0, true, false));
    both.push_back(net("Home", -55, false, true));
    merged = mergeBySsid(both);
    CHECK_NUM(merged.size(), 1u);
    CHECK(merged[0].saved);
    CHECK(merged[0].visible);
    CHECK_NUM(merged[0].rssi, -55);

    // A connected entry keeps its own BSSID and rates even when a stronger
    // scan hit for the same SSID arrives.
    std::vector<Network> conn;
    Network c = net("Work", -70, true, true, true);
    c.bssid = "cc:cc:cc:cc:cc:cc";
    c.rxKbps = 866000;
    Network s = net("Work", -40, false, true);
    s.bssid = "dd:dd:dd:dd:dd:dd";
    conn.push_back(c);
    conn.push_back(s);
    merged = mergeBySsid(conn);
    CHECK_NUM(merged.size(), 1u);
    CHECK(merged[0].connected);
    CHECK_EQ(merged[0].bssid, std::string("cc:cc:cc:cc:cc:cc"));
    CHECK_NUM(merged[0].rssi, -70); // the connected entry's own signal
    CHECK_EQ(rateText(merged[0].rxKbps), std::string("866 Mbps"));

    // A revealed password survives the merge from either side.
    std::vector<Network> pw;
    Network p1 = net("Home", 0, true, false);
    p1.password = "secret";
    p1.passwordKnown = true;
    pw.push_back(p1);
    pw.push_back(net("Home", -55, false, true));
    merged = mergeBySsid(pw);
    CHECK(merged[0].passwordKnown);
    CHECK_EQ(merged[0].password, std::string("secret"));

    // Distinct SSIDs stay distinct.
    std::vector<Network> distinct;
    distinct.push_back(net("A", -50, false, true));
    distinct.push_back(net("B", -50, false, true));
    CHECK_NUM(mergeBySsid(distinct).size(), 2u);
}

// ---- 10. hints ----------------------------------------------------------------------------

static void testHints()
{
    CHECK(hintFor(true, true, 5) == Hint::NoAdapter);   // the adapter wins
    CHECK(hintFor(false, true, 5) == Hint::ServiceDown);
    CHECK(hintFor(false, false, 0) == Hint::NoResults);
    CHECK(hintFor(false, false, 3) == Hint::None);

    CHECK_EQ(hintTitle(Hint::NoAdapter, true), std::string("没有无线网卡"));
    CHECK_EQ(hintTitle(Hint::NoAdapter, false), std::string("No wireless adapter"));
    CHECK_EQ(hintTitle(Hint::ServiceDown, true), std::string("WLAN AutoConfig 服务未运行"));
    CHECK_EQ(hintTitle(Hint::NoResults, false), std::string("No matching networks"));
    CHECK_EQ(hintTitle(Hint::None, true), std::string(""));
    CHECK(!hintSubtitle(Hint::NoAdapter, true).empty());
    CHECK(!hintSubtitle(Hint::ServiceDown, false).empty());
    CHECK(!hintSubtitle(Hint::NoResults, true).empty());
}

// ---- 11. row text and connectability ------------------------------------------------------

static void testRowText()
{
    Network n = net("Home", -60, true, true, true);
    n.frequency = 5180;
    n.rxKbps = 866000;
    const std::string sub = rowSubtitle(n, false, true);
    CHECK(sub.find("5 GHz") != std::string::npos);
    CHECK(sub.find("ch 36") != std::string::npos);
    CHECK(sub.find("80%") != std::string::npos);
    CHECK(sub.find("866 Mbps") != std::string::npos);
    CHECK(sub.find("WPA2") != std::string::npos);
    CHECK(sub.find("connected") != std::string::npos);

    // Unknowns are left out, not printed as zero.
    Network bare;
    bare.ssid = "Bare";
    const std::string bareSub = rowSubtitle(bare, false, true);
    CHECK_EQ(bareSub, std::string(""));

    // A saved network out of range says so.
    Network away = net("Away", 0, true, false);
    const std::string awaySub = rowSubtitle(away, false, true);
    CHECK(awaySub.find("saved") != std::string::npos);
    CHECK(awaySub.find("out of range") != std::string::npos);

    // A revealed password shows masked or plainly.
    Network withPw = net("Home", -60, true, true);
    withPw.password = "hunter2";
    withPw.passwordKnown = true;
    CHECK(rowSubtitle(withPw, false, true).find("********") != std::string::npos);
    CHECK(rowSubtitle(withPw, false, false).find("hunter2") != std::string::npos);

    CHECK_EQ(rowTitle(net("Home", -60, false, true), false), std::string("Home"));
    Network hidden;
    hidden.bssid = "aa:bb:cc:dd:ee:ff";
    CHECK_EQ(rowTitle(hidden, false), std::string("(hidden network)"));
    CHECK_EQ(rowId(hidden), std::string("wifi:aa:bb:cc:dd:ee:ff"));
    CHECK_EQ(rowId(net("Home", -60, false, true)), std::string("wifi:Home"));
}

static void testConnect()
{
    CHECK(canConnect(net("Saved", 0, true, false)));            // a profile
    Network open = net("Open", -50, false, true);
    open.auth = Auth::Open;
    CHECK(canConnect(open));                                     // visible and open
    CHECK(!canConnect(net("Locked", -50, false, true)));         // visible, encrypted, no profile
    CHECK(!canConnect(net("Here", -50, true, true, true)));      // already connected

    CHECK_EQ(connectRefusal(net("Here", -50, true, true, true), false), std::string("Already connected"));
    CHECK_EQ(connectRefusal(net("Locked", -50, false, true), false),
        std::string("Enter the password in Windows Settings"));
    CHECK(!connectRefusal(net("Saved", 0, true, false), true).empty());
}

// ---- 12. the confirmation step ------------------------------------------------------------

static void testForgetStep()
{
    // The plain list is not a confirmation.
    CHECK_EQ(forgetTarget(""), std::string(""));
    CHECK_EQ(forgetTarget("HJJ"), std::string(""));
    CHECK_EQ(forgetTarget("forget"), std::string(""));      // no trailing space
    CHECK_EQ(forgetTarget("forget "), std::string(""));     // no SSID
    CHECK_EQ(forgetTarget("forget   "), std::string(""));   // only spaces
    CHECK_EQ(forgetTarget("Forget HJJ"), std::string(""));  // case-sensitive: a search
    CHECK_EQ(forgetTarget("FORGET HJJ"), std::string(""));

    // A confirmation, with the SSID kept whole (spaces included).
    CHECK_EQ(forgetTarget("forget HJJ"), std::string("HJJ"));
    CHECK_EQ(forgetTarget("forget My Cafe Net"), std::string("My Cafe Net"));
    CHECK_EQ(forgetTarget("forget HJJ   "), std::string("HJJ")); // trailing spaces trimmed
    CHECK_EQ(forgetTarget("forget  HJJ"), std::string("HJJ"));   // extra space after the prefix
    CHECK_EQ(forgetTarget("forget 中文网络"), std::string("中文网络"));
    // A network actually named "forget me" is still reachable.
    CHECK_EQ(forgetTarget("forget forget me"), std::string("forget me"));

    // Round trip: the query the plugin asks the host to set is the one it
    // reads back as a confirmation for that SSID.
    CHECK_EQ(forgetQuery("HJJ"), std::string("forget HJJ"));
    CHECK_EQ(forgetTarget(forgetQuery("HJJ")), std::string("HJJ"));
    CHECK_EQ(forgetTarget(forgetQuery("My Cafe Net")), std::string("My Cafe Net"));
    CHECK_EQ(forgetTarget(forgetQuery("forget me")), std::string("forget me"));
    CHECK_EQ(forgetTarget(forgetQuery("中文")), std::string("中文"));
    // An SSID with trailing spaces survives the round trip only trimmed; that
    // is the same normalisation the profile list applies.
    CHECK_EQ(forgetTarget(forgetQuery("HJJ ")), std::string("HJJ"));
}

// ---- 13. settings -------------------------------------------------------------------------

static void testSettings()
{
    const Settings def = readSettings(winshun::Json(winshun::Json::Object {}));
    CHECK(def.includeHidden);
    CHECK(def.showPasswordMasked);

    winshun::Json::Object o;
    o["includeHidden"] = false;
    o["showPasswordMasked"] = false;
    const Settings set = readSettings(winshun::Json(std::move(o)));
    CHECK(!set.includeHidden);
    CHECK(!set.showPasswordMasked);

    // A wrong type falls back to the default rather than throwing.
    winshun::Json::Object bad;
    bad["includeHidden"] = "yes";
    bad["showPasswordMasked"] = 3;
    const Settings fallback = readSettings(winshun::Json(std::move(bad)));
    CHECK(fallback.includeHidden);
    CHECK(fallback.showPasswordMasked);
}

// ---- main ---------------------------------------------------------------------------------

int main()
{
    testXmlText();
    testXmlDecode();
    testHex();
    testAuth();
    testProfile();
    testPasswordDisplay();
    testSignal();
    testFilter();
    testOrdering();
    testMerge();
    testHints();
    testRowText();
    testConnect();
    testForgetStep();
    testSettings();

    std::printf("%d checks, %d failed\n", g_checks, g_failed);
    return g_failed == 0 ? 0 : 1;
}
