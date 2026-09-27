// For rollback snapshots (docs/11): finds the parts of .data that only
// library code touches. Every code reference whose target lies in .data is
// attributed to its source function; a run of referenced .data addresses
// touched only from functions at or above <libraryStart> is a library-owned
// block that the snapshot must not restore. A run ends at the first address
// a game function touches, or at an unreferenced gap wider than <maxGap>
// bytes (a large unreferenced stretch is usually a pool the game reaches
// through a base pointer, not library data). Prints the runs as
// [start, end) RVAs, largest first, plus a summary.
//
// Run via analyzeHeadless ... -postScript MapLibraryData.java <libraryStart> [minRunBytes] [maxGap]
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressIterator;
import ghidra.program.model.listing.Function;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceManager;

import java.util.*;

public class MapLibraryData extends GhidraScript {

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 1) {
            println("Usage: MapLibraryData <libraryStart> [minRunBytes] [maxGap]");
            return;
        }
        long libraryStart = Long.parseLong(args[0].replace("0x", ""), 16);
        long minRun = args.length > 1 ? Long.parseLong(args[1]) : 256;
        long maxGap = args.length > 2 ? Long.parseLong(args[2]) : 512;

        MemoryBlock data = currentProgram.getMemory().getBlock(".data");
        if (data == null) {
            println("no .data block");
            return;
        }
        long dataStart = data.getStart().getOffset();
        long dataEnd = data.getEnd().getOffset() + 1;
        long imageBase = currentProgram.getImageBase().getOffset();

        // Every referenced .data address: is any reference from game code
        // (a function below the threshold)?
        TreeMap<Long, Boolean> gameTouched = new TreeMap<>();
        TreeMap<Long, Integer> refCount = new TreeMap<>();
        ReferenceManager rm = currentProgram.getReferenceManager();
        AddressIterator dests = rm.getReferenceDestinationIterator(data.getStart(), true);
        while (dests.hasNext()) {
            Address to = dests.next();
            long toOff = to.getOffset();
            if (toOff >= dataEnd) break;
            if (toOff < dataStart) continue;
            for (Reference ref : rm.getReferencesTo(to)) {
                Address from = ref.getFromAddress();
                if (!from.isMemoryAddress()) continue;
                Function f = currentProgram.getFunctionManager().getFunctionContaining(from);
                long fromAddr = f != null ? f.getEntryPoint().getOffset() : from.getOffset();
                boolean fromGame = fromAddr < libraryStart;
                gameTouched.merge(toOff, fromGame, (a, b) -> a || b);
                refCount.merge(toOff, 1, Integer::sum);
            }
        }

        List<long[]> runs = new ArrayList<>(); // start, end, refs
        long runStart = -1, prev = -1;
        int runRefs = 0;
        for (Map.Entry<Long, Boolean> e : gameTouched.entrySet()) {
            long a = e.getKey();
            boolean gap = runStart >= 0 && a - prev > maxGap;
            if (e.getValue() || gap) {
                if (runStart >= 0) runs.add(new long[] { runStart, prev + 8, runRefs });
                runStart = -1;
                runRefs = 0;
            }
            if (!e.getValue()) {
                if (runStart < 0) runStart = a;
                runRefs += refCount.get(a);
            }
            prev = a;
        }
        if (runStart >= 0) runs.add(new long[] { runStart, prev + 8, runRefs });

        runs.sort((x, y) -> Long.compare(y[1] - y[0], x[1] - x[0]));
        long total = 0;
        int shown = 0;
        println(String.format("%d referenced .data addresses; library-only runs in .data [%X, %X), library code >= %X, min run %d, max gap %d:",
                gameTouched.size(), dataStart - imageBase, dataEnd - imageBase, libraryStart - imageBase, minRun, maxGap));
        for (long[] r : runs) {
            long len = r[1] - r[0];
            if (len < minRun) continue;
            total += len;
            shown++;
            println(String.format("  { 0x%06X, 0x%06X }, // %d bytes, %d refs", r[0] - imageBase, r[1] - imageBase, len, r[2]));
        }
        println(String.format("%d runs >= %d bytes, %d bytes total (%d runs in all)", shown, minRun, total, runs.size()));
    }
}
