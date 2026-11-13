// Decompile functions containing the given addresses.
// Optional first arg "out=<path>" writes to a file instead of the log.
// @category KeyStep
import ghidra.app.decompiler.*;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import java.io.PrintWriter;
import java.util.*;

public class DecompileFunc extends GhidraScript {
    @Override public void run() throws Exception {
        List<String> args = new ArrayList<>(Arrays.asList(getScriptArgs()));
        PrintWriter w = null;
        if (!args.isEmpty() && args.get(0).startsWith("out=")) w = new PrintWriter(args.remove(0).substring(4));
        DecompInterface d = new DecompInterface();
        d.openProgram(currentProgram);
        try {
            for (String a : args) {
                Function f = getFunctionContaining(toAddr(Long.decode(a)));
                String s;
                if (f == null) s = "// no function at " + a;
                else {
                    DecompileResults r = d.decompileFunction(f, 60, monitor);
                    s = "// ==== " + f.getName() + " @ " + f.getEntryPoint() + " ====\n"
                        + (r.decompileCompleted() ? r.getDecompiledFunction().getC()
                                                  : "// failed: " + r.getErrorMessage());
                }
                if (w != null) w.println(s); else println(s);
            }
        } finally { d.dispose(); if (w != null) w.close(); }
    }
}
