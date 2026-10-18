// RE-0 acceptance summary: one stable line the shell script can assert on.
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;

public class PrintSummary extends GhidraScript {
    @Override
    public void run() throws Exception {
        long functions = 0;
        var it = currentProgram.getFunctionManager().getFunctions(true);
        while (it.hasNext()) {
            Function f = it.next();
            if (!f.isExternal()) { functions++; }
        }
        // Program has no getEntryPoint() in Ghidra 12.1.4 (the plan's call does not compile);
        // the external entry point is the loader's record of the ELF entry.
        var entries = currentProgram.getSymbolTable().getExternalEntryPointIterator();
        String entry = entries.hasNext() ? entries.next().toString() : "none";
        println("summary program=" + currentProgram.getName()
            + " language=" + currentProgram.getLanguageID()
            + " functions=" + functions
            + " entry=" + entry
            + " imageBase=" + currentProgram.getImageBase());
    }
}
