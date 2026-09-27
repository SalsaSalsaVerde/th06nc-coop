// Hex-dumps N raw bytes starting at <address>, one line per 16 bytes, for
// external (Python) parsing of a data structure. Run via analyzeHeadless
// ... -postScript DumpBytes.java <address> <count>
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.mem.Memory;

public class DumpBytes extends GhidraScript {

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 2) {
            println("Usage: DumpBytes <address> <count>");
            return;
        }
        Address start = currentProgram.getAddressFactory().getAddress(args[0]);
        int count = Integer.parseInt(args[1]);
        Memory mem = currentProgram.getMemory();
        byte[] buf = new byte[count];
        mem.getBytes(start, buf);
        StringBuilder sb = new StringBuilder();
        for (int i = 0; i < count; i++) {
            sb.append(String.format("%02x", buf[i]));
            if (i < count - 1) sb.append(" ");
        }
        println("HEXDUMP " + start + " " + count + " " + sb.toString());
    }
}
