// FindParseChain.java — RE-2: the `.anb` parse-chain seed set (the attach path).
//
// RE-2 (docs/notes/re2-recon.md §1) decoded the file -> runtime parse chain of the `xff` animation
// containers: loadCdMotionFile (FUN_00670680) -> FS load (FUN_006abe10) -> header fixup
// (FUN_005896a0) -> attach wrapper (FUN_00589310) -> attach (FUN_00588900) -> the fixup twin
// (FUN_00b24130) + section decoder (FUN_00b23c10) -> motion-structure fixup (FUN_0066fe60) ->
// the runtime object. This script carries the pass's seed set (the functions its §0
// decompile/refs logs covered, plus the chain's entry and fixup) so the chain can be re-read
// from the repo alone.
//
// Modes (deterministic: every list prints sorted by address; addresses in every heading):
//
//   decompile [<addr>...]   (default) — decompile each seed function (plus any explicit <addr>),
//                           sorted by entry address. A bare address list means this mode.
//   refs [<addr>...]        — print the references to/from each seed (or only the explicit <addr>s,
//                           which replace the seed set) — the cheap reconnaissance mode (no decompile).
//   callers [<addr>...] [d] — walk incoming references to those functions, `d` levels (default 1);
//                           with no addresses, walks the seed set.
//   callees [<addr>...] [d] — walk outgoing calls of those functions, `d` levels (default 1);
//                           with no addresses, walks the seed set.
//
// The callers/callees modes print edges and the reached functions only; feed the addresses you
// want decompiled back to the default mode (or FindSampler.java decompile).
//
// Usage (the dot-free project-path workaround, see tools/re/ghidra/README.md):
//
//   GHIDRA_HOME=$HOME/code/mine/ghidra-bundle/dist/ghidra_12.1.4_PUBLIC
//   proj_link_dir=$(mktemp -d); ln -s "$PWD/out/re/ghidra" "$proj_link_dir/ghidra"
//   "$GHIDRA_HOME/support/analyzeHeadless" "$proj_link_dir/ghidra" sotc -process eboot.elf \
//     -noanalysis -scriptPath "$PWD/tools/re/ghidra" -postScript FindParseChain.java \
//     > out/re/log-parsechain.log 2>&1
//   rm -rf "$proj_link_dir"
//
// Output: `FindParseChain: seed …` / `edge …` / `reached-fn …` / `function …` (each followed by
// the decompilation) / `summary …` lines.

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

public class FindParseChain extends GhidraScript {

    /** Parse-chain seeds — RE-2 recon §1.1 and the §0 decompile logs (roles per the memo). */
    private static final long[] SEEDS = {
        0x00588830L, // alternate attach (pre-allocated node) §1.3
        0x00588900L, // attach §1.3
        0x00588dd0L, // (§0 log-parsechain-2)
        0x00588e50L, // (§0 log-parsechain-1)
        0x00588e80L, // attach fallback §1.3
        0x00589310L, // attach wrapper §1.1
        0x005896a0L, // header fixup pass §1.2
        0x0058a030L, // section-decoder callback §1.3
        0x0058a0a0L, // section-decoder callback §1.3
        0x0058a110L, // name/object walk §4
        0x005fc5d0L, // frame-count getter (sampler) §1.6
        0x0066fe60L, // motion-structure fixup §1.5
        0x00670680L, // loadCdMotionFile (.anb loader) §1.1
        0x006abe10L, // FS load §1.1
        0x00b23a50L, // table walk §4
        0x00b23b10L, // (§0 log-parsechain-2)
        0x00b23c10L, // section decoder §1.4
        0x00b24130L, // fixup twin §1.2
    };

    private static final int DEFAULT_DEPTH = 1;
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
        if (args.length == 0) {
            decompileMode(args);
            return;
        }
        String mode = args[0];
        if ("decompile".equals(mode)) {
            decompileMode(args);
            return;
        }
        if ("refs".equals(mode)) {
            refsMode(args);
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
        if (mode.startsWith("0x") || mode.startsWith("0X")) {
            decompileMode(args);
            return;
        }
        throw new IllegalArgumentException("unknown mode \"" + mode
            + "\" (usage: FindParseChain.java [decompile [<addr>...] | refs [<addr>...] |"
            + " callers [<addr>...] [depth] | callees [<addr>...] [depth]])");
    }

    // --- Mode: decompile (default) ---------------------------------------------

    private void decompileMode(String[] args) throws Exception {
        int from = args.length > 0 && "decompile".equals(args[0]) ? 1 : 0;
        List<Address> addrs = parseAddresses(args, from);
        for (long s : SEEDS) {
            addrs.add(addr(s));
        }
        FunctionManager fm = currentProgram.getFunctionManager();
        TreeMap<Long, Function> funcs = new TreeMap<>();
        for (Address a : addrs) {
            Function f = fm.getFunctionContaining(a);
            if (f == null) {
                f = fm.getFunctionAt(a);
            }
            if (f == null) {
                println("FindParseChain: decompile " + a + " — no function contains this address");
                continue;
            }
            funcs.put(f.getEntryPoint().getOffset(), f);
        }
        println("FindParseChain: decompile program=" + currentProgram.getName()
            + " functions=" + funcs.size());
        decompileFunctions(new ArrayList<>(funcs.values()));
    }

    // --- Mode: refs ------------------------------------------------------------

    private void refsMode(String[] args) throws Exception {
        int from = args.length > 0 && "refs".equals(args[0]) ? 1 : 0;
        List<Address> addrs = parseAddresses(args, from);
        if (addrs.isEmpty()) {
            for (long s : SEEDS) {
                addrs.add(addr(s));
            }
        }
        Collections.sort(addrs);
        ReferenceManager rm = currentProgram.getReferenceManager();
        FunctionManager fm = currentProgram.getFunctionManager();
        Listing listing = currentProgram.getListing();
        for (Address a : addrs) {
            Function f = fm.getFunctionContaining(a);
            println("FindParseChain: refs " + a
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
                println("FindParseChain: ref-in " + a + " <- " + r.getFromAddress()
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
                    println("FindParseChain: ref-out " + f.getEntryPoint() + " -> " + line);
                }
            }
        }
    }

    // --- Mode: callers / callees ----------------------------------------------

    private void walkMode(String[] args, boolean incoming, boolean outgoing, String mode)
            throws Exception {
        int depth = DEFAULT_DEPTH;
        List<Long> seedAddrs = new ArrayList<>();
        for (int i = 1; i < args.length; i++) {
            String a = args[i];
            if (a.startsWith("0x") || a.startsWith("0X")) {
                seedAddrs.add(Long.decode(a));
            }
            else if (a.matches("[0-9]+")) {
                depth = Integer.parseInt(a);
            }
            else {
                throw new IllegalArgumentException("not an address or depth: \"" + a + "\"");
            }
        }
        if (seedAddrs.isEmpty()) {
            for (long s : SEEDS) {
                seedAddrs.add(s);
            }
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
                println("FindParseChain: seed " + a + " — no function contains this address");
                continue;
            }
            seedMap.put(f.getEntryPoint().getOffset(), f);
        }
        if (seedMap.isEmpty()) {
            throw new IllegalStateException("no seed address resolved to a function");
        }

        println("FindParseChain: " + mode + " program=" + currentProgram.getName()
            + " language=" + currentProgram.getLanguageID()
            + " imageBase=" + currentProgram.getImageBase()
            + " seeds=" + seedMap.size()
            + " depth=" + depth
            + " incoming=" + incoming
            + " outgoing=" + outgoing);
        for (Function f : seedMap.values()) {
            println("FindParseChain: seed " + f.getEntryPoint() + " fn=" + f.getName());
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
            println("FindParseChain: edge " + addr(e.from) + "(" + nameOf(fm.getFunctionAt(addr(e.from)))
                + ") -> " + addr(e.to) + "(" + nameOf(fm.getFunctionAt(addr(e.to)))
                + ") via=" + e.via);
        }
        println("FindParseChain: reached " + reached.size() + " function(s)");
        for (Map.Entry<Long, Function> en : reached.entrySet()) {
            Function f = en.getValue();
            println("FindParseChain: reached-fn " + f.getEntryPoint() + " fn=" + f.getName()
                + " size=" + f.getBody().getNumAddresses()
                + " reason=" + reason.get(en.getKey()));
        }
        println("FindParseChain: summary mode=" + mode
            + " seeds=" + seedMap.size()
            + " reached=" + reached.size()
            + " edges=" + edges.size());
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
    private void decompileFunctions(List<Function> funcs) throws Exception {
        DecompInterface ifc = new DecompInterface();
        if (!ifc.openProgram(currentProgram)) {
            throw new IllegalStateException("the decompiler refused " + currentProgram.getName());
        }
        try {
            println("FindParseChain: decompile functions=" + funcs.size());
            for (Function f : funcs) {
                println("FindParseChain: function " + f.getName() + " @" + f.getEntryPoint()
                    + " size=" + f.getBody().getNumAddresses());
                DecompileResults res = ifc.decompileFunction(f, DECOMPILE_TIMEOUT, monitor);
                if (!res.decompileCompleted()) {
                    println("FindParseChain: decompile FAILED " + f.getName() + ": " + res.getErrorMessage());
                    continue;
                }
                String c = res.getDecompiledFunction().getC();
                if (c == null) {
                    println("FindParseChain: decompile FAILED " + f.getName() + ": no C text");
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
