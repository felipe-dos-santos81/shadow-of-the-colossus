"""Per-boss interpreter coverage (S13 design D13): what each colossus's exported mode graph uses (predicates, actions,
control-def opcodes, locomotion kinds) against what the app's registries run (the `scr_registry_dump` output).

usage: python3 -I coverage.py GRAPHS_DIR DUMP_FILE OUT.md
  GRAPHS_DIR  <SOTC_LIB>/behaviour (the *.graph.json of `make behaviour`)
  DUMP_FILE   app/build/scr_registry_dump's stdout; several `registry <name>` blocks are merged (implemented > gap)
Each item is implemented, gap (known and refused) or missing (unknown to every registry in the dump).
"""
import glob
import json
import os
import sys

BOSSES = ["MINOTOUR_A", "MAMMOTH", "KNIGHT", "KIRIN", "BIRD", "MINOTOUR_B", "EEL", "YAMORI_B", "KAME", "NARGA", "LEO",
          "POSEIDON", "SNAKE", "CERBERUS_R", "MINOTOUR_C", "BUDDHA"]   # census16 order
CATS = (("preds", "predicates"), ("acts", "actions"), ("ops", "opcodes"), ("kinds", "kinds"))
RANK = {"implemented": 2, "gap": 1}


def load_graphs(directory):
    """{boss: {preds: {fn: name}, acts: {fn: name}, ops: {op: '0x..'}, kinds: {n: 'n'}}} from the *.graph.json"""
    out = {}
    for path in glob.glob(os.path.join(directory, "*.graph.json")):
        g = json.load(open(path))
        out[os.path.basename(path)[:-len(".graph.json")]] = {
            "preds": {int(cl["fn"], 16): cl["pred"] for c in g["conditions"].values() for cl in c["clauses"]},
            "acts": {int(a["fn"], 16): a["act"] for lst in g["actions"].values() for a in lst},
            "ops": {r["op"]: "0x%02x" % r["op"] for recs in (g.get("control_defs") or {}).values() for r in recs},
            "kinds": {m["locomotion"]["kind"]: str(m["locomotion"]["kind"]) for m in g["modes"] if m["locomotion"]},
        }
    return out


def parse_dump(text):
    """{fns: {fn: status}, ops: {op: status}, kinds: {n}} merged over every registry block"""
    d = {"fns": {}, "ops": {}, "kinds": set()}

    def put(table, key, status):
        if RANK[status] > RANK.get(table.get(key), 0):
            table[key] = status

    for line in text.splitlines():
        w = line.split()
        if not w or w[0] == "registry":
            continue
        if w[0] == "kind":
            d["kinds"].add(int(w[1]))
        elif w[0] == "op":
            put(d["ops"], int(w[1], 16), w[2])
        else:
            put(d["fns"], int(w[0], 16), w[1])
    return d


def classify(graph, dump):
    """{cat: {implemented|gap|missing: {key: name}}} for one boss"""
    status = {"preds": dump["fns"].get, "acts": dump["fns"].get, "ops": dump["ops"].get,
              "kinds": lambda n: "implemented" if n in dump["kinds"] else None}
    out = {}
    for cat, _ in CATS:
        out[cat] = {"implemented": {}, "gap": {}, "missing": {}}
        for key, name in graph[cat].items():
            out[cat][status[cat](key) or "missing"][key] = name
    return out


def item(cat, key, name):
    return "%s (0x%x)" % (name, key) if cat in ("preds", "acts") else name


def render(graphs, dump):
    bosses = [b for b in BOSSES if b in graphs] + sorted(set(graphs) - set(BOSSES))
    rep = {b: classify(graphs[b], dump) for b in bosses}
    lines = ["# Interpreter coverage per colossus", "",
             "Cells are implemented / gap / missing, counted over the items the boss's exported graph uses.", "",
             "| boss | " + " | ".join(t for _, t in CATS) + " | missing |", "|---|" + "---|" * (len(CATS) + 1)]
    tot = {c: [0, 0, 0] for c, _ in CATS}
    for b in bosses:
        cells = []
        for cat, _ in CATS:
            n = [len(rep[b][cat][s]) for s in ("implemented", "gap", "missing")]
            tot[cat] = [x + y for x, y in zip(tot[cat], n)]
            cells.append("%d/%d/%d" % tuple(n))
        lines.append("| %s | %s | %d |" % (b, " | ".join(cells), sum(len(rep[b][c]["missing"]) for c, _ in CATS)))
    lines.append("| **total** | %s | %d |" % (" | ".join("%d/%d/%d" % tuple(tot[c]) for c, _ in CATS),
                                              sum(t[2] for t in tot.values())))
    lines += ["", "The total row sums the boss rows: an item two bosses share counts twice.", "", "## Missing items", ""]
    for b in bosses:
        parts = ["- %s: %s" % (t, ", ".join(item(c, k, n) for k, n in sorted(rep[b][c]["missing"].items())))
                 for c, t in CATS if rep[b][c]["missing"]]
        lines += ["### " + b, ""] + (parts or ["- nothing missing"]) + [""]
    lines += ["---", "",
              "Footer (ruling R-U): BUDDHA, EEL, NARGA, SNAKE, BIRD, LEO and CERBERUS_R use non-`scr_slowstatus` status types "
              "in some modes, so the export walks fewer per-mode fields for them and their counts may be under.",
              "`sub` (0x832610) is the evaluator's own predicate: always implemented, never a boss-specific item."]
    return "\n".join(lines) + "\n"


def main(argv):
    if len(argv) != 4:
        sys.exit(__doc__)
    graphs = load_graphs(argv[1])
    if not graphs:
        sys.exit("no *.graph.json in " + argv[1])
    open(argv[3], "w").write(render(graphs, parse_dump(open(argv[2]).read())))


if __name__ == "__main__":
    main(sys.argv)
