// Searches the program's memory for a raw byte pattern (Ghidra's search
// syntax: hex byte pairs separated by spaces, "??" as a wildcard byte),
// and prints every match address plus the containing function.
// Run via analyzeHeadless ... -postScript FindBytePattern.java "<pattern>" [pattern2 ...]
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.mem.Memory;

public class FindBytePattern extends GhidraScript {

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 1) {
            println("Usage: FindBytePattern \"AA BB ?? CC\" [pattern2 ...]");
            return;
        }
        Memory mem = currentProgram.getMemory();
        for (String patternStr : args) {
            String[] tokens = patternStr.trim().split("\\s+");
            byte[] bytes = new byte[tokens.length];
            byte[] masks = new byte[tokens.length];
            for (int i = 0; i < tokens.length; i++) {
                if (tokens[i].equals("??")) {
                    bytes[i] = 0;
                    masks[i] = 0;
                } else {
                    bytes[i] = (byte) Integer.parseInt(tokens[i], 16);
                    masks[i] = (byte) 0xFF;
                }
            }
            println("=== pattern: " + patternStr + " ===");
            Address start = currentProgram.getMinAddress();
            int count = 0;
            Address found = mem.findBytes(start, bytes, masks, true, monitor);
            while (found != null) {
                Function f = currentProgram.getFunctionManager().getFunctionContaining(found);
                println("  " + found + " in " + (f != null ? (f.getName() + "@" + f.getEntryPoint()) : "(no function)"));
                count++;
                if (count > 200) {
                    println("  ...stopping after 200 matches");
                    break;
                }
                Address next = found.add(1);
                found = mem.findBytes(next, bytes, masks, true, monitor);
            }
            println("  total: " + count);
        }
    }
}
