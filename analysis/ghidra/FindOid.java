// Find the NDIS OID/IOCTL door: who references "Process commands: %d, Unknown ioctl: %d",
// who writes DAT_1408e5b40, and who references ZwDeviceIoControlFile. Decompile all.
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.symbol.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.address.*;
import ghidra.program.model.mem.*;
import java.io.*;
import java.util.*;

public class FindOid extends GhidraScript {

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
            if (++n > 60) { out.append("   ...(truncated)\n"); break; }
        }
    }

    @Override
    public void run() throws Exception {
        String outPath = "/D:/workspace/perdolinque/ac54_sdr/docs/oid_find.txt";
        AddressSpace space = currentProgram.getAddressFactory().getDefaultAddressSpace();
        FunctionManager fman = currentProgram.getFunctionManager();
        DecompInterface decomp = new DecompInterface();
        decomp.openProgram(currentProgram);
        StringBuilder out = new StringBuilder();
        out.append("# FindOid — NDIS OID/IOCTL door for rtwlanu.sys command bus\n");

        // 1. the "Process commands" string anchor
        Address sProcess = space.getAddress(0x14040a4e1L);
        out.append("\n## string anchor 0x14040a4e1: ");
        out.append(readCStr(sProcess, 64));
        refsOf(sProcess, out, "Process-commands string");
        // decompile functions referencing it (direct or via nearby string head)
        Set<Long> done = new HashSet<>();
        ReferenceIterator it = currentProgram.getReferenceManager().getReferencesTo(sProcess);
        while (it.hasNext()) {
            Reference r = it.next();
            Function fromFn = fman.getFunctionContaining(r.getFromAddress());
            if (fromFn != null && done.add(fromFn.getEntryPoint().getOffset())) {
                dumpFn(decomp, fromFn, out, "fn referencing 'Process commands' string");
            }
        }

        // 2. DAT_1408e5b40 (command queue global)
        Address gCmd = space.getAddress(0x1408e5b40L);
        refsOf(gCmd, out, "DAT_1408e5b40 cmd-queue global");
        it = currentProgram.getReferenceManager().getReferencesTo(gCmd);
        while (it.hasNext()) {
            Reference r = it.next();
            Function fromFn = fman.getFunctionContaining(r.getFromAddress());
            if (fromFn != null && done.add(fromFn.getEntryPoint().getOffset())) {
                dumpFn(decomp, fromFn, out, "fn referencing cmd-queue global");
            }
        }

        // 3. ZwDeviceIoControlFile string
        Memory mem = currentProgram.getMemory();
        String zw = "ZwDeviceIoControlFile";
        byte[] pattern = zw.getBytes(java.nio.charset.StandardCharsets.UTF_8);
        for (MemoryBlock b : mem.getBlocks()) {
            if (!b.isRead()) continue;
            long start = b.getStart().getOffset();
            long end = Math.min(b.getEnd().getOffset(), start + b.getSize());
            for (long off = start; off + zw.length() <= end; off += 2) {
                byte[] actual = new byte[zw.length()];
                if (mem.getBytes(space.getAddress(off), actual) != zw.length()) continue;
                if (java.util.Arrays.equals(actual, pattern)) {
                    out.append("\n## 'ZwDeviceIoControlFile' string found at 0x" + Long.toHexString(off) + "\n");
                    refsOf(space.getAddress(off), out, "ZwDeviceIoControlFile string");
                    ReferenceIterator it2 = currentProgram.getReferenceManager().getReferencesTo(space.getAddress(off));
                    while (it2.hasNext()) {
                        Reference r2 = it2.next();
                        Function fromFn2 = fman.getFunctionContaining(r2.getFromAddress());
                        if (fromFn2 != null && done.add(fromFn2.getEntryPoint().getOffset())) {
                            dumpFn(decomp, fromFn2, out, "fn referencing ZwDeviceIoControlFile string");
                        }
                    }
                    break;
                }
            }
        }

        // 4. known NDIS OID constants — search immediate comparisons in .text
        //    vendor/custom OID ranges
        long[] oidCandidates = {
            0x01000001L, 0x01000002L, 0x01000003L, 0x01000004L, 0x01000005L, 0x01000006L,
            0x01018001L, 0x01018002L, 0x01019001L, 0x01019002L,
            0x01010000L, 0x0101ffffL,
            0x00030000L, 0x00020000L, 0x01020000L,
            0x01010100L, 0x01010200L, 0x01010300L, 0x01010400L, 0x01010500L, 0x01010600L,
            0x01010700L, 0x01010800L, 0x01010900L, 0x01010a00L, 0x01010b00L, 0x01010c00L,
            0x01010d00L, 0x01010e00L, 0x01010f00L, 0x01011000L, 0x01011100L, 0x01011200L,
            0x01011300L, 0x01011400L, 0x01011500L, 0x01011600L, 0x01011700L, 0x01011800L,
            0x01011900L, 0x01011a00L, 0x01011b00L, 0x01011c00L, 0x01011d00L, 0x01011e00L,
            0x01011f00L, 0x01012000L, 0x01012100L, 0x01012200L, 0x01012300L, 0x01012400L,
            0x01012500L, 0x01012600L, 0x01012700L, 0x01012800L, 0x01012900L, 0x01012a00L,
            0x01012b00L, 0x01012c00L, 0x01012d00L, 0x01012e00L, 0x01012f00L, 0x01013000L,
            0x01013100L, 0x01013200L, 0x01013300L, 0x01013400L, 0x01013500L, 0x01013600L,
            0x01013700L, 0x01013800L, 0x01013900L, 0x01013a00L, 0x01013b00L, 0x01013c00L,
            0x01013d00L, 0x01013e00L, 0x01013f00L, 0x01014000L, 0x01014100L, 0x01014200L,
            0x01014300L, 0x01014400L, 0x01014500L, 0x01014600L, 0x01014700L, 0x01014800L,
            0x01014900L, 0x01014a00L, 0x01014b00L, 0x01014c00L, 0x01014d00L, 0x01014e00L,
            0x01014f00L, 0x01015000L, 0x01015100L, 0x01015200L, 0x01015300L, 0x01015400L,
            0x01015500L, 0x01015600L, 0x01015700L, 0x01015800L, 0x01015900L, 0x01015a00L,
            0x01015b00L, 0x01015c00L, 0x01015d00L, 0x01015e00L, 0x01015f00L,
        };
        out.append("\n## OID constant candidates found in code (mov/cmp immediates, 4-byte LE in .text) ##\n");
        List<MemoryBlock> textBlocks = new ArrayList<>();
        for (MemoryBlock b : mem.getBlocks()) {
            if (b.getName().contains(".text") || b.getName().equals("Default")) {
                if (b.isExecute()) textBlocks.add(b);
            }
        }
        for (MemoryBlock b : textBlocks) {
            long start = b.getStart().getOffset();
            long size = b.getSize();
            byte[] buf = new byte[Math.min((int) size, 4000000)];
            int read = mem.getBytes(space.getAddress(start), buf);
            for (long oid : oidCandidates) {
                byte[] le = { (byte)oid, (byte)(oid>>>8), (byte)(oid>>>16), (byte)(oid>>>24) };
                outer:
                for (int i = 0; i + 4 <= read; i++) {
                    if (buf[i]==le[0] && buf[i+1]==le[1] && buf[i+2]==le[2] && buf[i+3]==le[3]) {
                        long hit = start + i;
                        Function fn = fman.getFunctionContaining(space.getAddress(hit));
                        if (fn != null) {
                            out.append(String.format("  0x%08x  OID=0x%08x  in %s @0x%x%n",
                                hit & 0xffffffffL, oid, fn.getName(), fn.getEntryPoint().getOffset()));
                        }
                        if (i > 0 && buf[i-1] == (byte)0x38) out.append("        ^^ likely `mov reg, imm` (48/40 prefix)\n");
                        break outer;
                    }
                }
            }
        }

        decomp.dispose();
        String report = out.toString();
        try (FileWriter w = new FileWriter(outPath)) { w.write(report); }
        println("WROTE " + outPath + " (" + report.length() + " chars)");
    }

    String readCStr(Address a, int max) {
        Memory mem = currentProgram.getMemory();
        byte[] buf = new byte[max];
        int n;
        try { n = mem.getBytes(a, buf); } catch (Exception e) { return "(read failed)"; }
        if (n < 0) return "(read failed)";
        int end = 0;
        while (end < n && buf[end] != 0) end++;
        try { return new String(buf, 0, end, "UTF-8"); } catch (Exception e) { return "(bad utf8)"; }
    }
}
