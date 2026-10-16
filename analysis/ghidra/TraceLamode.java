import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.symbol.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.address.*;
import java.io.*;

public class TraceLamode extends GhidraScript {

    void dump(DecompInterface decomp, FunctionManager fman, AddressSpace space, StringBuilder out, long addr, String label) throws Exception {
        Function f = fman.getFunctionContaining(space.getAddress(addr));
        out.append("\n====================================================================\n");
        out.append(label + "  @ 0x" + Long.toHexString(addr) + (f != null ? "  " + f.getName() : "") + "\n");
        out.append("====================================================================\n");
        if (f == null) { out.append("(no function)\n"); return; }
        DecompileResults res = decomp.decompileFunction(f, 180, getMonitor());
        if (res != null && res.decompileCompleted()) out.append(res.getDecompiledFunction().getC());
        else out.append("(decompile failed: " + (res != null ? res.getErrorMessage() : "null") + ")\n");
    }

    @Override
    public void run() throws Exception {
        String outPath = "/D:/workspace/perdolinque/ac54_sdr/docs/lamode_path.txt";
        AddressSpace space = currentProgram.getAddressFactory().getDefaultAddressSpace();
        FunctionManager fman = currentProgram.getFunctionManager();
        ReferenceManager refMan = currentProgram.getReferenceManager();
        DecompInterface decomp = new DecompInterface();
        decomp.openProgram(currentProgram);
        StringBuilder out = new StringBuilder();
        out.append("# lamode (IQ dump) trigger path + LA-start config\n");

        // callers of lamode (FUN_1403a4770) and LA start (FUN_1403a3800)
        long[] targets = { 0x1403a4770L, 0x1403a3800L, 0x140175bc0L };
        String[] labels = { "lamode (IQ dump cmd)", "LA start (register config)", "uidbg (CMD 6)" };
        for (int i = 0; i < targets.length; i++) {
            Function tf = fman.getFunctionContaining(space.getAddress(targets[i]));
            out.append("\n-- callers of " + labels[i] + " --\n");
            ReferenceIterator it = refMan.getReferencesTo(space.getAddress(targets[i]));
            int n = 0;
            while (it.hasNext()) {
                Reference r = it.next();
                Address from = r.getFromAddress();
                Function cf = fman.getFunctionContaining(from);
                out.append("   from 0x" + Long.toHexString(from.getOffset()) + "  in " +
                    (cf != null ? cf.getName() + "@" + cf.getEntryPoint() : "(non-fn)") + "  (" + r.getReferenceType().getName() + ")\n");
                if (++n > 40) break;
            }
            if (n == 0) out.append("   (no direct refs)\n");
        }

        // decompile uidbg + LA start + lamode callers
        dump(decomp, fman, space, out, 0x140175bc0L, "uidbg (CMD 6)");
        dump(decomp, fman, space, out, 0x1403a3800L, "LA start (register config)");

        decomp.dispose();
        String report = out.toString();
        try (FileWriter w = new FileWriter(outPath)) { w.write(report); }
        println("WROTE " + outPath + " (" + report.length() + " chars)");
    }
}
