// Decompiles a list of function addresses (hex, no 0x prefix) to C-like
// pseudocode, one file per function, into the given output directory.
// Run via analyzeHeadless ... -postScript DecompileTargets.java <outdir> <addr1> <addr2> ...
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;

import java.io.*;

public class DecompileTargets extends GhidraScript {

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 2) {
            println("Usage: DecompileTargets <outdir> <addr1> [addr2 ...]");
            return;
        }
        String outDir = args[0];
        new File(outDir).mkdirs();

        DecompInterface decomp = new DecompInterface();
        decomp.openProgram(currentProgram);
        decomp.setSimplificationStyle("decompile");

        for (int i = 1; i < args.length; i++) {
            String addrStr = args[i];
            Address addr = currentProgram.getAddressFactory().getAddress(addrStr);
            Function f = currentProgram.getFunctionManager().getFunctionAt(addr);
            if (f == null) {
                f = currentProgram.getFunctionManager().getFunctionContaining(addr);
            }
            if (f == null) {
                println("No function at/containing " + addrStr);
                continue;
            }
            DecompileResults res = decomp.decompileFunction(f, 60, monitor);
            String outPath = outDir + "/" + f.getName() + "_" + f.getEntryPoint() + ".c";
            try (PrintWriter pw = new PrintWriter(new FileWriter(outPath))) {
                if (res.decompileCompleted()) {
                    pw.println(res.getDecompiledFunction().getC());
                } else {
                    pw.println("// decompile failed: " + res.getErrorMessage());
                }
            }
            println("Wrote " + outPath);
        }
        decomp.dispose();
    }
}
