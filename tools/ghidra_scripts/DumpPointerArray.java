// Reads a raw array of 8-byte pointers starting at <address>, prints each
// one, and resolves it to a containing Function name/entry point if one
// exists there. Run via analyzeHeadless ... -postScript
// DumpPointerArray.java <address> <count> [strideBytes]
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.mem.Memory;

public class DumpPointerArray extends GhidraScript {

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 2) {
            println("Usage: DumpPointerArray <address> <count> [strideBytes=8]");
            return;
        }
        Address start = currentProgram.getAddressFactory().getAddress(args[0]);
        int count = Integer.parseInt(args[1]);
        int stride = args.length >= 3 ? Integer.parseInt(args[2]) : 8;

        Memory mem = currentProgram.getMemory();
        for (int i = 0; i < count; i++) {
            Address entryAddr = start.add((long) i * stride);
            long ptrVal = mem.getLong(entryAddr);
            Address target = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(ptrVal);
            Function f = currentProgram.getFunctionManager().getFunctionContaining(target);
            String desc = (f != null) ? (f.getName() + " @ " + f.getEntryPoint()) : "(no function)";
            println(entryAddr + "  [" + i + "] -> " + String.format("0x%x", ptrVal) + "  " + desc);
        }
    }
}
