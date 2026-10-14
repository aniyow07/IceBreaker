// Export function inventory (address, size, name, thumb flag) to a TSV. Read-only.
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import java.io.*;
public class ExportFunctions extends GhidraScript {
  public void run() throws Exception {
    String out = getScriptArgs()[0];
    try (PrintWriter w = new PrintWriter(new FileWriter(out))) {
      w.println("entry\tsize\tname\tcallers\tcallees");
      for (Function f : currentProgram.getFunctionManager().getFunctions(true)) {
        w.println(f.getEntryPoint()+"\t"+f.getBody().getNumAddresses()+"\t"+f.getName(true)+"\t"
          +f.getCallingFunctions(monitor).size()+"\t"+f.getCalledFunctions(monitor).size());
      }
    }
  }
}
