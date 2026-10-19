// FindSampler.java — C3 (decode continuation): locate the keyframe sampler in eboot.elf.
//
// RE-1 located the animation loader chain (docs/notes/re-anb.md §4.4): loadCdMotionFile
// (FUN_00670680) hands the loaded bytes to FUN_0066fe60 — a 31-bit pointer re-base pass over
// nine header fields (base DAT_02f3b8c0) that walks the arrays reached from those fields with
// the per-element fix-ups FUN_0066dec0 / FUN_0066f9a0 / FUN_0066e460. The curve encoding is
// open (§3: the int16-triplet candidate is UNVERIFIED and must never be consumed as animation
// data). This script is the unblocking step: decompile the re-base pass, its callers (who
// consumes the re-based fields), the fix-ups, and the motion-object methods, so the key
// arithmetic (field roles, time base, quaternion reconstruction) can be read out of the binary.
//
// Modes (deterministic: every list is printed sorted by address; addresses in every heading):
//
//   walk [depth] [cap=N]   (default) — from the default seed set (RE-1 §4.4/§4.7) plus any
//                          explicit <addr>..., walk callers (incoming references) and callees
//                          (outgoing calls) `depth` levels (default 1), then decompile every
//                          function reached, sorted by entry address; `cap=N` bounds the
//                          decompiled set (default 160) and the skipped functions are listed,
//                          never silently dropped.
//   decompile <addr>...    — decompile the function containing each address.
//   callers <addr>... [d]  — walk incoming references to those functions, `d` levels (default 1).
//   callees <addr>... [d]  — walk outgoing calls of those functions, `d` levels (default 1).
//   refs <addr>...         — print the references to/from each address (no decompile) — the
//                            cheap reconnaissance mode.
//
// Usage (the RE-0 dot-free project-path workaround; the worktree lives under ~/.local):
//
//   GHIDRA_HOME=$HOME/code/mine/ghidra-bundle/dist/ghidra_12.1.4_PUBLIC
//   proj_link_dir=$(mktemp -d); ln -s "$PWD/out/re/ghidra" "$proj_link_dir/ghidra"
//   "$GHIDRA_HOME/support/analyzeHeadless" "$proj_link_dir/ghidra" sotc -process eboot.elf \
//     -noanalysis -scriptPath "$PWD/tools/re/ghidra" -postScript FindSampler.java \
//     > out/re/ghidra-sampler.log 2>&1
//   # follow-up run example:
//   ... -postScript FindSampler.java callers 0x00670300 2 > out/re/ghidra-sampler-follow.log 2>&1
//
// Output: `FindSampler: seed …` / `edge …` / `reached-fn …` / `function …` (each followed by the
// decompilation) / `summary …` lines. A caller edge (via=caller) is printed as
// `<caller> -> <callee>`; a callee edge (via=callee) is printed as `<caller> -> <callee>` too —
// the edge is always "from calls to", `via` records which walk found it.

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressSpace;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.listing.Listing;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;
import ghidra.program.model.symbol.ReferenceManager;

import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.Map;
import java.util.TreeMap;
import java.util.TreeSet;

public class FindSampler extends GhidraScript {

    /** Default seeds: RE-1 §4.4 / §4.7 — the loader chain + the motion-object method. */
    private static final long[] DEFAULT_SEEDS = {
        0x0066e460L, // per-element fix-up (§4.4)
        0x0066dec0L, // per-element fix-up (§4.4)
        0x0066f9a0L, // per-element fix-up (§4.4)
        0x0066fe60L, // FUN_0066fe60 — the 31-bit pointer re-base pass (§4.4)
        0x00670300L, // motion-object method (§4.7)
        0x00670680L, // loadCdMotionFile (§4.3 / §4.4)
    };

    private static final int DEFAULT_DEPTH = 1;
    private static final int DEFAULT_CAP = 160;
    private static final int DECOMPILE_TIMEOUT = 180;

    /** A call edge (from calls to); `via` records the walk that found it. */
    private static final class Edge implements Comparable<Edge> {
        final long from;
        final long to;
        final String via;

        Edge(long from, long to, String via) {
            this.from = from;
            this.to = to;
            this.via = via;
        }

        @Override
        public int compareTo(Edge o) {
            int c = Long.compare(from, o.from);
            return c != 0 ? c : Long.compare(to, o.to);
        }

        @Override
        public boolean equals(Object o) {
            return o instanceof Edge && compareTo((Edge) o) == 0;
        }

        @Override
        public int hashCode() {
            return (int) (from * 31 + to);
        }
    }

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length > 0) {
            String mode = args[0];
            if ("decompile".equals(mode)) {
                decompileMode(args);
                return;
            }
            if ("callers".equals(mode)) {
                walkMode(args, true, false, "callers");
                return;
            }
            if ("callees".equals(mode)) {
                walkMode(args, false, true, "callees");
                return;
            }
            if ("refs".equals(mode)) {
                refsMode(args);
                return;
            }
            if (!"walk".equals(mode)) {
                throw new IllegalArgumentException("unknown mode \"" + mode
                    + "\" (usage: FindSampler.java [walk [depth] [cap=N] | decompile <addr>... |"
                    + " callers <addr>... [depth] | callees <addr>... [depth] | refs <addr>...])");
            }
        }
        walkMode(args, true, true, "walk");
    }

    // --- Mode: walk ------------------------------------------------------------

    private void walkMode(String[] args, boolean incoming, boolean outgoing, String mode)
            throws Exception {
        int depth = DEFAULT_DEPTH;
        int cap = DEFAULT_CAP;
        List<Long> seedAddrs = new ArrayList<>();
        boolean explicit = false;
        for (int i = 1; i < args.length; i++) {
            String a = args[i];
            if (a.startsWith("cap=")) {
                cap = Integer.parseInt(a.substring(4));
            }
            else if (a.startsWith("0x") || a.startsWith("0X")) {
                seedAddrs.add(Long.decode(a));
                explicit = true;
            }
            else if (a.matches("[0-9]+")) {
                depth = Integer.parseInt(a);
            }
            else {
                throw new IllegalArgumentException("not an address, depth, or cap=: \"" + a + "\"");
            }
        }
        if ("walk".equals(mode)) {
            for (long s : DEFAULT_SEEDS) {
                seedAddrs.add(s);
            }
        }
        else if (!explicit) {
            throw new IllegalArgumentException(
                "usage: FindSampler.java " + mode + " <addr>... [depth]");
        }

        FunctionManager fm = currentProgram.getFunctionManager();
        TreeMap<Long, Function> seedMap = new TreeMap<>();
        for (long s : seedAddrs) {
            Address a = addr(s);
            Function f = fm.getFunctionContaining(a);
            if (f == null) {
                f = fm.getFunctionAt(a);
            }
            if (f == null) {
                println("FindSampler: seed " + a + " — no function contains this address");
                continue;
            }
            seedMap.put(f.getEntryPoint().getOffset(), f);
        }
        if (seedMap.isEmpty()) {
            throw new IllegalStateException("no seed address resolved to a function");
        }

        println("FindSampler: " + mode + " program=" + currentProgram.getName()
            + " language=" + currentProgram.getLanguageID()
            + " imageBase=" + currentProgram.getImageBase()
            + " seeds=" + seedMap.size()
            + " depth=" + depth
            + " cap=" + cap
            + " incoming=" + incoming
            + " outgoing=" + outgoing);
        for (Function f : seedMap.values()) {
            println("FindSampler: seed " + f.getEntryPoint() + " fn=" + f.getName());
        }

        TreeMap<Long, Function> reached = new TreeMap<>(seedMap);
        TreeMap<Long, String> reason = new TreeMap<>();
        for (Long e : seedMap.keySet()) {
            reason.put(e, "seed");
        }
        TreeSet<Edge> edges = new TreeSet<>();
        List<Function> frontier = new ArrayList<>(seedMap.values());
        for (int level = 1; level <= depth && !frontier.isEmpty(); level++) {
            List<Function> next = new ArrayList<>();
            for (Function f : frontier) {
                if (incoming) {
                    for (Function c : callersOf(f)) {
                        edges.add(new Edge(c.getEntryPoint().getOffset(),
                            f.getEntryPoint().getOffset(), "caller"));
                        long key = c.getEntryPoint().getOffset();
                        if (!reached.containsKey(key)) {
                            reached.put(key, c);
                            reason.put(key, "caller-of:" + f.getEntryPoint());
                            next.add(c);
                        }
                    }
                }
                if (outgoing) {
                    for (Function g : calleesOf(f)) {
                        edges.add(new Edge(f.getEntryPoint().getOffset(),
                            g.getEntryPoint().getOffset(), "callee"));
                        long key = g.getEntryPoint().getOffset();
                        if (!reached.containsKey(key)) {
                            reached.put(key, g);
                            reason.put(key, "callee-of:" + f.getEntryPoint());
                            next.add(g);
                        }
                    }
                }
            }
            frontier = next;
        }

        for (Edge e : edges) {
            println("FindSampler: edge " + addr(e.from) + "(" + nameOf(fm.getFunctionAt(addr(e.from)))
                + ") -> " + addr(e.to) + "(" + nameOf(fm.getFunctionAt(addr(e.to)))
                + ") via=" + e.via);
        }
        println("FindSampler: reached " + reached.size() + " function(s)");
        for (Map.Entry<Long, Function> en : reached.entrySet()) {
            Function f = en.getValue();
            println("FindSampler: reached-fn " + f.getEntryPoint() + " fn=" + f.getName()
                + " size=" + f.getBody().getNumAddresses()
                + " reason=" + reason.get(en.getKey()));
        }

        List<Function> toDecompile = new ArrayList<>();
        List<Function> skipped = new ArrayList<>();
        for (Function f : reached.values()) {
            if (toDecompile.size() < cap) {
                toDecompile.add(f);
            }
            else {
                skipped.add(f);
            }
        }
        decompileFunctions(toDecompile, reason);
        if (!skipped.isEmpty()) {
            println("FindSampler: skipped " + skipped.size() + " function(s) over cap=" + cap);
            for (Function f : skipped) {
                println("FindSampler: skipped-fn " + f.getEntryPoint() + " fn=" + f.getName()
                    + " reason=" + reason.get(f.getEntryPoint().getOffset()));
            }
        }
        println("FindSampler: summary mode=" + mode
            + " seeds=" + seedMap.size()
            + " reached=" + reached.size()
            + " edges=" + edges.size()
            + " decompiled=" + toDecompile.size()
            + " skipped=" + skipped.size());
    }

    // --- Mode: decompile -------------------------------------------------------

    private void decompileMode(String[] args) throws Exception {
        List<Address> addrs = parseAddresses(args, 1);
        if (addrs.isEmpty()) {
            throw new IllegalArgumentException(
                "usage: FindSampler.java decompile <addr> [<addr>...] (addresses from a walk log)");
        }
        Collections.sort(addrs);
        FunctionManager fm = currentProgram.getFunctionManager();
        TreeMap<Long, Function> funcs = new TreeMap<>();
        for (Address a : addrs) {
            Function f = fm.getFunctionContaining(a);
            if (f == null) {
                f = fm.getFunctionAt(a);
            }
            if (f == null) {
                println("FindSampler: decompile " + a + " — no function contains this address");
                continue;
            }
            println("FindSampler: decompile " + a + " -> " + f.getName() + " @" + f.getEntryPoint());
            funcs.put(f.getEntryPoint().getOffset(), f);
        }
        decompileFunctions(new ArrayList<>(funcs.values()), null);
    }

    // --- Mode: refs ------------------------------------------------------------

    private void refsMode(String[] args) throws Exception {
        List<Address> addrs = parseAddresses(args, 1);
        if (addrs.isEmpty()) {
            throw new IllegalArgumentException(
                "usage: FindSampler.java refs <addr> [<addr>...] (addresses from a walk log)");
        }
        Collections.sort(addrs);
        ReferenceManager rm = currentProgram.getReferenceManager();
        FunctionManager fm = currentProgram.getFunctionManager();
        Listing listing = currentProgram.getListing();
        for (Address a : addrs) {
            Function f = fm.getFunctionContaining(a);
            println("FindSampler: refs " + a
                + " fn=" + (f == null ? "-" : f.getName())
                + (f == null ? "" : " @" + f.getEntryPoint()));
            List<Reference> in = new ArrayList<>();
            ReferenceIterator it = rm.getReferencesTo(a);
            while (it.hasNext()) {
                in.add(it.next());
            }
            in.sort((r1, r2) -> {
                int c = Long.compare(r1.getFromAddress().getOffset(), r2.getFromAddress().getOffset());
                return c != 0 ? c
                    : r1.getReferenceType().toString().compareTo(r2.getReferenceType().toString());
            });
            for (Reference r : in) {
                Function c = fm.getFunctionContaining(r.getFromAddress());
                println("FindSampler: ref-in " + a + " <- " + r.getFromAddress()
                    + " type=" + r.getReferenceType()
                    + " fn=" + (c == null ? "-" : c.getName() + "@" + c.getEntryPoint()));
            }
            if (f != null) {
                TreeMap<Long, String> outs = new TreeMap<>();
                InstructionIterator ii = listing.getInstructions(f.getBody(), true);
                while (ii.hasNext()) {
                    Instruction ins = ii.next();
                    for (Reference r : ins.getReferencesFrom()) {
                        if (!r.getReferenceType().isCall() && !r.getReferenceType().isJump()) {
                            continue;
                        }
                        Address to = r.getToAddress();
                        Function g = fm.getFunctionAt(to);
                        if (!outs.containsKey(to.getOffset())) {
                            outs.put(to.getOffset(), to + " type=" + r.getReferenceType()
                                + " fn=" + (g == null ? "-" : g.getName()));
                        }
                    }
                }
                for (String line : outs.values()) {
                    println("FindSampler: ref-out " + f.getEntryPoint() + " -> " + line);
                }
            }
        }
    }

    // --- Call-graph helpers ----------------------------------------------------

    /** Functions whose body references f's entry point (sorted by entry). */
    private List<Function> callersOf(Function f) {
        ReferenceManager rm = currentProgram.getReferenceManager();
        FunctionManager fm = currentProgram.getFunctionManager();
        TreeMap<Long, Function> out = new TreeMap<>();
        ReferenceIterator it = rm.getReferencesTo(f.getEntryPoint());
        while (it.hasNext()) {
            Reference r = it.next();
            Function c = fm.getFunctionContaining(r.getFromAddress());
            if (c == null) {
                continue;
            }
            out.put(c.getEntryPoint().getOffset(), c);
        }
        return new ArrayList<>(out.values());
    }

    /** Functions called (or tail-jumped) from f's body (sorted by entry). */
    private List<Function> calleesOf(Function f) {
        FunctionManager fm = currentProgram.getFunctionManager();
        Listing listing = currentProgram.getListing();
        TreeMap<Long, Function> out = new TreeMap<>();
        InstructionIterator it = listing.getInstructions(f.getBody(), true);
        while (it.hasNext()) {
            Instruction ins = it.next();
            for (Reference r : ins.getReferencesFrom()) {
                if (!r.getReferenceType().isCall() && !r.getReferenceType().isJump()) {
                    continue;
                }
                Address to = r.getToAddress();
                Function g = fm.getFunctionAt(to);
                if (g == null || g.getEntryPoint().equals(f.getEntryPoint())) {
                    continue;
                }
                if (!g.getEntryPoint().equals(to)) {
                    continue; // intra-function jump into another body region
                }
                out.put(g.getEntryPoint().getOffset(), g);
            }
        }
        return new ArrayList<>(out.values());
    }

    /** Decompile each function once, in entry-address order. */
    private void decompileFunctions(List<Function> funcs, Map<Long, String> reason) throws Exception {
        DecompInterface ifc = new DecompInterface();
        if (!ifc.openProgram(currentProgram)) {
            throw new IllegalStateException("the decompiler refused " + currentProgram.getName());
        }
        try {
            println("FindSampler: decompile functions=" + funcs.size());
            for (Function f : funcs) {
                String r = reason == null ? null : reason.get(f.getEntryPoint().getOffset());
                println("FindSampler: function " + f.getName() + " @" + f.getEntryPoint()
                    + " size=" + f.getBody().getNumAddresses()
                    + (r == null ? "" : " reason=" + r));
                DecompileResults res = ifc.decompileFunction(f, DECOMPILE_TIMEOUT, monitor);
                if (!res.decompileCompleted()) {
                    println("FindSampler: decompile FAILED " + f.getName() + ": " + res.getErrorMessage());
                    continue;
                }
                String c = res.getDecompiledFunction().getC();
                if (c == null) {
                    println("FindSampler: decompile FAILED " + f.getName() + ": no C text");
                    continue;
                }
                for (String line : c.split("\n", -1)) {
                    println(line);
                }
            }
        }
        finally {
            ifc.dispose();
        }
    }

    // --- Parsing / formatting --------------------------------------------------

    private List<Address> parseAddresses(String[] args, int from) {
        List<Address> addrs = new ArrayList<>();
        AddressSpace space = currentProgram.getAddressFactory().getDefaultAddressSpace();
        for (int i = from; i < args.length; i++) {
            try {
                addrs.add(space.getAddress(Long.decode(args[i])));
            }
            catch (NumberFormatException e) {
                throw new IllegalArgumentException("not an address: \"" + args[i] + "\"");
            }
        }
        return addrs;
    }

    private Address addr(long offset) {
        return currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(offset);
    }

    private static String nameOf(Function f) {
        return f == null ? "-" : f.getName();
    }
}
