// Scans every instruction in the program for a scalar/immediate operand
// equal to one of the given hex values (any operand position), and prints
// the instruction address, mnemonic, and containing function. Useful for
// re-finding a function after a recompile via a distinctive numeric
// constant (a loop stride, a slot count, etc.) instead of a string anchor.
// Run via analyzeHeadless ... -postScript FindImmediateOperand.java <hex1> [hex2 ...]
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.scalar.Scalar;

import java.util.HashSet;
import java.util.Set;

public class FindImmediateOperand extends GhidraScript {

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 1) {
            println("Usage: FindImmediateOperand <hex1> [hex2 ...]");
            return;
        }
        Set<Long> targets = new HashSet<>();
        for (String a : args) {
            targets.add(Long.parseLong(a, 16));
        }

        InstructionIterator it = currentProgram.getListing().getInstructions(true);
        int count = 0;
        while (it.hasNext() && !monitor.isCancelled()) {
            Instruction instr = it.next();
            int numOps = instr.getNumOperands();
            for (int op = 0; op < numOps; op++) {
                Object[] objs = instr.getOpObjects(op);
                for (Object o : objs) {
                    if (o instanceof Scalar) {
                        long val = ((Scalar) o).getSignedValue();
                        long uval = ((Scalar) o).getUnsignedValue();
                        if (targets.contains(val) || targets.contains(uval)) {
                            Address addr = instr.getAddress();
                            Function f = currentProgram.getFunctionManager().getFunctionContaining(addr);
                            println("  " + addr + "\t" + instr.toString() + "\tin " +
                                (f != null ? (f.getName() + "@" + f.getEntryPoint()) : "(no function)"));
                            count++;
                            if (count > 500) {
                                println("  ...stopping after 500 matches");
                                return;
                            }
                        }
                    }
                }
            }
        }
        println("total: " + count);
    }
}
