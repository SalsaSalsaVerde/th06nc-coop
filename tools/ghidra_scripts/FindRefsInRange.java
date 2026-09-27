// Counts every code reference whose target falls inside [start, end), grouped
// by containing function. Useful for sizing how much code touches a whole
// struct by absolute field address (not just its base), e.g. to estimate the
// patch surface of instantiating a second copy of a fixed global struct.
// Run via analyzeHeadless ... -postScript FindRefsInRange.java <start> <end> [listDetail]
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressIterator;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;

import java.util.Map;
import java.util.TreeMap;

public class FindRefsInRange extends GhidraScript {

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 2) {
            println("Usage: FindRefsInRange <start> <end> [listDetail]");
            return;
        }
        Address start = currentProgram.getAddressFactory().getAddress(args[0]);
        Address end = currentProgram.getAddressFactory().getAddress(args[1]);
        boolean detail = args.length > 2;

        Map<String, Integer> perFunction = new TreeMap<>();
        Map<Long, Integer> perOffset = new TreeMap<>();
        int total = 0;
        AddressIterator dests = currentProgram.getReferenceManager().getReferenceDestinationIterator(start, true);
        while (dests.hasNext()) {
            Address to = dests.next();
            if (to.compareTo(end) >= 0) break;
            ReferenceIterator it = currentProgram.getReferenceManager().getReferencesTo(to);
            while (it.hasNext()) {
                Reference ref = it.next();
                Function f = getFunctionContaining(ref.getFromAddress());
                String name = f == null ? "(no function)" : f.getName();
                perFunction.merge(name, 1, Integer::sum);
                perOffset.merge(to.subtract(start), 1, Integer::sum);
                total++;
                if (detail) {
                    println(String.format("  %s -> +0x%X\t%s\tin %s", ref.getFromAddress(),
                            to.subtract(start), ref.getReferenceType(), name));
                }
            }
        }
        println("=== per function ===");
        perFunction.entrySet().stream()
                .sorted((a, b) -> b.getValue() - a.getValue())
                .forEach(e -> println(String.format("  %5d  %s", e.getValue(), e.getKey())));
        println("=== per field offset ===");
        for (Map.Entry<Long, Integer> e : perOffset.entrySet()) {
            println(String.format("  +0x%05X  %d", e.getKey(), e.getValue()));
        }
        println("total refs: " + total + " across " + perFunction.size() + " functions, "
                + perOffset.size() + " distinct offsets");
    }
}
