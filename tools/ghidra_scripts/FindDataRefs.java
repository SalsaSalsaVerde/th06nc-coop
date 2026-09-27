// Finds all instructions that reference a given data address, and the
// function each reference is in. Useful for finding who reads/writes a
// specific global variable.
// Run via analyzeHeadless ... -postScript FindDataRefs.java <addr> [addr2 ...]
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;
import ghidra.program.model.address.Address;

public class FindDataRefs extends GhidraScript {

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 1) {
            println("Usage: FindDataRefs <addr> [addr2 ...]");
            return;
        }
        for (String addrStr : args) {
            Address addr = currentProgram.getAddressFactory().getAddress(addrStr);
            println("=== refs to " + addrStr + " ===");
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
            println("  total: " + count);
        }
    }
}
