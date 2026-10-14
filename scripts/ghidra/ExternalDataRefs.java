// Args: <out.txt> <name...>. For external data symbols, list referencing functions (through the GOT) and decompile the first two.
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import ghidra.program.model.address.*;
import java.io.*;
import java.util.*;
public class ExternalDataRefs extends GhidraScript {
  public void run() throws Exception {
    String[] a = getScriptArgs();
    DecompInterface di = new DecompInterface(); di.openProgram(currentProgram);
    FunctionManager fm = currentProgram.getFunctionManager();
    try (PrintWriter w = new PrintWriter(new FileWriter(a[0]))) {
      for (int k = 1; k < a.length; k++) {
        for (Symbol s : currentProgram.getSymbolTable().getSymbols(a[k])) {
          w.println("== " + a[k] + " @ " + s.getAddress() + " external=" + s.isExternal());
          Set<Function> fs = new LinkedHashSet<>();
          for (Reference r : getReferencesTo(s.getAddress())) {
            Address from = r.getFromAddress();
            Function f = fm.getFunctionContaining(from);
            if (f != null) fs.add(f);
            else for (Reference r2 : getReferencesTo(from)) { Function f2 = fm.getFunctionContaining(r2.getFromAddress()); if (f2 != null) fs.add(f2); }
          }
          w.println("functions: " + fs.size());
          int n = 0;
          for (Function f : fs) {
            w.println("  " + f.getEntryPoint() + " " + f.getName(true));
            if (n++ < 2) {
              DecompileResults r = di.decompileFunction(f, 60, monitor);
              if (r.decompileCompleted()) w.println(r.getDecompiledFunction().getC());
            }
          }
        }
      }
    }
  }
}
