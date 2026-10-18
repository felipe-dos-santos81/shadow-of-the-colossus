// FindAnim.java — RE-1 (B3): locate the animation runtime in eboot.elf.
//
// The `xff` container family (.anb / .skb) and the sibling .cani bank are decoded offline
// (tools/re/anb/probe.py; docs/notes/re-anb.md §1–§2). This script is the other half of RE-1:
// find where the retail runtime touches those containers, so the loading path and the keyframe
// sampling can be read out of the binary and fed back into the decode.
//
// Mode 1 — scan (default): walk every initialized memory block of the current program and report
// every hit of the container-family constants and strings, each with its containing function and
// a one-line context. Deterministic: hits print sorted by (address, pattern), and every pattern
// gets a count line even when it is 0 (a documented negative is a result).
//
//   $GHIDRA_HOME/support/analyzeHeadless out/re/ghidra sotc -process eboot.elf -noanalysis \
//       -scriptPath tools/re/ghidra -postScript FindAnim.java > out/re/ghidra-anim.log 2>&1
//
// Mode 2 — decompile: dump the decompilation of the function containing each address (addresses
// come from the scan log):
//
//   ... -postScript FindAnim.java decompile 0x00401234 0x00405678
//
// Mode 3 — follow: list the references to each address (who points at this string or constant)
// and dump the decompilation of every function that references it — the loading-path walk:
//
//   ... -postScript FindAnim.java follow 0x01030996 0x0127059e
//
// Scan patterns (fixed order; string patterns match ASCII case-insensitively, >= 3 chars):
//
//   xff-magic      bytes 78 66 66 00   the `xff` container magic (u32 LE 0x00666678)
//   xff-magic-rev  bytes 00 66 66 78   byte-order variant (u32 LE 0x78666600)
//   oodle-8c0a     bytes 8c 0a         Oodle block marker (u16 LE 0x0A8C)
//   str-anb        "anb"      str-dot-anb ".anb"     str-skb "skb"     str-mina "MINA"
//   str-anim       "ANIM" / "anim" — one pattern: the plan's two entries (the ANIM string and
//                  the anim directory name) coincide case-insensitively, so scanning once
//                  covers both without duplicating every hit
//   str-animdata   "ANIMDATA"  str-cp3shadow "cp3shadow"
//
// Output: one `FindAnim: pattern …` line per pattern, one `FindAnim: hit …` line per hit, then a
// `FindAnim: summary …` line. Context: string patterns report the enclosing printable string;
// binary patterns report the surrounding bytes with the match bracketed. Bytes that cannot be
// read are zero-filled for comparison and counted in the summary (unreadable=N).

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressSpace;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.mem.MemoryAccessException;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;
import ghidra.program.model.symbol.ReferenceManager;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.List;
import java.util.TreeSet;

public class FindAnim extends GhidraScript {

    /** Read window; consecutive windows overlap by MAX_PATTERN-1 bytes. */
    private static final int CHUNK = 1 << 20;
    /** Fallback read granularity when a whole window cannot be read in one call. */
    private static final int SUB = 4096;
    /** Longest pattern: "cp3shadow" (9 bytes). */
    private static final int MAX_PATTERN = 9;
    /** Longest enclosing string printed for a string hit. */
    private static final int MAX_CONTEXT = 160;
    /** How far left of a match the enclosing string may start. */
    private static final int MAX_LEFT = 256;

    private static final class Pattern {
        final String name;
        final String desc;
        final boolean ci;
        final byte[] bytes;
        final byte[] lower;

        Pattern(String name, String desc, boolean ci, int... vals) {
            this.name = name;
            this.desc = desc;
            this.ci = ci;
            this.bytes = new byte[vals.length];
            this.lower = new byte[vals.length];
            for (int i = 0; i < vals.length; i++) {
                this.bytes[i] = (byte) vals[i];
                this.lower[i] = lowerByte((byte) vals[i]);
            }
        }
    }

    private static final Pattern[] PATTERNS = {
        new Pattern("xff-magic", "u32 LE 0x00666678; container magic bytes 78 66 66 00", false,
            0x78, 0x66, 0x66, 0x00),
        new Pattern("xff-magic-rev", "u32 LE 0x78666600; byte-order variant bytes 00 66 66 78", false,
            0x00, 0x66, 0x66, 0x78),
        new Pattern("oodle-8c0a", "u16 LE 0x0A8C; Oodle block marker bytes 8c 0a", false,
            0x8c, 0x0a),
        new Pattern("str-anb", "ASCII \"anb\" (case-insensitive)", true, 'a', 'n', 'b'),
        new Pattern("str-dot-anb", "ASCII \".anb\" (case-insensitive)", true, '.', 'a', 'n', 'b'),
        new Pattern("str-skb", "ASCII \"skb\" (case-insensitive)", true, 's', 'k', 'b'),
        new Pattern("str-mina", "ASCII \"MINA\" (case-insensitive)", true, 'm', 'i', 'n', 'a'),
        new Pattern("str-anim", "ASCII \"ANIM\"/\"anim\" (case-insensitive; one scan for both plan entries)", true,
            'a', 'n', 'i', 'm'),
        new Pattern("str-animdata", "ASCII \"ANIMDATA\" (case-insensitive)", true,
            'a', 'n', 'i', 'm', 'd', 'a', 't', 'a'),
        new Pattern("str-cp3shadow", "ASCII \"cp3shadow\" (case-insensitive)", true,
            'c', 'p', '3', 's', 'h', 'a', 'd', 'o', 'w'),
    };

    private static final class Hit implements Comparable<Hit> {
        final Address addr;
        final int pat;

        Hit(Address addr, int pat) {
            this.addr = addr;
            this.pat = pat;
        }

        @Override
        public int compareTo(Hit o) {
            int c = Long.compare(addr.getOffset(), o.addr.getOffset());
            return c != 0 ? c : Integer.compare(pat, o.pat);
        }
    }

    /** Bytes that could not be read during the scan (zero-filled for comparison). */
    private long unreadableBytes;

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length > 0 && "decompile".equals(args[0])) {
            decompileMode(args);
            return;
        }
        if (args.length > 0 && "follow".equals(args[0])) {
            followMode(args);
            return;
        }
        if (args.length > 0) {
            throw new IllegalArgumentException("unknown mode \"" + args[0]
                + "\" (usage: FindAnim.java [decompile <addr>... | follow <addr>...])");
        }
        scanMode();
    }

    // --- Mode 1: scan ---------------------------------------------------------

    private void scanMode() throws Exception {
        Memory memory = currentProgram.getMemory();
        List<Hit> hits = new ArrayList<>();
        int blocks = 0;
        long bytes = 0;
        unreadableBytes = 0;

        byte[] buf = new byte[CHUNK + MAX_PATTERN - 1];
        byte[] lower = new byte[buf.length];

        for (MemoryBlock block : memory.getBlocks()) {
            if (!block.isInitialized()) {
                continue;
            }
            blocks++;
            long len = block.getSize();
            bytes += len;
            long pos = 0;
            while (pos < len) {
                int want = (int) Math.min((long) buf.length, len - pos);
                Address cur = block.getStart().add(pos);
                int got = readWindow(block, cur, buf, want);
                boolean last = (pos + got >= len);
                int owned = last ? got : Math.max(0, got - (MAX_PATTERN - 1));
                for (int i = 0; i < got; i++) {
                    lower[i] = lowerByte(buf[i]);
                }
                scanWindow(buf, lower, got, owned, cur, hits);
                pos += last ? got : Math.max(1, got - (MAX_PATTERN - 1));
            }
        }

        Collections.sort(hits);
        int[] counts = new int[PATTERNS.length];
        for (Hit h : hits) {
            counts[h.pat]++;
        }

        println("FindAnim: scan program=" + currentProgram.getName()
            + " language=" + currentProgram.getLanguageID()
            + " imageBase=" + currentProgram.getImageBase()
            + " blocks=" + blocks
            + " bytes=" + bytes
            + " patterns=" + PATTERNS.length);
        for (int pi = 0; pi < PATTERNS.length; pi++) {
            println("FindAnim: pattern " + PATTERNS[pi].name + " (" + PATTERNS[pi].desc + "): "
                + counts[pi] + " hit(s)");
        }

        FunctionManager fm = currentProgram.getFunctionManager();
        TreeSet<String> functions = new TreeSet<>();
        for (Hit h : hits) {
            Function f = fm.getFunctionContaining(h.addr);
            String fn = f == null ? "-" : f.getName();
            if (f != null) {
                functions.add(f.getEntryPoint().toString());
            }
            String blockName = blockNameAt(h.addr);
            Pattern p = PATTERNS[h.pat];
            String ctx = p.ci ? stringContext(h.addr) : hexContext(h.addr, p.bytes.length);
            println("FindAnim: hit " + h.addr + " blk=" + blockName + " fn=" + fn
                + " pat=" + p.name + " ctx=" + ctx);
        }

        println("FindAnim: summary program=" + currentProgram.getName()
            + " blocks=" + blocks
            + " bytes=" + bytes
            + " patterns=" + PATTERNS.length
            + " hits=" + hits.size()
            + " functions=" + functions.size()
            + " unreadable=" + unreadableBytes);
    }

    /**
     * Fill buf[0..want) from block at cur. A single read covers the window; when it cannot be
     * served (short read or unreadable range), the rest is read in SUB-byte pieces, and any piece
     * that still fails is zero-filled and counted in unreadableBytes. Returns the number of bytes
     * available in buf (always want unless the block itself ended early).
     */
    private int readWindow(MemoryBlock block, Address cur, byte[] buf, int want) {
        try {
            int got = block.getBytes(cur, buf, 0, want);
            if (got >= want) {
                return got;
            }
            return got + readSub(block, cur.add(got), buf, got, want - got);
        }
        catch (MemoryAccessException e) {
            return readSub(block, cur, buf, 0, want);
        }
    }

    private int readSub(MemoryBlock block, Address start, byte[] buf, int off, int len) {
        int done = 0;
        while (done < len) {
            int n = Math.min(SUB, len - done);
            try {
                int got = block.getBytes(start.add(done), buf, off + done, n);
                if (got < n) {
                    Arrays.fill(buf, off + done + got, off + done + n, (byte) 0);
                    unreadableBytes += (n - got);
                }
            }
            catch (MemoryAccessException e) {
                Arrays.fill(buf, off + done, off + done + n, (byte) 0);
                unreadableBytes += n;
            }
            done += n;
        }
        return len;
    }

    /**
     * Scan buf[0..got) for every pattern; a match starting at position i is recorded only when
     * i < owned, so the overlap region (the last MAX_PATTERN-1 bytes of a non-final window) is
     * owned by the next window and every start position is scanned exactly once.
     */
    private void scanWindow(byte[] buf, byte[] lower, int got, int owned, Address base, List<Hit> hits) {
        for (int pi = 0; pi < PATTERNS.length; pi++) {
            Pattern p = PATTERNS[pi];
            int n = p.bytes.length;
            int lastStart = Math.min(owned, got - n + 1) - 1;
            for (int i = 0; i <= lastStart; i++) {
                boolean match = true;
                if (p.ci) {
                    for (int k = 0; k < n; k++) {
                        if (lower[i + k] != p.lower[k]) {
                            match = false;
                            break;
                        }
                    }
                }
                else {
                    for (int k = 0; k < n; k++) {
                        if (buf[i + k] != p.bytes[k]) {
                            match = false;
                            break;
                        }
                    }
                }
                if (match) {
                    hits.add(new Hit(base.add(i), pi));
                }
            }
        }
    }

    // --- Mode 2: decompile ----------------------------------------------------

    private void decompileMode(String[] args) throws Exception {
        List<Address> addrs = parseAddresses(args, 1);
        if (addrs.isEmpty()) {
            throw new IllegalArgumentException(
                "usage: FindAnim.java decompile <addr> [<addr>...] (addresses from the scan log)");
        }
        Collections.sort(addrs);
        FunctionManager fm = currentProgram.getFunctionManager();
        List<Function> funcs = new ArrayList<>();
        for (Address a : addrs) {
            Function f = fm.getFunctionContaining(a);
            if (f == null) {
                f = fm.getFunctionAt(a);
            }
            if (f == null) {
                println("FindAnim: decompile " + a + " — no function contains this address");
                continue;
            }
            println("FindAnim: decompile " + a + " -> " + f.getName() + " @" + f.getEntryPoint());
            funcs.add(f);
        }
        decompileFunctions(funcs);
    }

    // --- Mode 3: follow — references, then the referencing functions -----------

    private void followMode(String[] args) throws Exception {
        List<Address> addrs = parseAddresses(args, 1);
        if (addrs.isEmpty()) {
            throw new IllegalArgumentException(
                "usage: FindAnim.java follow <addr> [<addr>...] (addresses from the scan log)");
        }
        Collections.sort(addrs);
        ReferenceManager rm = currentProgram.getReferenceManager();
        FunctionManager fm = currentProgram.getFunctionManager();
        List<Function> funcs = new ArrayList<>();
        for (Address a : addrs) {
            List<Reference> refs = new ArrayList<>();
            ReferenceIterator it = rm.getReferencesTo(a);
            while (it.hasNext()) {
                refs.add(it.next());
            }
            refs.sort((r1, r2) -> {
                int c = Long.compare(r1.getFromAddress().getOffset(), r2.getFromAddress().getOffset());
                return c != 0 ? c
                    : r1.getReferenceType().toString().compareTo(r2.getReferenceType().toString());
            });
            println("FindAnim: follow " + a + " blk=" + blockNameAt(a) + " refs=" + refs.size());
            for (Reference r : refs) {
                Address from = r.getFromAddress();
                Function f = fm.getFunctionContaining(from);
                println("FindAnim: ref " + a + " <- " + from + " blk=" + blockNameAt(from)
                    + " type=" + r.getReferenceType() + " fn=" + (f == null ? "-" : f.getName()));
                if (f != null) {
                    funcs.add(f);
                }
            }
        }
        decompileFunctions(funcs);
    }

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

    /** Decompile each distinct function once, in entry-address order. */
    private void decompileFunctions(List<Function> funcs) throws Exception {
        TreeSet<String> seen = new TreeSet<>();
        List<Function> ordered = new ArrayList<>();
        for (Function f : funcs) {
            if (seen.add(f.getEntryPoint().toString())) {
                ordered.add(f);
            }
        }
        ordered.sort((f1, f2) -> Long.compare(f1.getEntryPoint().getOffset(), f2.getEntryPoint().getOffset()));

        DecompInterface ifc = new DecompInterface();
        if (!ifc.openProgram(currentProgram)) {
            throw new IllegalStateException("the decompiler refused " + currentProgram.getName());
        }
        try {
            println("FindAnim: decompile functions=" + ordered.size());
            for (Function f : ordered) {
                println("FindAnim: function " + f.getName() + " @" + f.getEntryPoint()
                    + " size=" + f.getBody().getNumAddresses());
                DecompileResults res = ifc.decompileFunction(f, 180, monitor);
                if (!res.decompileCompleted()) {
                    println("FindAnim: decompile FAILED " + f.getName() + ": " + res.getErrorMessage());
                    continue;
                }
                String c = res.getDecompiledFunction().getC();
                if (c == null) {
                    println("FindAnim: decompile FAILED " + f.getName() + ": no C text");
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

    // --- Context helpers ------------------------------------------------------

    private String blockNameAt(Address a) {
        MemoryBlock b = currentProgram.getMemory().getBlock(a);
        return b == null ? "-" : b.getName();
    }

    /** The NUL/printable-bounded ASCII run containing the match. */
    private String stringContext(Address addr) {
        Address start = addr;
        for (int i = 0; i < MAX_LEFT && start.getOffset() > 0; i++) {
            Address prev = start.subtract(1);
            int b = byteAt(prev);
            if (b < 0 || !printable(b)) {
                break;
            }
            start = prev;
        }
        StringBuilder sb = new StringBuilder();
        Address a = start;
        for (int i = 0; i < MAX_CONTEXT; i++) {
            int b = byteAt(a);
            if (b < 0 || !printable(b)) {
                break;
            }
            sb.append((char) b);
            a = a.add(1);
        }
        boolean more = false;
        int b = byteAt(a);
        if (b >= 0 && printable(b)) {
            more = true;
        }
        return "\"" + sb + "\"" + (more ? "…" : "");
    }

    /** Hex bytes around the match, with the matched bytes bracketed. */
    private String hexContext(Address addr, int matchLen) {
        StringBuilder sb = new StringBuilder();
        long base = addr.getOffset();
        for (long o = -8; o < matchLen + 8; o++) {
            if (o == 0) {
                sb.append('[');
            }
            sb.append(hexByte(base + o));
            sb.append(' ');
            if (o == matchLen - 1) {
                sb.append("] ");
            }
        }
        return sb.toString().trim();
    }

    private String hexByte(long offset) {
        if (offset < 0) {
            return "??";
        }
        int b = byteAt(currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(offset));
        return b < 0 ? "??" : String.format("%02x", b);
    }

    private int byteAt(Address a) {
        try {
            return currentProgram.getMemory().getByte(a) & 0xff;
        }
        catch (MemoryAccessException e) {
            return -1;
        }
    }

    private static boolean printable(int b) {
        return b >= 0x20 && b <= 0x7e;
    }

    private static byte lowerByte(byte b) {
        return (b >= 'A' && b <= 'Z') ? (byte) (b + 32) : b;
    }
}
