// Step 2: find callers of the cmd-queue fn FUN_14017aee4, and the true
// "Process commands" string address + its referencing function.
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.symbol.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.address.*;
import ghidra.program.model.mem.*;
import java.io.*;
import java.util.*;

public class FindOid2 extends GhidraScript {

    void dumpFn(DecompInterface decomp, Function f, StringBuilder out, String label) {
        if (f == null) return;
        out.append("\n====================================================================\n");
        out.append(label + "  " + f.getName() + "  @ 0x" + Long.toHexString(f.getEntryPoint().getOffset()) + "\n");
        out.append("====================================================================\n");
        try {
            DecompileResults res = decomp.decompileFunction(f, 240, getMonitor());
            if (res != null && res.decompileCompleted()) {
                out.append(res.getDecompiledFunction().getC());
            } else {
                out.append("(decompile failed: " + (res != null ? res.getErrorMessage() : "null") + ")\n");
            }
        } catch (Exception e) {
            out.append("(decompile exception: " + e + ")\n");
        }
    }

    void refsOf(Address addr, StringBuilder out, String label) {
        out.append("\n---- refs to " + label + " (0x" + Long.toHexString(addr.getOffset()) + ") ----\n");
        ReferenceIterator it = currentProgram.getReferenceManager().getReferencesTo(addr);
        int n = 0;
        while (it.hasNext()) {
            Reference r = it.next();
            Address from = r.getFromAddress();
            Function fromFn = currentProgram.getFunctionManager().getFunctionContaining(from);
            out.append("   from 0x" + Long.toHexString(from.getOffset()) + " in " +
                (fromFn != null ? fromFn.getName() + "@" + Long.toHexString(fromFn.getEntryPoint().getOffset()) : "(non-fn)") +
                " (" + r.getReferenceType().getName() + ")\n");
            if (++n > 80) { out.append("   ...(truncated)\n"); break; }
        }
    }

    @Override
    public void run() throws Exception {
        String outPath = "/D:/workspace/perdolinque/ac54_sdr/docs/oid_find2.txt";
        AddressSpace space = currentProgram.getAddressFactory().getDefaultAddressSpace();
        FunctionManager fman = currentProgram.getFunctionManager();
        Memory mem = currentProgram.getMemory();
        DecompInterface decomp = new DecompInterface();
        decomp.openProgram(currentProgram);
        StringBuilder out = new StringBuilder();
        out.append("# FindOid2 — callers of cmd-queue fn + Process-commands string\n");

        // 1. callers of FUN_14017aee4 (queue)
        refsOf(space.getAddress(0x14017aee4L), out, "queue fn FUN_14017aee4");
        Set<Long> done = new HashSet<>();
        ReferenceIterator it = currentProgram.getReferenceManager().getReferencesTo(space.getAddress(0x14017aee4L));
        while (it.hasNext()) {
            Reference r = it.next();
            Function fromFn = fman.getFunctionContaining(r.getFromAddress());
            if (fromFn != null && done.add(fromFn.getEntryPoint().getOffset())) {
                dumpFn(decomp, fromFn, out, "CALLER of queue fn (likely NDIS OID handler)");
            }
        }

        // 2. true address of "Process commands: %d"
        byte[] pat = "Process commands: %d, Unknown ioctl: %d".getBytes(java.nio.charset.StandardCharsets.UTF_8);
        for (MemoryBlock b : mem.getBlocks()) {
            if (!b.isRead()) continue;
            long start = b.getStart().getOffset();
            long end = Math.min(b.getEnd().getOffset(), start + b.getSize());
            for (long off = start; off + pat.length <= end; off += 2) {
                byte[] actual = new byte[pat.length];
                if (mem.getBytes(space.getAddress(off), actual) != pat.length) continue;
                if (java.util.Arrays.equals(actual, pat)) {
                    out.append("\n## 'Process commands' string at 0x" + Long.toHexString(off) + "\n");
                    Address saddr = space.getAddress(off);
                    refsOf(saddr, out, "Process-commands string (true addr)");
                    ReferenceIterator it2 = currentProgram.getReferenceManager().getReferencesTo(saddr);
                    while (it2.hasNext()) {
                        Reference r2 = it2.next();
                        Function fromFn2 = fman.getFunctionContaining(r2.getFromAddress());
                        if (fromFn2 != null && done.add(fromFn2.getEntryPoint().getOffset())) {
                            dumpFn(decomp, fromFn2, out, "fn referencing Process-commands string");
                        }
                    }
                }
            }
        }

        // 3. decompile FUN_140400cc0 (the copy helper used by queue fn)
        Function cp = fman.getFunctionContaining(space.getAddress(0x140400cc0L));
        if (cp != null) dumpFn(decomp, cp, out, "copy helper FUN_140400cc0");

        // 4. the OidCustom handler fns — decompile each (they contain the OID immediates)
        long[] cands = { 0x1400248ccL, 0x140008350L, 0x14000eee0L, 0x140026a5cL, 0x140035068L, 0x140046d50L };
        for (long a : cands) {
            Function f = fman.getFunctionContaining(space.getAddress(a));
            if (f != null && done.add(f.getEntryPoint().getOffset())) {
                dumpFn(decomp, f, out, "OidCustom handler candidate");
            }
        }

        decomp.dispose();
        String report = out.toString();
        try (FileWriter w = new FileWriter(outPath)) { w.write(report); }
        println("WROTE " + outPath + " (" + report.length() + " chars)");
    }
}
