// @category KeyStep
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.Reference;

public class XRefs extends GhidraScript {
    @Override public void run() throws Exception {
        for (String a : getScriptArgs()) {
            println("== refs to " + a);
            for (Reference r : getReferencesTo(toAddr(Long.decode(a)))) {
                Function f = getFunctionContaining(r.getFromAddress());
                println("  " + r.getFromAddress() + " " + r.getReferenceType()
                        + " in " + (f == null ? "?" : f.getName()));
            }
        }
    }
}
