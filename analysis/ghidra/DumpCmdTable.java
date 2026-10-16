// Dump the command dispatch jump table at 0x1404dc650.
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.mem.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import java.util.*;
import java.io.*;

public class DumpCmdTable extends GhidraScript {

    @Override
    public void run() throws Exception {
        String outPath = "/D:/workspace/perdolinque/ac54_sdr/docs/cmd_table.txt";
        AddressSpace space = currentProgram.getAddressFactory().getDefaultAddressSpace();
        Memory memory = currentProgram.getMemory();
        FunctionManager fman = currentProgram.getFunctionManager();
        SymbolTable sym = currentProgram.getSymbolTable();

        long base = 0x1404dc650L;
        int nentries = 0x40; // dump 64 candidate 8-byte slots

        StringBuilder out = new StringBuilder();
        out.append("# Command dispatch jump table @ 0x1404dc650\n");
        out.append("# dispatcher: FUN_14017e690 ; valid cmd index < 0x23 (35)\n");

        byte[] buf = new byte[8 * nentries];
        memory.getBytes(space.getAddress(base), buf);

        for (int i = 0; i < nentries; i++) {
            long val = 0;
            for (int b = 0; b < 8; b++) {
                val |= ((long) (buf[i * 8 + b] & 0xff)) << (8 * b);
            }
            String tag = "";
            if (val >= 0x140000000L && val < 0x140a00000L) {
                tag = "  <-- CODE";
                Function f = fman.getFunctionContaining(space.getAddress(val));
                if (f != null) {
                    tag += "  " + f.getName() + "@" + f.getEntryPoint();
                } else {
                    // try symbol at this address
                    Symbol[] syms = sym.getSymbols(space.getAddress(val));
                    for (Symbol s : syms) {
                        if (s.isPrimary()) { tag += "  sym=" + s.getName(); break; }
                    }
                }
            }
            out.append(String.format("  [%2d] +0x%03x  0x%012x%s%n", i, i * 8, val, tag));
        }

        String report = out.toString();
        try (FileWriter w = new FileWriter(outPath)) {
            w.write(report);
        }
        println("WROTE " + outPath);
    }
}
