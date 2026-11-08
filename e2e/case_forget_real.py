# -*- coding: utf-8 -*-
"""Real forget: first press -> setQuery confirm; confirm query -> delete row;
delete -> profile gone (verified against netsh). Then the profile is rebuilt
by the caller."""
import run_e2e as r

out = open("forget_result.txt", "w", encoding="utf-8")
p = r.Proc()
# step 1: forget press on the HJJ row
items, _ = p.query("")
it = next(x for x in items if x.get("id") == "wifi:HJJ")
eff, _ = p.action("forget", it)
out.write("step1 effects: %r\n" % (eff,))
# step 2: the host would re-query with "forget HJJ"
items2, _ = p.query("forget HJJ")
out.write("step2 rows: %r\n" % (r.brief(items2),))
# step 3: confirm -> real delete
confirm = items2[0]
eff2, _ = p.action("forget", confirm, "forget HJJ")
out.write("step3 effects: %r\n" % (eff2,))
# step 4: re-query — the profile should be gone
items3, _ = p.query("")
out.write("step4 ids: %r\n" % ([x.get("id") for x in items3],))
out.write("exit: %s\n" % p.shutdown())
out.close()
print(open("forget_result.txt", encoding="utf-8").read())
