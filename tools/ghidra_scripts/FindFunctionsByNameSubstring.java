// Lists all functions whose name contains the given substring (case-insensitive).
// Run via analyzeHeadless ... -postScript FindFunctionsByNameSubstring.java <substring>
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;

public class FindFunctionsByNameSubstring extends GhidraScript {

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 1) {
            println("Usage: FindFunctionsByNameSubstring <substring>");
            return;
        }
        String needle = args[0].toLowerCase();
        int count = 0;
        for (Function f : currentProgram.getFunctionManager().getFunctions(true)) {
            if (f.getName().toLowerCase().contains(needle)) {
                println(f.getEntryPoint() + "\t" + f.getName() + "\tthunk=" + f.isThunk() +
                    "\texternal=" + f.isExternal());
                count++;
            }
        }
        println("Total matches: " + count);
    }
}
