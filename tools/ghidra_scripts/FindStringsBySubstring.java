// Finds defined string data whose value contains a substring (case-insensitive),
// and prints the functions that reference each match.
// Run via analyzeHeadless ... -postScript FindStringsBySubstring.java <substring> [substring2 ...]
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Data;
import ghidra.program.model.listing.DataIterator;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;

public class FindStringsBySubstring extends GhidraScript {

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 1) {
            println("Usage: FindStringsBySubstring <substring> [substring2 ...]");
            return;
        }
        DataIterator it = currentProgram.getListing().getDefinedData(true);
        while (it.hasNext()) {
            Data d = it.next();
            if (!d.hasStringValue()) continue;
            Object val = d.getValue();
            if (val == null) continue;
            String s = val.toString();
            for (String needle : args) {
                if (s.toLowerCase().contains(needle.toLowerCase())) {
                    Address addr = d.getAddress();
                    println("=== \"" + s + "\" at " + addr + " ===");
                    ReferenceIterator refs = currentProgram.getReferenceManager().getReferencesTo(addr);
                    int count = 0;
                    while (refs.hasNext()) {
                        Reference r = refs.next();
                        Address from = r.getFromAddress();
                        Function caller = currentProgram.getFunctionManager().getFunctionContaining(from);
                        println("  " + from + "\t" + r.getReferenceType() + "\tin " +
                            (caller != null ? (caller.getName() + "@" + caller.getEntryPoint()) : "(no function)"));
                        count++;
                    }
                    println("  total refs: " + count);
                    break;
                }
            }
        }
    }
}
