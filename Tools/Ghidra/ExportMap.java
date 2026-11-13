// @category KeyStep
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import java.io.PrintWriter;

public class ExportMap extends GhidraScript {
    @Override public void run() throws Exception {
        try (PrintWriter w = new PrintWriter(getScriptArgs()[0])) {
            for (Function f : currentProgram.getFunctionManager().getFunctions(true))
                w.printf("%s %6d %s%n", f.getEntryPoint(), f.getBody().getNumAddresses(), f.getName());
        }
    }
}
