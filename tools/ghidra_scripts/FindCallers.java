// Finds all functions that call a named function, whether it's a regular
// function or an external (import-table) function like fopen/fwrite.
// Run via analyzeHeadless ... -postScript FindCallers.java <funcName> [funcName2 ...]
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;
import ghidra.program.model.address.Address;

import java.util.*;

public class FindCallers extends GhidraScript {

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 1) {
            println("Usage: FindCallers <funcName> [funcName2 ...]");
            return;
        }
        for (String name : args) {
            println("=== callers of " + name + " ===");
            List<Function> targets = new ArrayList<>();
            for (Function f : currentProgram.getFunctionManager().getFunctions(true)) {
                if (f.getName().equals(name)) {
                    targets.add(f);
                }
            }
            Iterator<Function> extIt = currentProgram.getFunctionManager().getExternalFunctions();
            while (extIt.hasNext()) {
                Function f = extIt.next();
                if (f.getName().equals(name)) {
                    targets.add(f);
                }
            }
            if (targets.isEmpty()) {
                println("  (no function named " + name + " found, regular or external)");
                continue;
            }
            for (Function target : targets) {
                println("  target @ " + target.getEntryPoint() + " external=" + target.isExternal());
                ReferenceIterator refs = currentProgram.getReferenceManager().getReferencesTo(target.getEntryPoint());
                while (refs.hasNext()) {
                    Reference r = refs.next();
                    Address from = r.getFromAddress();
                    Function caller = currentProgram.getFunctionManager().getFunctionContaining(from);
                    println("    call from " + from + " in " +
                        (caller != null ? (caller.getName() + "@" + caller.getEntryPoint()) : "(no function)"));
                }
            }
        }
    }
}
