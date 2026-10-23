#!/usr/bin/env python3
"""The colossus census (S10 Task 2, design D6/s6/s13.15): sixteen colossi from data alone.

Read-only on the game tree and the library; stdlib only. For each colossus it records the
converted live glb, the dead body/mound, the clip stem (found by skeleton containment in the
live glb), the `.anb` clip census (flag-clear for the S2 decoder, track types), the weakpoint
materials, the arena zone/level/cgpr set and start position, plus name-derived mechanic hints
(labelled as such). It also scans the audio banks by cue stem and checks the shipped sha256
(logs/game.sha256) of the colossus model dirs and cp3shadow/anim, reporting mismatches (the
known clobbered Valus files) without failing on them.

  census.py --game GAME --lib LIB [--sha game.sha256] [--out census.json] [--check]

Defaults: GAME=$SOTC_GAME_ROOT, LIB=$SOTC_LIB, SHA=<GAME>/../../logs/game.sha256.
--check exits 1 unless: 16 rows, every stem's skeleton coverage is 1.0, every arena level dir
and zone is present. Hash mismatches never fail it.
"""
import argparse, collections, glob, hashlib, json, os, re, struct, sys, time

sys.dont_write_bytecode = True
HERE = os.path.dirname(os.path.abspath(__file__))
RE_DIR = os.path.dirname(HERE)
sys.path[:0] = [os.path.join(RE_DIR, 'anb'), os.path.join(RE_DIR, 'stage')]
import parse2                      # the flag-clear .anb reader
import spawn_probe as sp           # cgpr decompress/read (arena scripts)

ANBSET = {0, 2, 3, 4, 5, 6, 12, 13, 14}   # AnbReader's track-type set (s4-recon.md section 2)
# Clip stem per colossus, confirmed (not assumed) by skeleton coverage in the live glb.
HYP = {1: 'minotaur_a', 2: 'boss_a', 3: 'knight_a', 4: 'kirin_a', 5: 'bird_a', 6: 'minotaur_b',
       7: 'eel_b', 8: 'yamori_b', 9: 'kame_a', 10: 'narga_a', 11: 'leo_a', 12: 'poseidon_a',
       13: 'snake_c', 14: 'cerberus_a', 15: 'minotaur_c', 16: 'buddha_a'}
# Name-derived mechanic hints over clip names (NOT verified behaviour).
MECH = [('flying', 'fly|flap|hover|gride|takeoff|landing|wing|hane'), ('swim/under', 'swim|dive|under|nori|dai'),
        ('bow/arrow', 'arrow|bow\\b|shot'), ('breath/beam', 'breath|beam|fire|charge|thunder|elec|lightning'),
        ('sand/burrow', 'sand|burrow|dig'), ('geyser/pulse', 'geyser|pulse'),
        ('climb struct', 'tower|hashigo|ladder|wall|ude|kune'), ('ride', 'nori|ride'),
        ('jump/dash', 'jump|dash|run|quickturn')]
FAM = [('death', 'death|dead|die'), ('stand/idle', '^(stand|standby|wait|boot)'), ('walk/run', 'walk|run|dash'),
       ('attack', 'atk|atack|attack|shot|fire'), ('damage/down', 'dmg|damage|down|struggle|faint'),
       ('refuse/shake', 'refuse|shake|angry|threat')]
# Arena-script tokens (name-derived hints): counts of identifiers containing these words.
ARENA_KW = ['underwater', 'water', 'sand', 'lava', 'fire', 'lightning', 'storm', 'climb', 'bow', 'torch',
            'fly', 'cloud', 'geyser', 'steam', 'swim', 'colossus', 'cutscene']
SKIP_GLB = r'collision|lod\d|emitter|hiza|hige|weapon|hahen|tooth|koura|eye_|null_|skirt|hashigo|meshemitter|stone|_geo|kosi'


def glbjson(p):
    with open(p, 'rb') as f:
        n = struct.unpack('<I', f.read(20)[12:16])[0]
        return json.loads(f.read(n))


def skbnames(p):
    d = open(p, 'rb').read()
    out = []
    for x in re.findall(rb'[A-Za-z0-9_]*Bone_\w+', d[d.find(b'.data'):]):
        x = x.decode()
        if x not in out:
            out.append(x)
    return out


def sha256(p):
    h = hashlib.sha256()
    with open(p, 'rb') as f:
        for b in iter(lambda: f.read(1 << 20), b''):
            h.update(b)
    return h.hexdigest()


def hash_check(G, shafile):
    """Shipped sha256 of the colossus model dirs and cp3shadow/anim only (the tree is 130k files)."""
    pre = ('game/models/shadow/characters/colossus_', 'game/cp3shadow/anim/')
    exp = {}
    for l in open(shafile):
        h, _, p = l.strip().partition('  ')
        if p.startswith(pre):
            exp[p] = h
    bad, missing = [], []
    for p, h in sorted(exp.items()):
        f = os.path.join(G, p[len('game/'):])
        if not os.path.isfile(f):
            missing.append(p)
        elif sha256(f) != h:
            bad.append(dict(path=p, expected=h, actual=sha256(f)))
    return dict(checked=len(exp), mismatched=bad, missing=missing)


def clipinfo(G, stem):
    out = []
    for f in sorted(glob.glob(f'{G}/cp3shadow/anim/{stem}_*.anb')):
        n = os.path.basename(f)[len(stem) + 1:-4]
        try:
            a = parse2.parse_anb(open(f, 'rb').read(), f)
            types = {r['type'] for r in a['track_rows']}
            out.append(dict(n=n, flagclear=True, frames=a['frames'], tracks=a['tracks'],
                            types_ok=types <= ANBSET, root=bool(a['root_keys'])))
        except ValueError:
            out.append(dict(n=n, flagclear=False))
    return out


def glb_stats(g):
    j = glbjson(g)
    return dict(glb=g, size=os.path.getsize(g), joints=sum(len(s['joints']) for s in j.get('skins', [])),
                meshes=len(j.get('meshes', [])), nodes={x.get('name') for x in j.get('nodes', [])})


def banks(G):
    """Audio banks by cue stem: the dominant non-generic name prefixes in each boss*.sxd."""
    out = {}
    for f in sorted(glob.glob(G + '/sound/shadow/boss*.sxd')):
        names = [x.decode() for x in re.findall(rb'[a-z][a-z0-9]*_[a-z0-9_]{2,}', open(f, 'rb').read(0x20000))]
        c = collections.Counter(n.split('_')[0] for n in names)
        out[os.path.basename(f)[:-4]] = dict(names=len(names), stems=c.most_common(6))
    return out


def arena_tokens(G, idx):
    c = collections.Counter()
    for f in glob.glob(f'{G}/scripts/shadow/world/arena_{idx:02d}/_cmn/*.cgpr'):
        b = sp.decompress(sp.read_bytes(f))[0]
        for m in re.findall(rb'[A-Za-z0-9_]{4,}', b):
            s = m.decode('latin1').lower()
            for k in ARENA_KW:
                if k in s:
                    c[k] += 1
    return dict(sorted(c.items()))


def census(G, L, shafile):
    cat = json.load(open(L + '/catalog.json'))['entries']
    skb = {os.path.basename(p)[:-4]: skbnames(p) for p in glob.glob(G + '/cp3shadow/skel/*.skb')}
    dead = {os.path.basename(p).replace('_dead_body.cgpr', '') for p in
            glob.glob(G + '/scripts/shadow/entities/dead_colossi/_cmn/*.cgpr')}
    start = {}
    for l in open(G + '/misc/playerstartpositions.txt'):
        m = re.search(r'arena_(\d\d)/arena_\d\d\.sgpr\s+(\S+) (\S+) (\S+)', l)
        if m:
            start[int(m.group(1))] = [float(m.group(i)) for i in (2, 3, 4)]
    zx = open(G + '/misc/zoneinfo/_cmn/shadowzones.cxml', 'rb').read()[3:].decode('latin1')
    zones = {n: (re.findall(r'inherit="([^"]*)"', a), re.findall(r'<Area name="([^"]+)"', b))
             for n, a, b in re.findall(r'<Zone name="([^"]+)"([^>]*)>(.*?)</Zone>', zx, re.S)}
    hc = hash_check(G, shafile)
    bad_by_path = {b['path'] for b in hc['mismatched']}
    rows = []
    for d in sorted(glob.glob(L + '/models/shadow/characters/colossus_??_*')):
        n = os.path.basename(d)
        if n.endswith('_deadbody'):
            continue
        idx = int(n.split('_')[1])
        r = dict(idx=idx, dir=n)
        cands = sorted((glb_stats(g) for g in glob.glob(d + '/**/*.glb', recursive=True)
                        if not re.search(SKIP_GLB, os.path.basename(g))),
                       key=lambda s: (s['joints'], s['size']), reverse=True)
        m = cands[0]
        r.update(glb=os.path.relpath(m['glb'], L), glb_mb=round(m['size'] / 1e6, 1), joints=m['joints'], meshes=m['meshes'])
        ent = [v for k, v in cat.items() if k.startswith(f'models/shadow/characters/{n}/')]
        r['conv'] = dict(ok=sum(v['status'] == 'ok' for v in ent), total=len(ent))
        wp = collections.Counter()
        for g in glob.glob(d + '/**/*.glb', recursive=True):
            if 'collision' in g:
                continue
            j = glbjson(g)
            mt = [x.get('name', '') for x in j.get('materials', [])]
            for me in j.get('meshes', []):
                for pr in me['primitives']:
                    nm = mt[pr['material']] if 'material' in pr else ''
                    if re.search('weak', nm, re.I):
                        wp[nm] += 1
        r['weak_glb_mats'] = dict(wp)
        r['weak_cmat'] = sorted(os.path.basename(p)[:-5] for p in
                                glob.glob(f'{G}/materials/shadow/characters/colossus_{idx:02d}/_ps4/*.cmat') if re.search('weak', p, re.I))
        dd = []
        for g in sorted(glob.glob(d + '_deadbody/**/*.glb', recursive=True)):
            if 'collision' in g:
                continue
            s = glb_stats(g)
            dd.append(dict(f=os.path.basename(g), mb=round(s['size'] / 1e6, 2), meshes=s['meshes'], skin=s['joints']))
        r['dead'] = dd
        r['mound'] = [x['f'] for x in dd if re.search('mound|dirt|stone', x['f'])]
        stem = HYP[idx]
        r['dead_cgpr'] = stem in dead
        sk = skb.get(stem, [])
        r.update(stem=stem, skb_names=len(sk), skb_cov=round(len([x for x in sk if x in m['nodes']]) / max(1, len(sk)), 3),
                 rivals=[(round(a, 2), k) for a, k in sorted(((len([x for x in v if x in m['nodes']]) / max(1, len(v)), k)
                                                              for k, v in skb.items() if k != stem and len(v) >= 20), reverse=True)[:2]])
        ci = clipinfo(G, stem)
        names = [c['n'] for c in ci]
        bones = [x for x in sk if not x.startswith('OptBone')]
        tc = collections.Counter(c['tracks'] for c in ci if c['flagclear'])
        r['track_counts'] = dict(tc.most_common(4))
        r['map_ok'] = all(n_ <= len(bones) and all(b in m['nodes'] for b in bones[:n_]) for n_ in tc if n_)
        r['sha_bad'] = [c['n'] for c in ci if f'game/cp3shadow/anim/{stem}_{c["n"]}.anb' in bad_by_path]
        r['clips'] = len(ci)
        r['flagset'] = sum(not c['flagclear'] for c in ci)
        r['types_bad'] = sum(1 for c in ci if c['flagclear'] and not c['types_ok'])
        r['root'] = sum(1 for c in ci if c.get('root'))
        r['death_clips'] = [x for x in names if re.search('death|dead|die', x)]
        r['fam'] = {k: sum(1 for x in names if re.search(rx, x)) for k, rx in FAM}
        r['objanim'] = len(glob.glob(f'{G}/cp3shadow/anim/objanim_{stem.split("_")[0]}_*.anb'))
        r['mech'] = {k: sorted({x for x in names if re.search(rx, x)})[:6] for k, rx in MECH if any(re.search(rx, x) for x in names)}
        a = f'arena_{idx:02d}'
        zk = [k for k in zones if k == a or k.startswith(a + '_')]
        zi, areas = zones.get(a, zones[zk[0]] if zk else ([], []))
        r['zone'] = dict(name=zk, inherit=zi, areas=len(areas), own=[x for x in areas if x.lower().startswith(a)])
        r['levels_dir'] = os.path.isdir(f'{G}/models/shadow/levels/{a}')
        r['levels_files'] = len(glob.glob(f'{G}/models/shadow/levels/{a}/_cmn/*'))
        r['world_scripts'] = sorted(os.path.basename(p) for p in glob.glob(f'{G}/scripts/shadow/world/{a}/_cmn/*.cgpr'))
        r['start'] = start.get(idx)
        r['arena_glb'] = len(glob.glob(f'{L}/models/shadow/levels/{a}/**/*.glb', recursive=True))
        r['arena_kw'] = arena_tokens(G, idx)
        rows.append(r)
    return dict(rows=rows, banks=banks(G), hashes=hc)


def check(c):
    errs = []
    if len(c['rows']) != 16:
        errs.append(f"{len(c['rows'])} rows, want 16")
    for r in c['rows']:
        if r['skb_cov'] != 1.0:
            errs.append(f"#{r['idx']} {r['stem']}: skeleton coverage {r['skb_cov']}")
        if not (r['levels_dir'] and r['zone']['name'] and r['world_scripts'] and r['arena_glb']):
            errs.append(f"#{r['idx']}: arena dir/zone/cgpr/glb missing")
    return errs


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--game', default=os.environ.get('SOTC_GAME_ROOT'))
    ap.add_argument('--lib', default=os.environ.get('SOTC_LIB'))
    ap.add_argument('--sha')
    ap.add_argument('--out', help='write the census json here')
    ap.add_argument('--check', action='store_true', help='assert the census; exit 1 on failure')
    a = ap.parse_args()
    if not (a.game and a.lib):
        ap.error('--game/--lib (or SOTC_GAME_ROOT/SOTC_LIB) are required')
    t0 = time.time()
    c = census(a.game, a.lib, a.sha or os.path.join(a.game, '..', '..', 'logs', 'game.sha256'))
    if a.out:
        json.dump(c, open(a.out, 'w'), indent=1, default=list)
        open(a.out, 'a').write('\n')
    h = c['hashes']
    print(f"census: {len(c['rows'])} rows, {len(c['banks'])} banks, {h['checked']} shipped hashes checked, "
          f"{len(h['mismatched'])} mismatched, {len(h['missing'])} missing, {time.time() - t0:.1f}s")
    for b in h['mismatched']:
        print('  clobbered:', b['path'], b['expected'][:12])
    errs = check(c) if a.check else []
    for e in errs:
        print('FAIL:', e)
    return 1 if errs else 0


if __name__ == '__main__':
    sys.exit(main())
