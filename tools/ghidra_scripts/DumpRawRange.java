// Disassembles a raw address range that may not belong to any recognized
// Function (e.g. a local label used only as a function-pointer target).
// Ensures disassembly exists first via disassemble(), then prints
// instructions from start up to (but not including) end.
// Run via analyzeHeadless ... -postScript DumpRawRange.java <start> <end>
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.symbol.Reference;

public class DumpRawRange extends GhidraScript {

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 2) {
            println("Usage: DumpRawRange <start> <end>");
            return;
        }
        Address start = currentProgram.getAddressFactory().getAddress(args[0]);
        Address end = currentProgram.getAddressFactory().getAddress(args[1]);

        if (getInstructionAt(start) == null) {
            disassemble(start);
        }

        Address cur = start;
        while (cur != null && cur.compareTo(end) < 0) {
            Instruction insn = getInstructionAt(cur);
            if (insn == null) {
                println(cur + "\t(no instruction - data or undefined)");
                break;
            }
            String extra = "";
            if (insn.getMnemonicString().toLowerCase().contains("call") ||
                insn.getMnemonicString().toLowerCase().contains("jmp")) {
                for (Reference r : insn.getReferencesFrom()) {
                    var callee = currentProgram.getFunctionManager().getFunctionContaining(r.getToAddress());
                    if (callee != null) {
                        extra = "  ; -> " + callee.getName() + " @ " + callee.getEntryPoint();
                    } else {
                        extra = "  ; -> " + r.getToAddress();
                    }
                }
            }
            println(insn.getAddress() + "\t" + insn + extra);
            cur = insn.getAddress().add(insn.getLength());
        }
    }
}
