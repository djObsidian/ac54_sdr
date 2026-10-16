// Dump the full command dispatch table (array of 40-byte structs) with decoded strings.
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.mem.*;
import ghidra.program.model.listing.*;
import java.io.*;

public class DumpCmdTable2 extends GhidraScript {

    static long rd8(Memory m, AddressSpace s, long a) throws Exception {
        byte[] b = new byte[8];
        m.getBytes(s.getAddress(a), b);
        long v = 0;
        for (int i = 0; i < 8; i++) v |= ((long)(b[i] & 0xff)) << (8 * i);
        return v;
    }

    static String rdstr(Memory m, AddressSpace s, long a, int max) {
        if (a < 0x140000000L || a > 0x140a00000L) return "(bad ptr)";
        byte[] b = new byte[max];
        try { m.getBytes(s.getAddress(a), b); } catch (Exception e) { return "(read err)"; }
        StringBuilder sb = new StringBuilder();
        for (byte c : b) {
            if (c == 0) break;
            sb.append((char) c);
        }
        return sb.toString();
    }

    @Override
    public void run() throws Exception {
        String outPath = "/D:/workspace/perdolinque/ac54_sdr/docs/cmd_table.txt";
        AddressSpace space = currentProgram.getAddressFactory().getDefaultAddressSpace();
        Memory memory = currentProgram.getMemory();
        FunctionManager fman = currentProgram.getFunctionManager();

        long base = 0x1404dc650L;
        int nentries = 0x23; // valid cmd index < 0x23 (35)
        int stride = 40;    // 5 * 8

        StringBuilder out = new StringBuilder();
        out.append("# Realtek debug command dispatch table @ 0x1404dc650\n");
        out.append("# dispatcher: FUN_14017e690 ; array of 40-byte structs\n");
        out.append("# struct: +0x00 handler | +0x08 data | +0x10 name_str | +0x18 idx_str | +0x20 flag\n");
        out.append("# NOTE: user-facing command number = (array index) + 1\n\n");

        for (int i = 0; i < nentries; i++) {
            long off = base + (long) i * stride;
            long handler = rd8(memory, space, off + 0x00);
            long data = rd8(memory, space, off + 0x08);
            long namePtr = rd8(memory, space, off + 0x10);
            long idxPtr = rd8(memory, space, off + 0x18);
            long flag = rd8(memory, space, off + 0x20);

            String nameStr = rdstr(memory, space, namePtr, 96);
            String idxStr = rdstr(memory, space, idxPtr, 48);

            String hname = "";
            if (handler >= 0x140000000L && handler < 0x140a00000L) {
                Function f = fman.getFunctionContaining(space.getAddress(handler));
                hname = (f != null) ? f.getName() + "@" + f.getEntryPoint() : "(non-fn)";
            }

            out.append(String.format("CMD %2d (idx %2d): handler=0x%012x %s | data=0x%012x | flag=0x%x%n",
                    i + 1, i, handler, hname, data, flag));
            out.append(String.format("       name : \"%s\"%n", nameStr));
            out.append(String.format("       idxtag: \"%s\"%n", idxStr));
        }

        String report = out.toString();
        try (FileWriter w = new FileWriter(outPath)) {
            w.write(report);
        }
        println("WROTE " + outPath);
    }
}
