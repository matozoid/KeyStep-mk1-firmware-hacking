// Applies names from a TSV: addr \t name [\t comment]  (modifies the DB).
// @category KeyStep
import ghidra.app.script.GhidraScript;
import ghidra.program.model.symbol.SourceType;
import java.nio.file.*;

public class ApplySymbols extends GhidraScript {
    @Override public void run() throws Exception {
        for (String line : Files.readAllLines(Paths.get(getScriptArgs()[0]))) {
            if (line.isBlank() || line.startsWith("#")) continue;
            String[] c = line.split("\t");
            var at = toAddr(Long.decode(c[0]));
            var f = getFunctionAt(at);
            if (f != null) f.setName(c[1], SourceType.USER_DEFINED);
            else createLabel(at, c[1], true, SourceType.USER_DEFINED);
            if (c.length > 2) setPlateComment(at, c[2]);
        }
    }
}
