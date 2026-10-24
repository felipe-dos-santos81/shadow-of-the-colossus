"""Reader for the eboot's embedded SCR definition database (the original game's 'def' sheets).

Read-only on out/re/eboot.elf (a PS4 ELF, file offset = VA + 0x4000); stdlib only. Call load(path) first.
An object (table) header is {+0 name*, +8 hash, +0x10 type*, +0x18 count, +0x20 data*}; a type is
{+0 typename*, +0x18 u32 elemsize, +0x1c u32 nfields, +0x20 fieldlist*}. A *handle* inside an element is a
16-byte pair {import*, packed}: import* -> {obj*, hash}; packed lo16 = 1-based import slot, bits 18..29 = index.
"""
import struct

OFF = 0x4000
B = b""


def load(path):
    global B
    with open(path, "rb") as fh:
        B = fh.read()


def q(va): return struct.unpack_from("<Q", B, va + OFF)[0]
def u(va): return struct.unpack_from("<I", B, va + OFF)[0]
def f(va): return struct.unpack_from("<f", B, va + OFF)[0]


def cs(va):
    o = va + OFF
    if not (0x100000 <= va and o < len(B)): return None
    e = B.find(b"\0", o); t = B[o:e]
    if 3 <= len(t) < 200 and all(32 <= c < 127 for c in t): return t.decode()
    return None


def tname(obj):
    t = q(obj + 0x10)
    return cs(q(t)) if t > 0x100000 else None


def objinfo(obj):
    t = q(obj + 0x10)
    return dict(name=cs(q(obj)), type=cs(q(t)), size=u(t + 0x18), nf=u(t + 0x1c), count=q(obj + 0x18), data=q(obj + 0x20))


def elem_name(obj, i):
    """first printable string pointer inside element i (the def's label)"""
    oi = objinfo(obj)
    base = oi["data"] + i * oi["size"]
    for o in range(0, oi["size"], 8):
        v = q(base + o)
        if 0x1000000 <= v < 0x1600000:
            s = cs(v)
            if s and len(s) > 2 and (o == oi["size"] - 8 or o == 0): return s
    return None


def catalog():
    """{(table name, type name): table VA} for every def table in the data segment (first wins)."""
    lo, hi = 0x1604000, 0x1604000 + 0x17d3100
    cat, tcache = {}, {}
    for va in range(lo, hi - 0x28, 8):
        t = struct.unpack_from("<Q", B, va + OFF + 0x10)[0]
        if not (lo <= t < hi): continue
        n = struct.unpack_from("<Q", B, va + OFF + 0x18)[0]
        if not (0 < n < 20000): continue
        d = struct.unpack_from("<Q", B, va + OFF + 0x20)[0]
        if not (lo <= d < hi + 0x1000000): continue
        name = cs(struct.unpack_from("<Q", B, va + OFF)[0])
        if not name: continue
        if t not in tcache: tcache[t] = cs(q(t))
        if tcache[t]: cat.setdefault((name, tcache[t]), va)
    return cat
