"""E2E protocol driver for ra9fael.wifi (real WLAN, real process).

Speaks the resident JSON-RPC-over-stdio protocol to wifi.exe directly.
Usage: python run_e2e.py <case>   e.g. python run_e2e.py query
"""
import json, subprocess, sys, os, time, io
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8", errors="replace")

EXE = os.path.join(os.path.dirname(__file__), "..", "wifi.exe")

class Proc:
    def __init__(self, locale="zh"):
        self.p = subprocess.Popen([EXE], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                  stderr=subprocess.DEVNULL)
        self.send({"jsonrpc": "2.0", "id": 0, "method": "initialize",
                   "params": {"settings": {}, "locale": locale, "theme": "dark",
                              "appearance": {}, "pluginDir": ".", "dataDir": "."}})
        self.next_id = 1

    def send(self, obj):
        self.p.stdin.write((json.dumps(obj, ensure_ascii=False) + "\n").encode("utf-8"))
        self.p.stdin.flush()

    def read_until_id(self, want_id, timeout=15.0):
        """Read lines until a response with the wanted id. Returns (response, notifications)."""
        end = time.time() + timeout
        notes = []
        while time.time() < end:
            line = self.p.stdout.readline()
            if not line:
                raise RuntimeError("process closed stdout")
            try:
                m = json.loads(line.decode("utf-8"))
            except Exception:
                continue
            if m.get("id") == want_id and ("result" in m or "error" in m):
                return m, notes
            notes.append(m)
        raise TimeoutError(f"no response for id {want_id}")

    def call(self, method, params, timeout=15.0):
        i = self.next_id; self.next_id += 1
        self.send({"jsonrpc": "2.0", "id": i, "method": method, "params": params})
        return self.read_until_id(i, timeout)

    def query(self, text, locale=None):
        r, notes = self.call("query", {"queryId": self.next_id, "command": "wifi",
                                       "trigger": "wifi", "keyword": "wifi", "text": text,
                                       "raw": "wifi " + text, "limit": 200, "match": []})
        return r["result"]["items"], notes

    def action(self, action, item, text=""):
        r, notes = self.call("action", {"command": "wifi", "action": action, "text": text,
                                        "item": item, "trigger": "wifi", "files": []})
        return r["result"]["effects"], notes

    def shutdown(self):
        try:
            self.call("shutdown", {}, timeout=5)
        except Exception:
            pass
        self.p.stdin.close()
        return self.p.wait(timeout=10)

def brief(items):
    out = []
    for it in items:
        out.append({
            "id": it.get("id"),
            "title": it.get("title"),
            "subtitle": it.get("subtitle"),
            "valid": it.get("valid", True),
            "actions": it.get("actions"),
        })
    return out

def main():
    case = sys.argv[1] if len(sys.argv) > 1 else "query"
    p = Proc()
    try:
        if case == "query":
            items, notes = p.query("")
            print(json.dumps(brief(items), ensure_ascii=False, indent=1))
        elif case == "filter":
            items, _ = p.query(sys.argv[2] if len(sys.argv) > 2 else "imp")
            print(json.dumps(brief(items), ensure_ascii=False, indent=1))
        elif case == "connect":
            # find a saved-but-not-connected network and connect to it
            items, _ = p.query("")
            target = None
            for it in items:
                if it.get("id", "").startswith("wifi:profile:") and "connected" not in (it.get("subtitle") or ""):
                    target = it; break
            if not target:
                print("NO-SAVED-DISCONNECTED-TARGET"); return
            print("target:", target["title"])
            eff, _ = p.action("connect", target)
            print(json.dumps(eff, ensure_ascii=False))
            time.sleep(8)
            items2, _ = p.query("")
            print(json.dumps(brief(items2), ensure_ascii=False, indent=1))
        elif case == "disconnect":
            items, _ = p.query("")
            cur = next((it for it in items if "connected" in (it.get("subtitle") or "")
                        or "已连接" in (it.get("subtitle") or "")), None)
            if not cur:
                print("NO-CONNECTED-TARGET"); return
            print("target:", cur["title"])
            eff, _ = p.action("disconnect", cur)
            print(json.dumps(eff, ensure_ascii=False))
            time.sleep(6)
            items2, _ = p.query("")
            print(json.dumps(brief(items2), ensure_ascii=False, indent=1))
        elif case == "reconnect":
            items, _ = p.query("")
            target = next((it for it in items if it.get("id") == "wifi:HJJ"), None)
            if not target:
                print("NO-TARGET"); return
            print("target:", target["title"])
            eff, _ = p.action("connect", target)
            print(json.dumps(eff, ensure_ascii=False))
            time.sleep(10)
            items2, _ = p.query("")
            print(json.dumps(brief(items2), ensure_ascii=False, indent=1))
        elif case == "password":
            items, _ = p.query("HJJ")
            it = next(it for it in items if it.get("id") == "wifi:HJJ")
            eff, notes = p.action("password", it, "HJJ")
            print(json.dumps(eff, ensure_ascii=False))
            items2, _ = p.query("HJJ")
            print("after:", json.dumps(brief(items2), ensure_ascii=False))
        elif case == "copyxml":
            items, _ = p.query("HJJ")
            it = next(it for it in items if it.get("id") == "wifi:HJJ")
            eff, _ = p.action("copyXml", it, "HJJ")
            print(json.dumps(eff, ensure_ascii=False))
        elif case == "forget1":
            items, _ = p.query("forget HJJ")
            print(json.dumps(brief(items), ensure_ascii=False, indent=1))
        elif case == "connectopen":
            # connect to a visible open network with no profile (none expected here)
            items, _ = p.query("")
            target = next((it for it in items if it.get("id", "").startswith("wifi:")
                           and not it.get("id", "").startswith("wifi:profile:")
                           and "open" in (it.get("subtitle") or "") and it.get("actions")), None)
            if not target:
                print("NO-OPEN-VISIBLE-TARGET"); return
            print("target:", target["title"])
            eff, _ = p.action("connect", target)
            print(json.dumps(eff, ensure_ascii=False))
        else:
            print("unknown case", case)
    finally:
        print("exit:", p.shutdown(), file=sys.stderr)

if __name__ == "__main__":
    main()
