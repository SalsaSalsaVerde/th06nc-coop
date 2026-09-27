// Dumps raw disassembly (mnemonic + operands) of a function, annotating CALL
// targets with the callee's function name if known.
// Run via analyzeHeadless ... -postScript DumpDisasm.java <addr>
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.symbol.Reference;

public class DumpDisasm extends GhidraScript {

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 1) {
            println("Usage: DumpDisasm <addr>");
            return;
        }
        Address addr = currentProgram.getAddressFactory().getAddress(args[0]);
        Function f = currentProgram.getFunctionManager().getFunctionContaining(addr);
        if (f == null) {
            println("No function at " + addr);
            return;
        }
        println("Function: " + f.getName() + " @ " + f.getEntryPoint());
        InstructionIterator it = currentProgram.getListing().getInstructions(f.getBody(), true);
        while (it.hasNext()) {
            Instruction insn = it.next();
            String extra = "";
            if (insn.getMnemonicString().toLowerCase().contains("call")) {
                Reference[] refs = insn.getReferencesFrom();
                for (Reference r : refs) {
                    Function callee = currentProgram.getFunctionManager().getFunctionAt(r.getToAddress());
                    if (callee != null) {
                        extra = "  ; -> " + callee.getName() + " @ " + callee.getEntryPoint();
                    }
                }
            }
            println(insn.getAddress() + "\t" + insn + extra);
        }
    }
}
