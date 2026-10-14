// For every imported (external/thunk) function, list the functions that reference it. Read-only TSV.
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import java.io.*;
import java.util.*;
public class ImportCallers extends GhidraScript {
  public void run() throws Exception {
    try (PrintWriter w = new PrintWriter(new FileWriter(getScriptArgs()[0]))) {
      FunctionManager fm = currentProgram.getFunctionManager();
      for (Function f : fm.getFunctions(true)) {
        if (!(f.isThunk() || f.isExternal())) continue;
        Function t = f.isThunk() ? f.getThunkedFunction(true) : f;
        if (t == null || !t.isExternal()) continue;
        Set<String> callers = new TreeSet<>();
        for (Reference r : getReferencesTo(f.getEntryPoint())) {
          Function c = fm.getFunctionContaining(r.getFromAddress());
          if (c != null && c != f) callers.add(c.getName(true));
        }
        w.println(t.getName()+"\t"+callers.size()+"\t"+String.join(" | ", callers));
      }
    }
  }
}
