// Disasm.java — RE-2: print listing instructions for address ranges (the parse-chain pass used it
// to confirm the call shapes recorded in docs/notes/re2-recon.md §1; e.g. the attach tail
// `0x00588d0e-0x00588d3d` and the loader's measured order `0x0067086b-0x00670941`).
//
// Usage — one or more <start-end> ranges (hex, '-' separator; end is exclusive; a bare address
// disassembles 0x400 bytes from it):
//
//   GHIDRA_HOME=$HOME/code/mine/ghidra-bundle/dist/ghidra_12.1.4_PUBLIC
//   proj_link_dir=$(mktemp -d); ln -s "$PWD/out/re/ghidra" "$proj_link_dir/ghidra"
//   "$GHIDRA_HOME/support/analyzeHeadless" "$proj_link_dir/ghidra" sotc -process eboot.elf \
//     -noanalysis -scriptPath "$PWD/tools/re/ghidra" -postScript Disasm.java \
//     0x0067086b-0x00670941 0x005896a0-0x005899f0 > out/re/log-disasm.log 2>&1
//   rm -rf "$proj_link_dir"
//
// Output: one `Disasm: range <start> - <end>` line per range, then one
// `  <addr>  <instruction>` line per instruction in listing (address) order — deterministic
// under -noanalysis. See tools/re/ghidra/README.md for the baseline rebuild and the
// VA -> file (+0x4000) mapping.

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;

public class Disasm extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length == 0) {
            throw new IllegalArgumentException("usage: Disasm.java <start-end> [...]");
        }
        for (String a : args) {
            String[] parts = a.split("-");
            Address start = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(Long.decode(parts[0]));
            Address end = parts.length > 1
                ? currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(Long.decode(parts[1]))
                : start.add(0x400);
            println("Disasm: range " + start + " - " + end);
            InstructionIterator it = currentProgram.getListing().getInstructions(start, true);
            while (it.hasNext()) {
                Instruction ins = it.next();
                if (ins.getAddress().compareTo(end) >= 0) {
                    break;
                }
                println("  " + ins.getAddress() + "  " + ins.toString());
            }
        }
    }
}
