// 1) Decompile FUN_140175820 (dm / Dynamic Control).
// 2) Find callers of FUN_14017e690 (dispatcher) and the LA-dump fns.
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.symbol.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.address.*;
import java.io.*;

public class TraceDm extends GhidraScript {

    @Override
    public void run() throws Exception {
        String outPath = "/D:/workspace/perdolinque/ac54_sdr/docs/dm_dispatch.txt";
        AddressSpace space = currentProgram.getAddressFactory().getDefaultAddressSpace();
        FunctionManager fman = currentProgram.getFunctionManager();
        ReferenceManager refMan = currentProgram.getReferenceManager();
        SymbolTable sym = currentProgram.getSymbolTable();

        StringBuilder out = new StringBuilder();

        // ---- 1) decompile dm ----
        DecompInterface decomp = new DecompInterface();
        decomp.openProgram(currentProgram);
        Function dm = fman.getFunctionContaining(space.getAddress(0x140175820L));
        out.append("===== dm (Dynamic Control) @ 0x140175820 " + (dm != null ? dm.getName() : "") + " =====\n");
        if (dm != null) {
            DecompileResults res = decomp.decompileFunction(dm, 120, getMonitor());
            out.append((res != null && res.decompileCompleted()) ? res.getDecompiledFunction().getC() : "(fail)");
        }
        decomp.dispose();

        // ---- 2) find callers ----
        long[] targets = {
            0x14017e690L, // dispatcher FUN_14017e690
            0x140175820L, // dm
            0x1403a3920L, // LA query/poll
            0x1403a3c40L, // is_round_up
            0x1403a3800L, // LA start (called from LA Dump more)
        };
        out.append("\n\n===== CALLERS =====\n");
        for (long t : targets) {
            Function tf = fman.getFunctionContaining(space.getAddress(t));
            String tname = (tf != null) ? tf.getName() : ("0x" + Long.toHexString(t));
            out.append("\n-- callers of " + tname + " --\n");
            // references to the function entry
            Address entry = space.getAddress(t);
            ReferenceIterator it = refMan.getReferencesTo(entry);
            int n = 0;
            while (it.hasNext()) {
                Reference r = it.next();
                Address from = r.getFromAddress();
                Function cf = fman.getFunctionContaining(from);
                String cfn = (cf != null) ? cf.getName() + "@" + cf.getEntryPoint() : "(non-fn)";
                out.append("   from 0x" + Long.toHexString(from.getOffset()) + "  in " + cfn + "  (" + r.getReferenceType().getName() + ")\n");
                if (++n > 40) { out.append("   ... (truncated)\n"); break; }
            }
            if (n == 0) out.append("   (no direct refs to entry)\n");
        }

        String report = out.toString();
        try (FileWriter w = new FileWriter(outPath)) {
            w.write(report);
        }
        println("WROTE " + outPath + " (" + report.length() + " chars)");
    }
}
