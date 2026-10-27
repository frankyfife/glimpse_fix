// Headless: -postScript DecompileRange.java <out.c> <start> <end>  -- decompiles every function whose entry lies in [start,end)
import java.io.PrintWriter;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;

public class DecompileRange extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        Address lo = toAddr(args[1]), hi = toAddr(args[2]);
        DecompInterface ifc = new DecompInterface();
        ifc.openProgram(currentProgram);
        try (PrintWriter w = new PrintWriter(args[0], "UTF-8")) {
            for (Function f : currentProgram.getFunctionManager().getFunctions(lo, true)) {
                if (f.getEntryPoint().compareTo(hi) >= 0) break;
                DecompileResults r = ifc.decompileFunction(f, 90, monitor);
                w.printf("// ---- %s @ %s ----%n", f.getName(true), f.getEntryPoint());
                w.println(r.decompileCompleted() ? r.getDecompiledFunction().getC() : "// failed: " + r.getErrorMessage());
            }
        }
        ifc.dispose();
    }
}
