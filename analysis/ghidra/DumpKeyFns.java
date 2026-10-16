// Decompile the NDIS command handler, the dynamic-control core, and the LA/IQ dump entry.
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.symbol.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.address.*;
import java.io.*;

public class DumpKeyFns extends GhidraScript {

    void dump(DecompInterface decomp, FunctionManager fman, AddressSpace space, StringBuilder out, long addr, String label) throws Exception {
        Function f = fman.getFunctionContaining(space.getAddress(addr));
        out.append("\n====================================================================\n");
        out.append(label + "  @ 0x" + Long.toHexString(addr) + (f != null ? "  " + f.getName() : "") + "\n");
        out.append("====================================================================\n");
        if (f == null) { out.append("(no function)\n"); return; }
        DecompileResults res = decomp.decompileFunction(f, 180, getMonitor());
        if (res != null && res.decompileCompleted()) {
            out.append(res.getDecompiledFunction().getC());
        } else {
            out.append("(decompile failed: " + (res != null ? res.getErrorMessage() : "null") + ")\n");
        }
    }

    @Override
    public void run() throws Exception {
        String outPath = "/D:/workspace/perdolinque/ac54_sdr/docs/key_functions.txt";
        AddressSpace space = currentProgram.getAddressFactory().getDefaultAddressSpace();
        FunctionManager fman = currentProgram.getFunctionManager();
        DecompInterface decomp = new DecompInterface();
        decomp.openProgram(currentProgram);
        StringBuilder out = new StringBuilder();
        out.append("# Key functions decompiled for rtwlanu.sys (SDR/IQ-dump focus)\n");

        dump(decomp, fman, space, out, 0x14017b0e0L, "NDIS/IOCTL command handler (caller of dispatcher)");
        dump(decomp, fman, space, out, 0x1400426c0L, "Dynamic Control core (dm target)");
        dump(decomp, fman, space, out, 0x1403a38e0L, "LA/IQ dump entry (start)");
        dump(decomp, fman, space, out, 0x1403a3c40L, "LA is_round_up handler");
        dump(decomp, fman, space, out, 0x1403a4770L, "LA dump start-2 (calls FUN_1403a3800)");

        decomp.dispose();
        String report = out.toString();
        try (FileWriter w = new FileWriter(outPath)) {
            w.write(report);
        }
        println("WROTE " + outPath + " (" + report.length() + " chars)");
    }
}
