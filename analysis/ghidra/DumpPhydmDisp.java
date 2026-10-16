import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.symbol.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.address.*;
import ghidra.program.model.mem.*;
import java.io.*;

public class DumpPhydmDisp extends GhidraScript {

    @Override
    public void run() throws Exception {
        String outPath = "/D:/workspace/perdolinque/ac54_sdr/docs/phydm_dispatch.txt";
        AddressSpace space = currentProgram.getAddressFactory().getDefaultAddressSpace();
        Memory memory = currentProgram.getMemory();
        FunctionManager fman = currentProgram.getFunctionManager();
        DecompInterface decomp = new DecompInterface();
        decomp.openProgram(currentProgram);
        StringBuilder out = new StringBuilder();
        out.append("# phydm/HAL string command dispatcher + magics\n");

        // read magic bytes
        byte[] m4 = new byte[4];
        memory.getBytes(space.getAddress(0x1404115c0L), m4);
        byte[] m2 = new byte[2];
        memory.getBytes(space.getAddress(0x1404115d0L), m2);
        out.append("magic4 @0x1404115c0 = ");
        for (byte b : m4) out.append(String.format("%02x ", b & 0xff));
        out.append("\nmagic2 @0x1404115d0 = ");
        for (byte b : m2) out.append(String.format("%02x ", b & 0xff));
        out.append("\n");

        Function f = fman.getFunctionContaining(space.getAddress(0x140386620L));
        out.append("\n===== phydm dispatcher @ 0x140386620 " + (f != null ? f.getName() : "") + " =====\n");
        if (f != null) {
            DecompileResults res = decomp.decompileFunction(f, 240, getMonitor());
            if (res != null && res.decompileCompleted()) out.append(res.getDecompiledFunction().getC());
            else out.append("(fail: " + (res != null ? res.getErrorMessage() : "null") + ")\n");
        }
        decomp.dispose();

        String report = out.toString();
        try (FileWriter w = new FileWriter(outPath)) { w.write(report); }
        println("WROTE " + outPath + " (" + report.length() + " chars)");
    }
}
