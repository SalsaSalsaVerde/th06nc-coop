// Lists the N largest functions in the program by body size (bytes),
// largest first. Useful for re-finding a distinctive giant function (a
// state machine, a big switch/coroutine) after a recompile when no
// string or constant anchor is available.
// Run via analyzeHeadless ... -postScript ListLargestFunctions.java [N]
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;

import java.util.*;

public class ListLargestFunctions extends GhidraScript {

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        int limit = args.length >= 1 ? Integer.parseInt(args[0]) : 30;

        List<Function> funcs = new ArrayList<>();
        for (Function f : currentProgram.getFunctionManager().getFunctions(true)) {
            if (!f.isExternal()) {
                funcs.add(f);
            }
        }
        funcs.sort((a, b) -> Long.compare(b.getBody().getNumAddresses(), a.getBody().getNumAddresses()));

        int n = Math.min(limit, funcs.size());
        for (int i = 0; i < n; i++) {
            Function f = funcs.get(i);
            println("  " + f.getBody().getNumAddresses() + "\t" + f.getEntryPoint() + "\t" + f.getName());
        }
    }
}
