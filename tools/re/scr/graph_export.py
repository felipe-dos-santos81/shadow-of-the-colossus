#!/usr/bin/env python3
"""Export one colossus's SCR mode graph from out/re/eboot.elf as `sotc-scr-graph/0` JSON (S12 Task 2).

Read-only on the elf; stdlib only. Layouts are from the decompiled walker FUN_0082c080, evaluator
FUN_0082d9f0 and func runner FUN_0082bfb0 (docs/superpowers/specs/2026-10-23-s12-gaius-design.md §6,
docs/notes/s12-recon.md). Clips are written by name (lower-case stem of cp3shadow/anim/<name>.anb),
predicates and actions by function address ("0x832610", ruling R-G).

  graph_export.py export BOSS OUT.json [--elf PATH]      BOSS: any of the 16 colossi (tools/re/census/census.json)

S13 (docs/notes/s13-schemas.md) adds, all optional keys of the same `/0` format: control_defs, dprelate, attackdata,
more slowstatus fields, edge ref/speed/blend_out/from_ref and root kind. Recon: .superpowers/sdd/2026-10-23-s13-recon/
{graph,census16,additive}.
"""
import json, os, sys

sys.dont_write_bytecode = True
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path[:0] = [HERE]
import scrdb
from scrdb import q, u, f, cs
import predsem

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(HERE)))
DEFAULT_ELF = os.path.join(ROOT, "out", "re", "eboot.elf")
CAT = {}
EXIT = None                             # ALWAYS:scr_condition_def[0] 'Transition_Exit' (binary: walker sentinel)


def load(elf=DEFAULT_ELF):
    """read the elf and build the table catalog; fails closed when the elf is absent"""
    global EXIT
    if not os.path.isfile(elf):
        sys.exit("graph_export: %s is missing - run scripts/re0-acceptance.sh to unwrap eboot.bin first" % elf)
    scrdb.load(elf)
    CAT.clear(); CAT.update(scrdb.catalog())
    EXIT = q(0x22ce0f8)


def resolve(b, o):
    """handle {import*, packed} at b+o -> (objVA, idx, elemVA) or None (mirrors the binary's BEXTR 0xc12)"""
    imp = q(b + o)
    if not (0x1600000 <= imp < 0x4000000): return None
    obj = q(imp)
    if not (0x1600000 <= obj < 0x4000000) or not scrdb.tname(obj): return None
    oi = scrdb.objinfo(obj)
    idx = (u(b + o + 8) >> 18) & 0xfff
    if idx >= oi["count"]: return None
    return obj, idx, oi["data"] + idx * oi["size"]

def motion_orient_tables(boss):
    """every motion-orient table with a row whose from/to motion lies in the boss's own motion-def object (root
    +0x128), in table-address order (S13 fix 1, recon graph/findings.md §1: replaces the 3-boss dict)"""
    md = resolve(scrdb.objinfo(CAT[(boss, "scr_data_manager")])["data"], 0x128)[0]
    out = []
    for (n, t), va in sorted(CAT.items(), key=lambda kv: kv[1]):
        if t != "motion-orient": continue
        mo = scrdb.objinfo(va)
        if any((resolve(mo["data"] + i * mo["size"], o) or (0,))[0] == md for i in range(mo["count"]) for o in (0, 0x20)):
            out.append(n)
    return out

def hname(r):
    if not r: return None
    obj, idx, e = r
    oi = scrdb.objinfo(obj); lab = scrdb.elem_name(obj, idx)
    return "%s:%s[%d]%s" % (oi["name"], oi["type"], idx, (" '%s'" % lab) if lab else "")

def tn(r): return scrdb.objinfo(r[0])["type"] if r else None

def fval(b, o):
    w = u(b + o); v = f(b + o)
    return round(v, 6) if (w & 0x7f800000) and 1e-6 < abs(v) < 1e7 else None

# ---- condition clauses: 0x78 B; evaluator passes esi=+10 edx=+14 ecx=+18 xmm0..2=+1c/+20/+24 r8=+28 r9=+30
#      stack: +38, &+40, &+50, &+60 (handles).  +0 combiner (head only), +4 expected, +8 fn, +70 label.
def clause(b):
    d = dict(at=hex(b), fn=hex(q(b + 8)), comb=u(b), expect=u(b + 4), label=cs(q(b + 0x70)) or "")
    ints = {}
    for o in (0x10, 0x14, 0x18):
        w = u(b + o)
        if w: ints["+%02x" % o] = w
    fl = {}
    for o in (0x1c, 0x20, 0x24):
        if u(b + o): fl["+%02x" % o] = fval(b, o) if fval(b, o) is not None else hex(u(b + o))
    qs = {}
    for o in (0x28, 0x30, 0x38):
        v = q(b + o)
        if v: qs["+%02x" % o] = v if v < 0x80000000 else v - (1 << 64)
    hs = {}
    for o in (0x40, 0x50, 0x60):
        r = resolve(b, o)
        if r: hs["+%02x" % o] = r
    d.update(i=ints, fl=fl, q=qs, h=hs)
    return d

def cond_run(e):
    out = []
    while q(e + 8):
        out.append(clause(e)); e += 0x78
    return out

# ---- func_def actions: 0x70 B; runner passes esi=+08 edx=+0c xmm0..3=+10..+1c rcx=+20 r8=+28
#      r9=&+30, stack &+40 &+50 &+60 (handles).  list ends at fn 0.
def action(b):
    d = dict(at=hex(b), fn=hex(q(b)))
    ints = {"+%02x" % o: u(b + o) for o in (0x08, 0x0c) if u(b + o)}
    fl = {"+%02x" % o: (fval(b, o) if fval(b, o) is not None else hex(u(b + o))) for o in (0x10, 0x14, 0x18, 0x1c) if u(b + o)}
    qs = {"+%02x" % o: q(b + o) for o in (0x20, 0x28) if q(b + o)}
    hs = {}
    for o in (0x30, 0x40, 0x50, 0x60):
        r = resolve(b, o)
        if r: hs["+%02x" % o] = r
    d.update(i=ints, fl=fl, q=qs, h=hs)
    return d

def func_run(e):
    out = []
    while q(e):
        out.append(action(e)); e += 0x70
    return out

def dprelate_rows(r):
    """scr_dprelate handle -> the rows from its index to the table's end (data: 56 B, route +00, mask +08, value +10,
    object handle +18, condition handle +28; FUN_0082ddf0 takes the first match)"""
    oi = scrdb.objinfo(r[0])
    assert oi["size"] == 0x38, oi
    return [oi["data"] + i * 0x38 for i in range(r[1], oi["count"])]

def attack_rows(r):
    """scr_attackdata handle -> the rows from its index up to (not including) the first with no motion (+c0) and no
    FIX (+50): the terminator (census16 findings, data; 0x100 B each)"""
    oi = scrdb.objinfo(r[0])
    assert oi["size"] == 0x100, oi
    out = []
    for i in range(r[1], oi["count"]):
        e = oi["data"] + i * 0x100
        if not resolve(e, 0xc0) and not resolve(e, 0x50): break
        out.append(e)
    return out

def cdef_records(r):
    """control-def handle -> its 0x68 B records up to (not including) the opcode-1 terminator (R2, docs/notes/s12-recon.md)"""
    oi = scrdb.objinfo(r[0])
    assert oi["size"] == 0x68, oi
    out = []
    for i in range(r[1], oi["count"]):
        e = oi["data"] + i * 0x68
        if u(e) == 1: break
        out.append(e)
    return out

class Walk:
    def __init__(self, boss):
        self.boss = boss
        self.modes_obj = CAT[(boss, "scr_mode_status")]
        self.conds, self.funcs = {}, {}     # elemVA -> run
        self.dpr = {}                       # handle text -> [row VA] (a scr_dprelate a func or condition names)
        self.atk = {}                       # handle text -> [row VA] (a slowstatus attackdata run)
        self.modes = []
        # scr_data_manager root (binary: FUN_0082b1d0): +28 start mode, +38 retry start, +48 retry ctrl,
        # +58 death mode, +68 death control-mode, +b8 func list (callback FUN_0082bd00), +c8 per-frame func list
        self.root = scrdb.objinfo(CAT[(boss, "scr_data_manager")])["data"]
        self.root_lists = {}
        for o in (0xb8, 0xc8):
            r = resolve(self.root, o)
            self.root_lists["+%02x" % o] = r
            self.follow(r)
        oi = scrdb.objinfo(self.modes_obj)
        for i in range(oi["count"]):
            self.modes.append(self.mode(oi["data"] + i * oi["size"], i))

    def follow(self, r):
        if not r: return
        t = tn(r); e = r[2]
        if t == "scr_condition_def" and e not in self.conds:
            self.conds[e] = run = cond_run(e)
            for c in run:
                for h in c["h"].values(): self.follow(h)
        elif t == "scr_func_def" and e not in self.funcs:
            self.funcs[e] = run = func_run(e)
            for a in run:
                for h in a["h"].values(): self.follow(h)
        elif t == "scr_dprelate" and hname(r) not in self.dpr:
            self.dpr[hname(r)] = rows = dprelate_rows(r)
            for x in rows: self.follow(resolve(x, 0x28))

    def attack(self, r):
        """the slowstatus attack handle -> its attackdata run; the rows' conditions are walked (S13: their predicates
        must reach the strict loader)"""
        if not r or tn(r) != "scr_attackdata" or hname(r) in self.atk: return
        self.atk[hname(r)] = rows = attack_rows(r)
        for x in rows:
            for o in (0x40, 0x80): self.follow(resolve(x, o))

    def mode(self, b, i):
        m = dict(index=i, name=cs(q(b + 0xb0)), at=hex(b), fields={}, transitions=[])
        for o in (0x00, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x80, 0x90, 0xa0):
            r = resolve(b, o)
            if r:
                m["fields"]["+%02x" % o] = r
                self.follow(r)
        r = resolve(b, 0x00)                      # slowstatus: +20 = per-frame condition (FUN_0085a960 trigger)
        if r and tn(r) == "scr_slowstatus":
            for o in range(0, scrdb.objinfo(r[0])["size"] - 8, 8):
                self.follow(resolve(r[2], o))
        if r: self.attack(resolve(r[2], 0x10))     # any status type: locomotion.attack exports +10 for all of them
        h = resolve(b, 0x10)
        if h:
            e = h[2]
            while True:
                c = resolve(e, 0)
                if c and c[2] == EXIT: break
                self.follow(c)
                m["transitions"].append(dict(cond=c, target=resolve(e, 0x10), min_t2=f(e + 0x20)))
                e += 0x28
        return m

# ---- export one boss in the proposed S12 format (see findings.md section 4) ----------------------------------
def ref(r):
    """a resolved handle -> a stable reference: conditions 'TABLE:Cn', func lists 'TABLE:Fn', modes by index,
    controls/motions inline with their names"""
    if not r: return None
    obj, idx, e = r
    oi = scrdb.objinfo(obj); t = oi["type"]
    if t == "scr_condition_def": return "%s:C%d" % (oi["name"], idx)
    if t == "scr_func_def": return "%s:F%d" % (oi["name"], idx)
    if t == "scr_mode_status": return {"mode": idx, "name": cs(q(e + 0xb0))}
    if t == "script-ctrl-def": return {"ctrl": cs(q(e)), "bit": u(e + 8), "index": idx}
    if t == "motion-def":
        anb = hname(resolve(e, 0x128))
        return {"motion": "%s[%d]" % (oi["name"], idx), "name": cs(q(e + 8)), "clip": clip_name(anb.split("'")[1]) if anb and "'" in anb else anb}
    return hname(r)

def num_of(b, o):
    """a slowstatus word of unknown type: the float when it reads as one, else the integer"""
    v = fval(b, o)
    return v if v is not None else u(b + o)

def floats_of(r):
    if not r: return None
    obj, idx, e = r; n = scrdb.objinfo(obj)["size"] // 4
    return [round(f(e + 4 * k), 4) for k in range(n)]

def export(boss):
    w = Walk(boss)
    def args(spec, c):
        out = {}
        for k in (spec or {}):
            o = int(k[1:], 16)
            if k[0] == "i": out[k] = u(c + o)
            elif k[0] == "f": out[k] = round(f(c + o), 4)
            elif k[0] == "q": v = q(c + o); out[k] = -1 if v == 0xffffffffffffffff else hex(v)
            elif k[0] == "h": out[k] = ref(resolve(c, o))
        return out
    conds = {}
    for e, run in w.conds.items():
        obj = None
        for (name, typ), va in CAT.items():
            if typ == "scr_condition_def":
                oi = scrdb.objinfo(va)
                if oi["data"] <= e < oi["data"] + oi["count"] * 0x78: obj = (name, (e - oi["data"]) // 0x78); break
        cl = []
        for c in run:
            b = int(c["at"], 16); a = int(c["fn"], 16); sem = predsem.P.get(a, {})
            spec = sem.get("params")
            if spec is None:   # unread predicate: export every non-default slot raw
                # S13 fix 2: typed keys ("i10"); the raw "+10" keys were dropped by args()
                spec = {"i" + k[1:]: 1 for k in c["i"]}; spec.update({"f" + k[1:]: 1 for k in c["fl"]})
                spec.update({"q" + k[1:]: 1 for k, v in c["q"].items() if v != -1}); spec.update({"h" + k[1:]: 1 for k in c["h"]})
            cl.append({"pred": sem.get("name", "pred_" + c["fn"][2:]), "fn": c["fn"], "expect": bool(c["expect"]), "args": args(spec, b)})
        conds["%s:C%d" % obj] = {"label": run[0]["label"], "op": {1: "and", 2: "or"}.get(run[0]["comb"], "false"), "clauses": cl}
    funcs = {}
    for e, run in w.funcs.items():
        obj = None
        for (name, typ), va in CAT.items():
            if typ == "scr_func_def":
                oi = scrdb.objinfo(va)
                if oi["data"] <= e < oi["data"] + oi["count"] * 0x70: obj = (name, (e - oi["data"]) // 0x70); break
        lst = []
        for x in run:
            b = int(x["at"], 16); a = int(x["fn"], 16); sem = predsem.A.get(a, {})
            spec = sem.get("params")
            if spec is None:   # S13 fix 2, as for predicates
                spec = {"i" + k[1:]: 1 for k in x["i"]}; spec.update({"f" + k[1:]: 1 for k in x["fl"]}); spec.update({"h" + k[1:]: 1 for k in x["h"]})
            lst.append({"act": sem.get("name", "act_" + x["fn"][2:]), "fn": x["fn"], "args": args(spec, b)})
        funcs["%s:F%d" % obj] = lst
    modes = []
    for m in w.modes:
        F = m["fields"]
        ss = F.get("+00")
        sse = ss[2] if ss else None
        modes.append({
            "id": m["index"], "name": m["name"],
            "locomotion": None if not ss else {
                "row": hname(ss), "kind": u(sse) if u(sse) != 0xffffffff else -1, "p0c": u(sse + 0xc),
                "attack": hname(resolve(sse, 0x10)), "attack_trigger": ref(resolve(sse, 0x20)),
                "attain": floats_of(resolve(sse, 0x30)), "perception": floats_of(resolve(sse, 0x40)), "gaze": hname(resolve(sse, 0x50)),
                # S13 (data, census16 findings): +04 ref_player, +08 track, +78 judge volume, +60..+74/+98/+9c numbers
                "ref_player": u(sse + 4), "track": u(sse + 8), "judge": hname(resolve(sse, 0x78)),
                **{"p%02x" % o: num_of(sse, o) for o in (0x60, 0x64, 0x68, 0x6c, 0x70, 0x74, 0x98, 0x9c)}},
            "enter": {"actions": ref(F.get("+40")), "ctrl": ref(F.get("+50")), "wait_motion": ref(F.get("+60"))},
            "update": {"actions": ref(F.get("+20")), "ctrl": ref(F.get("+30"))},
            "exit": {"actions": ref(F.get("+70")), "ctrl_or_demo": ref(F.get("+80")), "wait_motion": ref(F.get("+90"))},
            "transitions": [{"when": ref(t["cond"]), "to": (ref(t["target"]) or {}).get("mode"),
                             **({"min_since_transition": round(t["min_t2"], 4)} if t["min_t2"] > 0 else {})} for t in m["transitions"]]})
    r = w.root
    root = {"start_mode": ref(resolve(r, 0x28)), "retry_start_mode": ref(resolve(r, 0x38)), "retry_ctrl": ref(resolve(r, 0x48)),
            "death_mode": ref(resolve(r, 0x58)), "death_control_mode": hname(resolve(r, 0x68)),
            "callback_actions": ref(resolve(r, 0xb8)), "every_frame_actions": ref(resolve(r, 0xc8)),
            "anger": floats_of(resolve(r, 0xd8)), "kind": u(r)}          # kind: +0x00 (guess, census16 Open)
    # the motion layer (data): a requested ctrl bit is matched by the current control-mode's control-assign rows
    # {+00 control-def, +10 script-ctrl-def, +20 motion-orient-def, +30 next control-mode}; the motion-orient
    # tables then give {from motion, orient label, to motion, +30, +34}.
    cmr = resolve(r, 0x68); control_modes = {}; cdefs = {}
    if cmr:
        cmo = scrdb.objinfo(cmr[0])
        for i in range(cmo["count"]):
            e = cmo["data"] + i * cmo["size"]
            rows = []
            for o in (0x00, 0x10):
                ca = resolve(e, o)
                if not ca: continue
                cao = scrdb.objinfo(ca[0])
                for j in range(cao["count"]):
                    x = cao["data"] + j * cao["size"]
                    nm = resolve(x, 0x30); wh = resolve(x, 0x00)
                    if wh: cdefs[hname(wh)] = wh
                    rows.append({"layer": "base" if o == 0 else "add", "when": hname(wh), "ctrl": ref(resolve(x, 0x10)),
                                 "orient": hname(resolve(x, 0x20)), "next_control_mode": nm[1] if nm else None})
            control_modes[i] = {"name": cs(q(e + 0x20)), "assign": rows}
    edges = []
    for name in motion_orient_tables(boss):
        mo = scrdb.objinfo(CAT[(name, "motion-orient")])
        for i in range(mo["count"]):
            e = mo["data"] + i * mo["size"]
            fr, to = ref(resolve(e, 0)), ref(resolve(e, 0x20))
            m = resolve(e, 0x20); m = m[2] if m else None       # the target motion-def (additive recon adddefs.py, binary)
            rf = resolve(m, 0x98) if m else None               # a motion-def, or another type (LEO's jump-velocity-def): then none
            rf = ref(rf) if rf and tn(rf) == "motion-def" else None
            fm = resolve(e, 0); fm = fm[2] if fm else None       # the from motion-def: its +0x98 tells two defs of one
            frf = resolve(fm, 0x98) if fm else None            # clip apart (KIRIN's neck dummy vs swing start, binary)
            frf = ref(frf) if frf and tn(frf) == "motion-def" else None
            edges.append({"table": name, "from": fr.get("clip") if isinstance(fr, dict) else fr, "orient": hname(resolve(e, 0x10)),
                          "to": to.get("clip") if isinstance(to, dict) else to, "p30": round(f(e + 0x30), 3), "p34": round(f(e + 0x34), 3),
                          # S13: +0x98 reference motion, +0x60 bit 1 speed-scaled, +0x218 blend-out frames
                          "ref": rf.get("clip") if isinstance(rf, dict) else rf, "speed": bool(m and u(m + 0x60) & 2),
                          "from_ref": frf.get("clip") if isinstance(frf, dict) else frf,
                          "blend_out": round(f(m + 0x218), 4) if m else 0.0})
    control_defs = {k: [cdef(e) for e in cdef_records(h)] for k, h in sorted(cdefs.items())}
    dprelate = {k: [{"mask": q_word(e + 8), "value": q_word(e + 0x10), "object": hname(resolve(e, 0x18)),
                     "cond": ref(resolve(e, 0x28)), "route": u(e)} for e in rows] for k, rows in sorted(w.dpr.items())}
    attackdata = {k: [attack_row(e) for e in rows] for k, rows in sorted(w.atk.items())}
    return {"format": "sotc-scr-graph/0", "control_modes": control_modes, "motion_graph": edges, "boss": boss, "source": "eboot.elf scr_data_manager %#x" % CAT[(boss, "scr_data_manager")],
            "root": root, "modes": modes, "conditions": dict(sorted(conds.items())), "actions": dict(sorted(funcs.items())),
            "control_defs": control_defs, "dprelate": dprelate, "attackdata": attackdata}


def q_word(va):
    v = q(va)
    return -1 if v == 0xffffffffffffffff else hex(v)

def finite(x):
    """a record float as a JSON number (a non-finite word is unused padding: 0)"""
    return round(x, 4) if x == x and abs(x) < 1e30 else 0.0

def cdef(e):
    """one 0x68 B control-def record (R2, binary): op +00, min +04, max +10, handle +38, mask +48, value +50, expect +58"""
    h = resolve(e, 0x38)
    if h and tn(h) == "scr_condition_def": sys.exit("graph_export: control-def record %#x names a condition" % e)
    return {"op": u(e), "min": finite(f(e + 4)), "max": finite(f(e + 0x10)), "handle": ref(h),
            "mask": q_word(e + 0x48), "value": q_word(e + 0x50), "expect": u(e + 0x58)}

def attack_row(e):
    """one 0x100 B attackdata row (census16 findings, data); names as attack_rows() of census16"""
    name = lambda r: (scrdb.elem_name(r[0], r[1]) or hname(r)) if r else None
    return {"label": cs(q(e)), "chain": hname(resolve(e, 0x08)), "chain_pct": round(f(e + 0x18), 3),
            "ctrl_modes": hname(resolve(e, 0x20)), "default": u(e + 0x30), "reselect_s": round(f(e + 0x34), 3),
            "sensing": u(e + 0x38), "cond": ref(resolve(e, 0x40)), "fix": name(resolve(e, 0x50)), "atk": name(resolve(e, 0x60)),
            "alt_atk": name(resolve(e, 0x70)), "attain": name(resolve(e, 0x90)), "alt_cond": ref(resolve(e, 0x80)),
            "issue": ref(resolve(e, 0xa0)), "issue2": ref(resolve(e, 0xb0)), "motion": ref(resolve(e, 0xc0)),
            "aim_bone": hname(resolve(e, 0xd0)), "aim_volume": hname(resolve(e, 0xe0)), "f0": round(f(e + 0xf0), 3),
            "cooldown_s": round(f(e + 0xf4), 3), "issue_arrived": u(e + 0xf8)}


def clip_name(s):
    """'anim/knight_A_boot.anb' -> 'knight_a_boot' (the file under cp3shadow/anim/)"""
    s = s.rsplit("/", 1)[-1]
    return (s[:-4] if s.endswith(".anb") else s).lower()


def main(argv):
    if len(argv) not in (4, 6) or argv[1] != "export" or (len(argv) == 6 and argv[4] != "--elf"):
        sys.exit(__doc__)
    load(argv[5] if len(argv) == 6 else DEFAULT_ELF)
    if (argv[2], "scr_data_manager") not in CAT:
        sys.exit("graph_export: unknown boss %r (have %s)" % (argv[2], ", ".join(sorted(n for n, t in CAT if t == "scr_data_manager"))))
    out = argv[3]
    data = json.dumps(export(argv[2]), indent=1, allow_nan=False)       # compute first: a raising export must not touch OUT
    os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
    tmp = out + ".tmp"
    with open(tmp, "w") as fh:
        fh.write(data)
    os.replace(tmp, out)


if __name__ == "__main__":
    main(sys.argv)
