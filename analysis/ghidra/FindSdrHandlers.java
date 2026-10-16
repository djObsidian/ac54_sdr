// Ghidra headless script: find SDR-relevant strings, their Xrefs, and the functions that reference them.
import ghidra.app.script.GhidraScript;
import ghidra.program.model.symbol.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.address.*;
import ghidra.program.model.mem.*;
import java.util.*;
import java.io.*;

public class FindSdrHandlers extends GhidraScript {

    static int find(byte[] hay, byte[] needle, int from) {
        outer:
        for (int i = from; i <= hay.length - needle.length; i++) {
            for (int j = 0; j < needle.length; j++) {
                if (hay[i + j] != needle[j]) continue outer;
            }
            return i;
        }
        return -1;
    }

    @Override
    public void run() throws Exception {
        String outPath = "/D:/workspace/perdolinque/ac54_sdr/docs/ghidra_xrefs.txt";
        String[] keys = {
            "Process commands", "Unknown ioctl",
            "Set MAC IQ dump", "BB Setting", "is_fake", "trig_mode", "smp_rate",
            "Trig_Edge", "Trig_Sel", "Dma_type", "Cap_num", "dbg_port",
            "LA Dump finished", "LA Dump more", "Release BB dbg_port",
            "Start to capture",
            "cmd 13 3 0 0/1", "cmd 16 1 2", "dbgcmd 19", "Incorrect cmd index",
            "dumpreg", "shadow operate", "shadow modify map",
            "PSD Pre-Scan Peak", "PSD Scan Peak Freq", "PSD Scan Peak Value",
            "psd_raw_data", "psd_dB_data", "fft_point", "path_sel", "RXIQC",
            "MP/Ant test mode", "path-B TRX", "Test the current ant",
            "ZwDeviceIoControlFile",
            "Phydm Send H2C_ID", "dump h2cq", "phydm_rssi_monitor_h2c",
            "is_round_up", "finish_addr", "Start_Addr", "smp_number",
            "cfg_la_mode",
            "SetFwRsvdPagePkt_8812A", "hal_Associate_8812AU",
            "TxFillCmdDesc8812AUsb", "HalUsbInMpduComplete8812AUsb",
            "odm_read_and_config_mp_8812a_phy_reg_pg_asus",
        };

        Memory memory = currentProgram.getMemory();
        ReferenceManager refMan = currentProgram.getReferenceManager();
        FunctionManager fman = currentProgram.getFunctionManager();

        List<String> out = new ArrayList<>();
        out.add("# Ghidra XREF report for rtwlanu.sys");
        out.add("# program: " + currentProgram.getName());

        for (MemoryBlock block : memory.getBlocks()) {
            if (!block.isRead()) continue;
            long size = block.getSize();
            if (size < 8 || size > 40_000_000L) continue;
            Address bstart = block.getStart();
            byte[] data = new byte[(int) size];
            memory.getBytes(bstart, data);

            for (String key : keys) {
                byte[] needleAscii = key.getBytes("US-ASCII");
                int from = 0;
                while (true) {
                    int idx = find(data, needleAscii, from);
                    if (idx < 0) break;
                    from = idx + 1;
                    Address addr = bstart.add(idx);
                    out.add("### KEY: " + key + "  @ 0x" + Long.toHexString(addr.getOffset()));
                    try {
                        ReferenceIterator it = refMan.getReferencesTo(addr);
                        int n = 0;
                        while (it.hasNext()) {
                            Reference r = it.next();
                            n++;
                            Address fromAddr = r.getFromAddress();
                            Function f = fman.getFunctionContaining(fromAddr);
                            String fn = (f != null) ? f.getName() + " @" + f.getEntryPoint() : "(non-fn)";
                            out.add("    xref from 0x" + Long.toHexString(fromAddr.getOffset()) + "  in  " + fn + "  (" + r.getReferenceType().getName() + ")");
                        }
                        if (n == 0) out.add("    (no direct xref to this exact address)");
                    } catch (Exception e) {
                        out.add("    (refman err: " + e + ")");
                    }
                }
                // UTF-16LE (first hit only)
                byte[] needleUtf16 = key.getBytes("UTF-16LE");
                int idx16 = find(data, needleUtf16, 0);
                if (idx16 >= 0) {
                    Address addr = bstart.add(idx16);
                    out.add("### KEY(UTF16): " + key + "  @ 0x" + Long.toHexString(addr.getOffset()));
                    try {
                        ReferenceIterator it = refMan.getReferencesTo(addr);
                        while (it.hasNext()) {
                            Reference r = it.next();
                            Address fromAddr = r.getFromAddress();
                            Function f = fman.getFunctionContaining(fromAddr);
                            String fn = (f != null) ? f.getName() + " @" + f.getEntryPoint() : "(non-fn)";
                            out.add("    xref from 0x" + Long.toHexString(fromAddr.getOffset()) + "  in  " + fn);
                        }
                    } catch (Exception e) {}
                }
            }
        }

        String report = String.join("\n", out);
        try (FileWriter w = new FileWriter(outPath)) {
            w.write(report);
        }
        println("WROTE " + outPath + "  (" + out.size() + " lines)");
    }
}
