import ghidra.app.decompiler.DecompiledFunction;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Program;

public class DumpFns extends GhidraScript {
    @Override
    public void run() throws Exception {
        Program prog = currentProgram;
        DecompInterface decomp = new DecompInterface();
        decomp.openProgram(prog);
        String[] addrs = {
            "1403a4770", // lamode handler (parses args, calls LA setup)
        };
        for (String a : addrs) {
            Address addr = prog.getAddressFactory().getDefaultAddressSpace().getAddress(Long.parseUnsignedLong(a, 16));
            Function fn = prog.getFunctionManager().getFunctionContaining(addr);
            if (fn == null) fn = prog.getFunctionManager().getFunctionAt(addr);
            println("====================================================");
            println("### " + (fn != null ? fn.getName() : a) + " @ 0x" + Long.toHexString(addr.getOffset()));
            if (fn == null) { println("(no function)"); continue; }
            DecompileResults res = decomp.decompileFunction(fn, 500, monitor);
            if (res.decompileCompleted()) {
                println(res.getDecompiledFunction().getC());
            } else {
                println("(decompile failed: " + res.getErrorMessage() + ")");
            }
        }
        decomp.dispose();
    }
}
