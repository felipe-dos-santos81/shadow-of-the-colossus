#!/usr/bin/env python3
"""S7 RE gates R1 + R3 (static, read-only): the idol placements, the spawn, the probes.

stdlib only (+ the vendored Oodle dylib through tools/re/stage/spawn_probe.py, personal use).

  xref  <eboot.elf> [prefix ...]   code/data references to strings (default: the
                                   BossStatue_Whole_* / BossStatus_Alive_* family)
  place [--out JSON]               compose tools/re/s7/shrine_placements.json from the
                                   shipped data (needs SOTC_GAME_ROOT and SOTC_LIB)

xref: VA = file offset - 0x4000 (re-toolchain.md). Code refs = RIP-relative LEA/MOV
(REX 48/4c + 8d/8b + modrm mod=00 rm=101); data refs = R_X86_64_RELATIVE (type 8)
relocations whose addend is the string's VA.

place: the 16 `<Boss>_Statue_Whole` entity records of
scripts/shadow/world/cathedral/_cmn/cathedral_int.cgpr (spawn_probe's measured record layout:
4x3 row-vector transform, rows 0-2 = the local axes' images, row 3 = translation) carry each
idol's position and its model reference; the cgpr frame **is** the cathedral glbs' frame (no
offset, no mirror — s7-recon.md R1). yaw_deg is the app's +Y yaw (0 faces +Z, 90 faces +X):
yaw = atan2(-row0.z, row0.x) — the negative of spawn_probe's printed `yaw`.
The spawn is misc/playerstartpositions.txt's cathedral_int.sgpr start moved into that frame by
home.cgpr's `level_setup` translation, dropped to the floor under it.
"""
import json, math, os, re, struct, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'stage'))
sys.path.insert(0, HERE)

BASE = 0x4000
TEXT_END = 0x1593978  # PT_LOAD[0] filesz: code refs are only scanned inside it

CATHEDRAL = 'scripts/shadow/world/cathedral/_cmn/cathedral_int.cgpr'
HOME = 'scripts/shadow/world/home/_cmn/home.cgpr'
STARTS = 'misc/playerstartpositions.txt'
START_KEY = 'world/cathedral/cathedral_int.sgpr'
INTERIOR = 'models/shadow/levels/cathedral/_cmn/cathedral_interior.glb'
PLAYER_COL = 'models/shadow/levels/cathedral/_cmn/cathedral_interior_collision_stairs_player_collision.glb'

# authored (builder) values — labelled `source: authored` in the JSON and s7-recon.md
MONO = {'glb': 'models/shadow/characters/mono/_cmn/mono.glb', 'clip': 'girl_a_death_loop',
        'seconds': 0.0, 'xz': (0.0, 23.0), 'lift': 0.10, 'yaw_deg': 0.0}
SPAWN_YAW = 0.0  # the start records carry no yaw: face the altar (+Z); the wake ends at 2.5 deg
FLOW = {'hold_s': 2.0, 'fade_out_s': 1.0, 'fade_in_s': 1.5, 'colour': [1, 1, 1], 'source': 'authored'}
PROBES = {'altar_step': [0.0, 20.0], 'hall': [0.0, -20.0], 'stair': [0.0, -68.0],
          'wall': {'from': [0.0, -13.0], 'to': [14.0, -13.0]}}


# ---------------------------------------------------------------- xref ----

def cstrings(elf, prefixes):
    out = {}
    for p in prefixes:
        for m in re.finditer(re.escape(p.encode()) + rb'[\x20-\x7e]*\x00', elf):
            if elf[m.start() - 1] == 0:
                out[m.start() - BASE] = m.group()[:-1].decode()
    return out


def code_refs(elf, targets):
    text = elf[BASE:BASE + TEXT_END]
    refs = {t: [] for t in targets}
    for m in re.finditer(rb'[\x48\x4c][\x8d\x8b][\x05\x0d\x15\x1d\x25\x2d\x35\x3d]', text, re.S):
        i = m.start()
        t = i + 7 + struct.unpack_from('<i', text, i + 3)[0]
        if t in refs:
            refs[t].append(i)
    return refs


def fn_start(elf, va):  # nearest preceding push rbp; mov rbp,rsp
    return elf.rfind(b'\x55\x48\x89\xe5', BASE, BASE + va + 1) - BASE


def data_refs(elf, va):
    out, i, pat = [], 0, struct.pack('<Q', va)
    while True:
        i = elf.find(pat, i)
        if i < 0:
            return out
        if i >= 16 and struct.unpack_from('<Q', elf, i - 8)[0] & 0xffffffff == 8:
            out.append(struct.unpack_from('<Q', elf, i - 16)[0])
        i += 1


def cmd_xref(argv):
    elf = open(argv[0], 'rb').read()
    prefixes = argv[1:] or ['BossStatue_Whole_', 'BossStatus_Alive_', 'HOME_STATUE_COMMON']
    strs = cstrings(elf, prefixes)
    refs = code_refs(elf, strs)
    for va, s in sorted(strs.items()):
        c = refs[va]
        print('%-32s str=%#x code=%d %s data=%s' % (
            s, va, len(c), ' '.join('%#x(fn~%#x)' % (h, fn_start(elf, h)) for h in c[:6]),
            ' '.join('%#x' % r for r in data_refs(elf, va)[:4]) or '-'))


# --------------------------------------------------------------- place ----

def env_dir(name):
    d = os.environ.get(name)
    if not d or not os.path.isdir(d):
        sys.exit('%s is unset or not a directory' % name)
    return d


def yaw_of(rows):
    return math.degrees(math.atan2(-rows[0][2], rows[0][0]))


def model_glb(lib, ref):
    """'$/models/.../****/name[_8hex].cmdl' -> the converted 'models/.../_cmn/name.glb'."""
    rel = re.sub(r'_[0-9a-f]{8}$', '', ref[2:-5]).replace('****', '_cmn') + '.glb'
    if not os.path.isfile(os.path.join(lib, rel)):
        sys.exit('converted glb missing: %s (ref %s)' % (rel, ref))
    return rel


def record_refs(body, recs, i):
    end = recs[i + 1]['offset'] if i + 1 < len(recs) else len(body)
    return [m.decode() for m in re.findall(rb'\$/models/[\x21-\x7e]+?\.cmdl', body[recs[i]['name_end']:end])]


def floors(tris, x, z):
    ys = []
    for a, b, c in tris:
        d = (b[0] - a[0]) * (c[2] - a[2]) - (c[0] - a[0]) * (b[2] - a[2])
        if abs(d) < 1e-9:
            continue
        u = ((x - a[0]) * (c[2] - a[2]) - (c[0] - a[0]) * (z - a[2])) / d
        v = ((b[0] - a[0]) * (z - a[2]) - (x - a[0]) * (b[2] - a[2])) / d
        if u >= 0 and v >= 0 and u + v <= 1:
            ys.append(a[1] + u * (b[1] - a[1]) + v * (c[1] - a[1]))
    return sorted(ys)


def idols(game, lib):
    import spawn_probe as sp
    doc, body = sp.parse_cgpr(os.path.join(game, CATHEDRAL))
    recs = doc['records']
    out, broken = [], None
    for i, r in enumerate(recs):
        if not r['name'].endswith('_Statue_Whole'):
            continue
        tr = r['transform']
        refs = record_refs(body, recs, i)
        item = {'slot': len(out) + 1, 'glb': model_glb(lib, refs[0]),
                'p': [round(v, 4) for v in tr['translation']], 'yaw_deg': round(yaw_of(tr['rows']), 3),
                'source': 'data', 'record': r['name'], 'offset': '%#x' % r['offset']}
        print('idol slot=%(slot)d %(record)s @%(offset)s p=%(p)s yaw=%(yaw_deg)s %(glb)s' % item)
        if not out:  # slot 1 (Valus) is the broken idol: the next two records, Broke + skirt
            parts = [(recs[j], record_refs(body, recs, j)[-1]) for j in (i + 1, i + 2)]
            broken = {'slot': 1, 'glbs': [model_glb(lib, ref) for _, ref in parts],
                      'ps': [[round(v, 4) for v in q['transform']['translation']] for q, _ in parts],
                      'yaw_deg': round(yaw_of(parts[0][0]['transform']['rows']), 3), 'source': 'data',
                      'records': ['%s@%#x' % (q['name'], q['offset']) for q, _ in parts]}
            print('broken %s' % broken)
        out.append(item)
    if len(out) != 16:
        sys.exit('expected 16 *_Statue_Whole records, found %d' % len(out))
    return out, broken


def spawn(game, lib):
    import spawn_probe as sp
    line = next(l for l in open(os.path.join(game, STARTS)) if START_KEY in l)
    world = [float(v) for v in line.split()[-3:]]
    doc, _ = sp.parse_cgpr(os.path.join(game, HOME))
    origin = next(r for r in doc['records'] if r['name'] == 'level_setup')['transform']['translation']
    x, z = world[0] - origin[0], world[2] - origin[2]
    y_rec = world[1] - origin[1]
    below = [y for y in floors(sp.glb_triangles(os.path.join(lib, INTERIOR)), x, z) if y <= y_rec]
    if not below or not 8.99 <= below[-1] <= 11.0:  # the 9-11 m band, 1 cm rounding
        sys.exit('spawn floor out of the 9-11 m band: %s' % below)
    print('spawn world=%s level_setup=%s -> (%.3f, %.3f, %.3f) floor=%.3f' % (
        world, [round(v, 3) for v in origin], x, y_rec, z, below[-1]))
    return {'p': [round(x, 3) + 0.0, round(below[-1], 3), round(z, 3)], 'yaw_deg': SPAWN_YAW,
            'source': 'data', 'yaw_source': 'authored', 'record': line.split()[0].lstrip('+')}


def mono(lib):
    import spawn_probe as sp
    x, z = MONO['xz']
    top = floors(sp.glb_triangles(os.path.join(lib, INTERIOR)), x, z)[-1]
    print('mono altar top=%.3f at (%.1f, %.1f)' % (top, x, z))
    return {'glb': MONO['glb'], 'clip': MONO['clip'], 'seconds': MONO['seconds'],
            'p': [x, round(top + MONO['lift'], 4), z], 'yaw_deg': MONO['yaw_deg'], 'source': 'authored'}


def probes(lib):
    import spawn_probe as sp
    tris = sp.glb_triangles(os.path.join(lib, INTERIOR)) + sp.glb_triangles(os.path.join(lib, PLAYER_COL))
    for k in ('altar_step', 'hall', 'stair'):
        ys = floors(tris, *PROBES[k])
        print('probe %s xz=%s lowest=%.3f surfaces=%d max=%.3f' % (k, PROBES[k], ys[0], len(ys), ys[-1]))
    w = PROBES['wall']
    print('probe wall to=%s surfaces=%s' % (w['to'], [round(y, 2) for y in floors(tris, *w['to'])][:6]))
    return PROBES


def cmd_place(argv):
    import wake_clip
    game, lib = env_dir('SOTC_GAME_ROOT'), env_dir('SOTC_LIB')
    out = argv[argv.index('--out') + 1] if '--out' in argv else os.path.join(HERE, 'shrine_placements.json')
    items, broken = idols(game, lib)
    rows, w = wake_clip.analyse(os.path.join(lib, 'models/shadow/characters/wander/_cmn/wander.glb'))
    if not w['complete']:
        sys.exit('no wake chain found')
    doc = {'idols': items, 'broken': broken, 'mono': mono(lib), 'spawn': spawn(game, lib),
           'wake': {'clips': w['wake'], 'seconds': round(w['seconds'], 3), 'source': 'data',
                    'root_series': any(r['root_series'] for r in rows if r['clip'] in w['wake']), 'pelvis_m': [round(w['start_pelvis_m'], 2), round(w['end_pelvis_m'], 2)],
                    'end_heading_deg': round(w['end_heading_deg'], 1)},
           'flow': FLOW, 'probes': probes(lib)}
    with open(out, 'w') as f:
        json.dump(doc, f, indent=1)
        f.write('\n')
    print('wrote %s idols=%d wake=%s' % (out, len(items), ','.join(w['wake'])))


if __name__ == '__main__':
    cmds = {'xref': cmd_xref, 'place': cmd_place}
    if len(sys.argv) < 2 or sys.argv[1] not in cmds:
        sys.exit(__doc__)
    cmds[sys.argv[1]](sys.argv[2:])
