// Decompile functions whose (namespace-qualified) names match any given regex; write one .c file each plus an index.
// Args: <outDir> <patternsFile> (one regex per line). Output is Ghidra pseudocode, NOT original source.
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.listing.*;
import java.io.*;
import java.util.regex.*;
public class DecompileSelected extends GhidraScript {
  public void run() throws Exception {
    String[] a = getScriptArgs();
    File out = new File(a[0]); out.mkdirs();
    java.util.List<String> lines = java.nio.file.Files.readAllLines(new File(a[1]).toPath());
    java.util.List<Pattern> pl = new java.util.ArrayList<>();
    for (String l : lines) if (!l.trim().isEmpty() && !l.startsWith("#")) pl.add(Pattern.compile(l.trim()));
    Pattern[] ps = pl.toArray(new Pattern[0]);
    DecompInterface di = new DecompInterface(); di.openProgram(currentProgram);
    try (PrintWriter idx = new PrintWriter(new FileWriter(new File(out,"index.tsv"),true))) {
      for (Function f : currentProgram.getFunctionManager().getFunctions(true)) {
        String n = f.getName(true); boolean hit=false;
        for (Pattern p: ps) if (p.matcher(n).find()) { hit=true; break; }
        if (!hit) continue;
        DecompileResults r = di.decompileFunction(f, 120, monitor);
        String body = r.decompileCompleted() ? r.getDecompiledFunction().getC() : ("// decompile failed: "+r.getErrorMessage());
        String fn = currentProgram.getName()+"_"+f.getEntryPoint()+".c";
        try (PrintWriter w = new PrintWriter(new FileWriter(new File(out,fn)))) {
          w.println("// "+currentProgram.getName()+" "+f.getEntryPoint()+" "+n+"\n// Ghidra 12.1.4 pseudocode - not original source");
          w.println(body);
        }
        idx.println(currentProgram.getName()+"\t"+f.getEntryPoint()+"\t"+n+"\t"+fn);
      }
    }
    di.dispose();
  }
}
