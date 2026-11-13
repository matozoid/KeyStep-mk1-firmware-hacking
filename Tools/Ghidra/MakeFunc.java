// Creates functions at the given addresses (modifies the DB).
// @category KeyStep
import ghidra.app.cmd.disassemble.ArmDisassembleCommand;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;

public class MakeFunc extends GhidraScript {
    @Override public void run() throws Exception {
        for (String a : getScriptArgs()) {
            Address at = toAddr(Long.decode(a) & ~1L);
            if (getFunctionAt(at) == null) {
                new ArmDisassembleCommand(at, null, true).applyTo(currentProgram, monitor);
                Function f = createFunction(at, null);
                println(a + (f == null ? " FAILED" : " -> " + f.getName()));
            }
        }
    }
}
