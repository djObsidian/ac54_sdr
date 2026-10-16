// Ghidra headless: decompile a set of functions (by entry address) and dump C to a file.
// Also find all functions that data-reference a given address range (to catch string-table handlers).
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.symbol.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.address.*;
import java.util.*;
import java.io.*;

public class DumpDecompiled extends GhidraScript {

    @Override
    public void run() throws Exception {
        String outPath = "/D:/workspace/perdolinque/ac54_sdr/docs/decompiled.txt";
        AddressSpace space = currentProgram.getAddressFactory().getDefaultAddressSpace();

        // key functions found via XREF report
        long[] fns = {
            0x14017e690L, // Incorrect cmd index  (dispatcher)
            0x1403a3920L, // LA Dump finished / LA Dump more
            0x14038b320L, // Release BB dbg_port
            0x140389910L, // dbg_port
            0x1403888a0L, // dumpreg
            0x14039c700L, // MP/Ant test mode
            0x14038f800L, // Test the current ant
            0x14034d280L, // cfg_la_mode (chip1)
            0x14035f750L, // cfg_la_mode (chip2)
            0x14036cca0L, // cfg_la_mode (chip3)
            0x1403238c0L, // PSD
            0x1402c6db4L, // PSD
            0x14033110cL, // PSD
            0x140394580L, // Phydm Send H2C_ID
            0x1403b1720L, // phydm_rssi_monitor_h2c
            0x1403a3c40L, // is_round_up
            0x1402197c8L, // SetFwRsvdPagePkt_8812A
            0x14016da80L, // hal_Associate_8812AU
        };

        StringBuilder out = new StringBuilder();
        out.append("# Decompiled functions for rtwlanu.sys\n\n");

        DecompInterface decomp = new DecompInterface();
        decomp.openProgram(currentProgram);

        FunctionManager fman = currentProgram.getFunctionManager();
        for (long addr : fns) {
            Address a = space.getAddress(addr);
            Function f = fman.getFunctionContaining(a);
            if (f == null) {
                f = fman.getFunction(addr);
            }
            out.append("====================================================================\n");
            out.append("FUNCTION @ 0x" + Long.toHexString(addr) + "  " + (f != null ? f.getName() : "(null)") + "\n");
            out.append("====================================================================\n");
            if (f == null) {
                out.append("  (no function found)\n\n");
                continue;
            }
            DecompileResults res = decomp.decompileFunction(f, 120, getMonitor());
            if (res != null && res.decompileCompleted()) {
                out.append(res.getDecompiledFunction().getC());
            } else {
                out.append("  (decompile failed: " + (res != null ? res.getErrorMessage() : "null") + ")\n");
            }
            out.append("\n");
        }

        // Find functions that data-reference the IQ-dump / BB string clusters
        out.append("\n\n#===============================================================\n");
        out.append("# Functions with DATA xref into IQ-dump / BB string clusters\n");
        out.append("#===============================================================\n");
        ReferenceManager refMan = currentProgram.getReferenceManager();
        long[][] ranges = {
            {0x140432c00L, 0x140433100L}, // Set MAC IQ dump / is_fake / trig / smp_rate / Cap_num
            {0x140429e40L, 0x140429ea0L}, // Release BB dbg_port / dbg_port
            {0x1404115c0L, 0x140411620L}, // Incorrect cmd index
            {0x14042c280L, 0x14042c2c0L}, // dumpreg
            {0x14042e600L, 0x14042e660L}, // Test the current ant
            {0x140430ef0L, 0x140430f60L}, // MP/Ant test mode / path-B TRX
        };
        Set<String> seen = new HashSet<>();
        for (long[] r : ranges) {
            Address start = space.getAddress(r[0]);
            Address end = space.getAddress(r[1]);
            // iterate addresses in range that are defined
            Address cur = start;
            while (cur.compareTo(end) <= 0) {
                ReferenceIterator it = refMan.getReferencesTo(cur);
                while (it.hasNext()) {
                    Reference ref = it.next();
                    Address from = ref.getFromAddress();
                    Function fromFn = fman.getFunctionContaining(from);
                    if (fromFn != null) {
                        String key = fromFn.getName() + "@" + fromFn.getEntryPoint();
                        if (seen.add(key)) {
                            out.append("  " + key + "   (refs 0x" + Long.toHexString(cur.getOffset()) + ")\n");
                        }
                    }
                }
                cur = cur.add(1);
            }
        }

        String report = out.toString();
        try (FileWriter w = new FileWriter(outPath)) {
            w.write(report);
        }
        decomp.dispose();
        println("WROTE " + outPath + "  (" + report.length() + " chars)");
    }
}
