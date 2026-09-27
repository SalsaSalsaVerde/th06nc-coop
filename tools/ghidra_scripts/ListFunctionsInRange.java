// Lists all functions (name, entry, size) whose entry point falls within
// [startAddr, endAddr), both given as hex Ghidra addresses (no 0x prefix).
// Run via analyzeHeadless ... -postScript ListFunctionsInRange.java <start> <end>
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import ghidra.program.model.address.Address;

public class ListFunctionsInRange extends GhidraScript {

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 2) {
            println("Usage: ListFunctionsInRange <startAddr> <endAddr>");
            return;
        }
        Address start = currentProgram.getAddressFactory().getAddress(args[0]);
        Address end = currentProgram.getAddressFactory().getAddress(args[1]);
        for (Function f : currentProgram.getFunctionManager().getFunctions(true)) {
            Address ep = f.getEntryPoint();
            if (ep.compareTo(start) >= 0 && ep.compareTo(end) < 0) {
                println(ep + "\t" + f.getName() + "\tsize=" + f.getBody().getNumAddresses());
            }
        }
    }
}
