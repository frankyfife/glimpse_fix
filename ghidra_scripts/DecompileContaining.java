// Headless: -postScript DecompileContaining.java <out.c> <addr> [<addr> ...]  -- decompiles the function containing each address
import java.io.PrintWriter;
import java.util.HashSet;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;

public class DecompileContaining extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        DecompInterface ifc = new DecompInterface();
        ifc.openProgram(currentProgram);
        HashSet<Function> done = new HashSet<>();
        try (PrintWriter w = new PrintWriter(args[0], "UTF-8")) {
            for (int i = 1; i < args.length; i++) {
                Function f = getFunctionContaining(toAddr(args[i]));
                if (f == null) { w.printf("// %s: no function%n%n", args[i]); continue; }
                if (!done.add(f)) continue;
                DecompileResults r = ifc.decompileFunction(f, 120, monitor);
                w.printf("// ---- %s @ %s (contains %s) ----%n", f.getName(true), f.getEntryPoint(), args[i]);
                w.println(r.decompileCompleted() ? r.getDecompiledFunction().getC() : "// failed: " + r.getErrorMessage());
            }
        }
        ifc.dispose();
    }
}
