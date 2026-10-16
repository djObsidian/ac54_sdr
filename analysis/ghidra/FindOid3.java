// Step 3: decompile the main command dispatcher (user-buffer parser), the
// OID read/result path, and the arg parser / output-writer helpers.
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.symbol.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.address.*;
import java.io.*;

public class FindOid3 extends GhidraScript {

    void dumpFn(DecompInterface decomp, Function f, StringBuilder out, String label) {
        if (f == null) return;
        out.append("\n====================================================================\n");
        out.append(label + "  " + f.getName() + "  @ 0x" + Long.toHexString(f.getEntryPoint().getOffset()) + "\n");
        out.append("====================================================================\n");
        try {
            DecompileResults res = decomp.decompileFunction(f, 260, getMonitor());
            if (res != null && res.decompileCompleted()) {
                out.append(res.getDecompiledFunction().getC());
            } else {
                out.append("(decompile failed: " + (res != null ? res.getErrorMessage() : "null") + ")\n");
            }
        } catch (Exception e) {
            out.append("(decompile exception: " + e + ")\n");
        }
    }

    @Override
    public void run() throws Exception {
        String outPath = "/D:/workspace/perdolinque/ac54_sdr/docs/oid_find3.txt";
        AddressSpace space = currentProgram.getAddressFactory().getDefaultAddressSpace();
        FunctionManager fman = currentProgram.getFunctionManager();
        DecompInterface decomp = new DecompInterface();
        decomp.openProgram(currentProgram);
        StringBuilder out = new StringBuilder();
        out.append("# FindOid3 — main dispatcher + OID read path + arg parser\n");

        dumpFn(decomp, fman.getFunctionContaining(space.getAddress(0x14017e690L)), out,
            "MAIN COMMAND DISPATCHER (user-buffer parser, Echo/fw/jumptable)");
        dumpFn(decomp, fman.getFunctionContaining(space.getAddress(0x14017ae28L)), out,
            "OID 0x818041 handler (query/result)");
        dumpFn(decomp, fman.getFunctionContaining(space.getAddress(0x14017adf8L)), out,
            "OID 0x818042 handler (read output)");
        dumpFn(decomp, fman.getFunctionContaining(space.getAddress(0x14017aff0L)), out,
            "arg parser (decimal/hex/char)");
        dumpFn(decomp, fman.getFunctionContaining(space.getAddress(0x1403988c0L)), out,
            "output-buffer writer (snprintf-style)");
        dumpFn(decomp, fman.getFunctionContaining(space.getAddress(0x140018700L)), out,
            "NDIS OID request handler (full, for cross-ref of 0x818040/41/42)");

        decomp.dispose();
        String report = out.toString();
        try (FileWriter w = new FileWriter(outPath)) { w.write(report); }
        println("WROTE " + outPath + " (" + report.length() + " chars)");
    }
}
