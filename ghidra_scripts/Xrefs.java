// Headless: -postScript Xrefs.java <out.txt> <addr> [<addr> ...]  -- lists references to each address with containing function
import java.io.PrintWriter;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.Reference;

public class Xrefs extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        try (PrintWriter w = new PrintWriter(args[0], "UTF-8")) {
            for (int i = 1; i < args.length; i++) {
                Address a = toAddr(args[i]);
                w.printf("== %s%n", a);
                for (Reference r : getReferencesTo(a)) {
                    Function f = getFunctionContaining(r.getFromAddress());
                    w.printf("  %s %s in %s%n", r.getFromAddress(), r.getReferenceType(), f == null ? "?" : f.getName() + "@" + f.getEntryPoint());
                }
            }
        }
    }
}
