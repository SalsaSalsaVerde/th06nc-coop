// Creates a Function at each given address if one doesn't already exist
// there (useful for local-label code that's only reached via a stored
// function pointer, which Ghidra's analysis doesn't auto-detect as a
// function entry point). Saves the program so the change persists.
// Run via analyzeHeadless ... -postScript CreateFunctionAt.java <addr1> [addr2 ...]
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;

public class CreateFunctionAt extends GhidraScript {

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 1) {
            println("Usage: CreateFunctionAt <addr1> [addr2 ...]");
            return;
        }
        boolean changed = false;
        for (String addrStr : args) {
            Address addr = currentProgram.getAddressFactory().getAddress(addrStr);
            Function existing = currentProgram.getFunctionManager().getFunctionAt(addr);
            if (existing != null) {
                println(addrStr + ": already a function (" + existing.getName() + ")");
                continue;
            }
            if (getInstructionAt(addr) == null) {
                disassemble(addr);
            }
            Function f = createFunction(addr, null);
            if (f != null) {
                println(addrStr + ": created " + f.getName() + " size=" + f.getBody().getNumAddresses());
                changed = true;
            } else {
                println(addrStr + ": createFunction FAILED");
            }
        }
        if (changed) {
            currentProgram.save("CreateFunctionAt", monitor);
        }
    }
}
