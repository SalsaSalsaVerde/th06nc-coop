// Headless recon dump for th06nc.exe: named functions, RTTI-derived classes,
// and functions that reference a set of interesting strings.
// Run via analyzeHeadless ... -process th06nc.exe -postScript ExtractRecon.java <outdir>
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.symbol.*;
import ghidra.program.model.address.Address;
import ghidra.program.model.data.DataType;
import ghidra.program.model.listing.Data;
import ghidra.program.model.listing.Listing;
import ghidra.program.model.listing.GhidraClass;

import java.io.*;
import java.util.*;

public class ExtractRecon extends GhidraScript {

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        String outDir = args.length > 0 ? args[0] : ".";
        new File(outDir).mkdirs();

        dumpNamedFunctions(outDir + "/named_functions.txt");
        dumpClasses(outDir + "/rtti_classes.txt");
        dumpStringXrefs(outDir + "/string_xrefs.txt");
    }

    private void dumpNamedFunctions(String path) throws IOException {
        try (PrintWriter pw = new PrintWriter(new FileWriter(path))) {
            FunctionManager fm = currentProgram.getFunctionManager();
            int count = 0;
            for (Function f : fm.getFunctions(true)) {
                String name = f.getName();
                // Skip Ghidra's auto-generated FUN_xxxxxx placeholder names
                if (name.startsWith("FUN_")) continue;
                pw.printf("%s\t%s\t%s%n", f.getEntryPoint(), name, f.getSignature());
                count++;
            }
            println("Named (non-FUN_) functions: " + count);
        }
    }

    private void dumpClasses(String path) throws IOException {
        try (PrintWriter pw = new PrintWriter(new FileWriter(path))) {
            SymbolTable st = currentProgram.getSymbolTable();
            Iterator<GhidraClass> it = st.getClassNamespaces();
            int count = 0;
            while (it.hasNext()) {
                GhidraClass gc = it.next();
                pw.println(gc.getName(true));
                count++;
            }
            println("RTTI/class namespaces: " + count);
        }
    }

    private void dumpStringXrefs(String path) throws IOException {
        String[] targets = new String[] {
            "th06.cfg",
            "replay/th6_",
            "EXE_SPELL_PRACTICE_SAVE_REPLAY",
            "EXE_PAUSE_RETURN_REPLAY_SELECT",
            "SteamAPI_Init",
            "D3D11CreateDevice",
            "XInputGetState",
            "Steam overlay now active",
            "T6RP",
        };

        try (PrintWriter pw = new PrintWriter(new FileWriter(path))) {
            Listing listing = currentProgram.getListing();
            for (String target : targets) {
                pw.println("=== " + target + " ===");
                List<Data> matches = findStringData(listing, target);
                if (matches.isEmpty()) {
                    pw.println("  (string not found as defined Data)");
                    continue;
                }
                for (Data data : matches) {
                    Address addr = data.getAddress();
                    Object val = data.getValue();
                    pw.println("  \"" + val + "\" at " + addr);
                    ReferenceIterator refs = currentProgram.getReferenceManager().getReferencesTo(addr);
                    while (refs.hasNext()) {
                        Reference r = refs.next();
                        Address from = r.getFromAddress();
                        Function f = findContainingFunction(from);
                        pw.printf("    ref from %s in function %s%n", from,
                            f != null ? (f.getName() + "@" + f.getEntryPoint()) : "(no function)");
                    }
                }
            }
        }
    }

    private List<Data> findStringData(Listing listing, String target) {
        List<Data> results = new ArrayList<>();
        for (Data d : listing.getDefinedData(true)) {
            try {
                Object val = d.getValue();
                if (val instanceof String && ((String) val).contains(target)) {
                    results.add(d);
                }
            } catch (Exception e) {
                // ignore
            }
        }
        return results;
    }

    private Function findContainingFunction(Address addr) {
        return currentProgram.getFunctionManager().getFunctionContaining(addr);
    }
}
