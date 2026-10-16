// Dump the phydm command name table @ DAT_14066d560 (49 entries x 17 bytes: keyword + 1 index byte)
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.mem.*;
import ghidra.program.model.listing.*;
import java.io.*;

public class DumpPhydmNames extends GhidraScript {

    @Override
    public void run() throws Exception {
        String outPath = "/D:/workspace/perdolinque/ac54_sdr/docs/phydm_cmd_names.txt";
        AddressSpace space = currentProgram.getAddressFactory().getDefaultAddressSpace();
        Memory memory = currentProgram.getMemory();
        FunctionManager fman = currentProgram.getFunctionManager();

        long base = 0x14066d560L;
        int nentries = 0x31; // 49
        int stride = 0x11;  // 17

        // map index -> handler (from FUN_140386620 switch)
        String[] handler = new String[0x31];
        handler[0x01] = "arg-parser demo (dec/hex/char/str)";
        handler[0x02] = "FUN_1403bc130";
        handler[0x03] = "FUN_1403a0c60";
        handler[0x04] = "FUN_140394a70";
        handler[0x05] = "FUN_140384c60";
        handler[0x06] = "FUN_140391630";
        handler[0x08] = "FUN_1403870d0";
        handler[0x09] = "FUN_140389df0";
        handler[0x0a] = "FUN_140388db0";
        handler[0x0b] = "FUN_14037d4a0";
        handler[0x0c] = "FUN_140389c70";
        handler[0x0d] = "FUN_14038bfe0";
        handler[0x0f] = "FUN_140386480";
        handler[0x10] = "FUN_1403991a0";
        handler[0x11] = "FUN_1403a4770  *** lamode (IQ/LA DUMP) ***";
        handler[0x12] = "FUN_1403888a0  *** dumpreg (0:all,1:BB,2:RF,3:MAC) ***";
        handler[0x13] = "FUN_1403b2110";
        handler[0x14] = "FUN_140386fc0";
        handler[0x15] = "FUN_140388c10";
        handler[0x16] = "FUN_14038c790";
        handler[0x17] = "FUN_140399d00";
        handler[0x18] = "FUN_140399600";
        handler[0x19] = "FUN_1403b1440";
        handler[0x1b] = "FUN_1403b85c0";
        handler[0x1c] = "FUN_1403b6bd0";
        handler[0x1d] = "FUN_1403b7ce0";
        handler[0x1e] = "FUN_1403b76a0";
        handler[0x1f] = "FUN_1403853c0";
        handler[0x20] = "txbf setting (0/1/2)";
        handler[0x21] = "FUN_140394580  (Phydm Send H2C_ID)";
        handler[0x22] = "FUN_140388d00";
        handler[0x23] = "FUN_1403ad8a0";
        handler[0x24] = "FUN_1403b9eb0";
        handler[0x25] = "FUN_14038af80";
        handler[0x26] = "HT-STF Gain Control on/off";
        handler[0x27] = "FUN_1403b6200";
        handler[0x28] = "FUN_1403a2b00";
        handler[0x29] = "FUN_140398960";
        handler[0x2a] = "FUN_1403b2f30";
        handler[0x2b] = "FUN_1403956e0";
        handler[0x2c] = "FUN_14037cff0";
        handler[0x2d] = "FUN_14038a880";
        handler[0x2e] = "FUN_1403b5870";

        StringBuilder out = new StringBuilder();
        out.append("# phydm command name table @ 0x14066d560 (49 x 17B entries)\n");
        out.append("# entry layout: [16B keyword][1B index] ; index = cmd id used by FUN_140386620\n\n");

        for (int i = 0; i < nentries; i++) {
            long off = base + (long) i * stride;
            byte[] kw = new byte[16];
            memory.getBytes(space.getAddress(off), kw);
            StringBuilder s = new StringBuilder();
            for (byte c : kw) { if (c == 0) break; s.append((char) c); }
            byte[] ib = new byte[1];
            memory.getBytes(space.getAddress(off + 16), ib);
            int idx = ib[0] & 0xff;
            String h = (idx < handler.length && handler[idx] != null) ? handler[idx] : "(idx " + idx + ")";
            out.append(String.format("%2d  idx=0x%02x  \"%s\"   ->  %s%n", i, idx, s, h));
        }

        String report = out.toString();
        try (FileWriter w = new FileWriter(outPath)) { w.write(report); }
        println("WROTE " + outPath);
    }
}
