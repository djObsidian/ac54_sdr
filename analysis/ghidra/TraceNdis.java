// Trace the NDIS entry path: callers of FUN_14017b0e0 and nearby NDIS OID handlers.
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.symbol.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.address.*;
import java.io.*;

public class TraceNdis extends GhidraScript {

    @Override
    public void run() throws Exception {
        String outPath = "/D:/workspace/perdolinque/ac54_sdr/docs/ndis_path.txt";
        AddressSpace space = currentProgram.getAddressFactory().getDefaultAddressSpace();
        FunctionManager fman = currentProgram.getFunctionManager();
        ReferenceManager refMan = currentProgram.getReferenceManager();
        DecompInterface decomp = new DecompInterface();
        decomp.openProgram(currentProgram);
        StringBuilder out = new StringBuilder();
        out.append("# NDIS entry path for rtwlanu.sys debug commands\n");

        long[] chain = { 0x14017b0e0L };
        // walk up callers a few levels
        java.util.Deque<Long> stack = new java.util.ArrayDeque<>();
        java.util.Set<Long> seen = new java.util.HashSet<>();
        stack.push(0x14017b0e0L);
        int depth = 0;
        while (!stack.isEmpty() && depth < 4) {
            long cur = stack.pop();
            Function cf = fman.getFunctionContaining(space.getAddress(cur));
            out.append("\n=== callers of 0x" + Long.toHexString(cur) + " (" + (cf != null ? cf.getName() : "") + ") ===\n");
            ReferenceIterator it = refMan.getReferencesTo(space.getAddress(cur));
            int n = 0;
            while (it.hasNext()) {
                Reference r = it.next();
                Address from = r.getFromAddress();
                Function fromFn = fman.getFunctionContaining(from);
                String type = r.getReferenceType().getName();
                out.append("   from 0x" + Long.toHexString(from.getOffset()) + " in " +
                    (fromFn != null ? fromFn.getName() + "@" + fromFn.getEntryPoint() : "(non-fn)") + " (" + type + ")\n");
                if (fromFn != null && !type.startsWith("DATA") && seen.add(fromFn.getEntryPoint().getOffset()) && fromFn.getEntryPoint().getOffset() != cur) {
                    stack.push(fromFn.getEntryPoint().getOffset());
                }
                if (++n > 30) break;
            }
            depth++;
        }

        // decompile the direct caller of FUN_14017b0e0
        Function c = fman.getFunctionContaining(space.getAddress(0x14017b0e0L));
        ReferenceIterator it = refMan.getReferencesTo(space.getAddress(0x14017b0e0L));
        while (it.hasNext()) {
            Reference r = it.next();
            if (r.getReferenceType().getName().startsWith("UNCONDITIONAL_CALL")) {
                Function fromFn = fman.getFunctionContaining(r.getFromAddress());
                if (fromFn != null) {
                    out.append("\n\n===== DECOMPILE caller of 14017b0e0: " + fromFn.getName() + " =====\n");
                    DecompileResults res = decomp.decompileFunction(fromFn, 120, getMonitor());
                    if (res != null && res.decompileCompleted()) out.append(res.getDecompiledFunction().getC());
                }
            }
        }

        decomp.dispose();
        String report = out.toString();
        try (FileWriter w = new FileWriter(outPath)) { w.write(report); }
        println("WROTE " + outPath + " (" + report.length() + " chars)");
    }
}
