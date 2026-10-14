// Args: <out.tsv> <patternsFile>. For each function matching a regex, list its callers (functions referencing its entry).
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import java.io.*;
import java.util.*;
import java.util.regex.*;
public class CallersOf extends GhidraScript {
  public void run() throws Exception {
    String[] a = getScriptArgs();
    List<Pattern> ps = new ArrayList<>();
    for (String l : java.nio.file.Files.readAllLines(new File(a[1]).toPath())) if (!l.isBlank() && !l.startsWith("#")) ps.add(Pattern.compile(l.trim()));
    FunctionManager fm = currentProgram.getFunctionManager();
    try (PrintWriter w = new PrintWriter(new FileWriter(a[0]))) {
      for (Function f : fm.getFunctions(true)) {
        String n = f.getName(true); boolean hit=false;
        for (Pattern p : ps) if (p.matcher(n).find()) { hit=true; break; }
        if (!hit) continue;
        Set<String> callers = new TreeSet<>();
        for (Reference r : getReferencesTo(f.getEntryPoint())) {
          Function c = fm.getFunctionContaining(r.getFromAddress());
          callers.add(c != null ? c.getName(true) : ("data@"+r.getFromAddress()));
        }
        w.println(f.getEntryPoint()+"\t"+n+"\t"+callers.size()+"\t"+String.join(" | ", callers));
      }
    }
  }
}
