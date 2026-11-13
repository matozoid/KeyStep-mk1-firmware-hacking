// For every volatile (peripheral) memory block, list functions that reference it,
// with the register offsets touched. Includes references from literal pools.
// @category KeyStep
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.symbol.Reference;
import java.util.*;

public class PeriphUsage extends GhidraScript {
    @Override public void run() throws Exception {
        for (MemoryBlock b : currentProgram.getMemory().getBlocks()) {
            if (!b.isVolatile()) continue;
            Map<String, TreeSet<String>> byFunc = new TreeMap<>();
            var it = currentProgram.getReferenceManager().getReferenceDestinationIterator(
                    new ghidra.program.model.address.AddressSet(b.getStart(), b.getEnd()), true);
            while (it.hasNext()) {
                var dst = it.next();
                for (Reference r : getReferencesTo(dst)) {
                    Function f = getFunctionContaining(r.getFromAddress());
                    String fn = f == null ? ("?" + r.getFromAddress()) : f.getName() + "@" + f.getEntryPoint();
                    byFunc.computeIfAbsent(fn, k -> new TreeSet<>())
                          .add("+0x" + Long.toHexString(dst.subtract(b.getStart())));
                }
            }
            if (byFunc.isEmpty()) continue;
            println("== " + b.getName() + " @ " + b.getStart());
            byFunc.forEach((k, v) -> println("   " + k + "  " + v));
        }
    }
}
