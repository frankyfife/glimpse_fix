// Like FuturamaPC's Decompile.java, but creates a function at an address that has none.
// Headless: -postScript DecompileCreate.java <out.c> <addr> [<addr> ...]
import java.io.PrintWriter;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.app.cmd.function.CreateFunctionCmd;
import ghidra.app.cmd.disassemble.DisassembleCommand;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;

public class DecompileCreate extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        DecompInterface ifc = new DecompInterface();
        ifc.openProgram(currentProgram);
        try (PrintWriter w = new PrintWriter(args[0], "UTF-8")) {
            for (int i = 1; i < args.length; i++) {
                Address a = toAddr(args[i]);
                Function f = getFunctionAt(a);
                if (f == null) {
                    new DisassembleCommand(a, null, true).applyTo(currentProgram, monitor);
                    new CreateFunctionCmd(a).applyTo(currentProgram, monitor);
                    f = getFunctionAt(a);
                }
                if (f == null) { w.printf("// %s: could not create a function%n%n", args[i]); continue; }
                DecompileResults r = ifc.decompileFunction(f, 90, monitor);
                w.printf("// ---- %s @ %s ----%n", f.getName(true), f.getEntryPoint());
                w.println(r.decompileCompleted() ? r.getDecompiledFunction().getC() : "// failed: " + r.getErrorMessage());
            }
        }
        ifc.dispose();
    }
}
