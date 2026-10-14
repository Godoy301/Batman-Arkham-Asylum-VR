import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.program.model.listing.Function;
import java.nio.file.*;
import java.io.PrintWriter;
public class AsylumFocused extends GhidraScript {
 public void run() throws Exception {
  if(!"4dac1f5e2ac6710b7378fdce74601f616f4753e3756cb5fda63c7519cc2eb028".equalsIgnoreCase(currentProgram.getExecutableSHA256())) throw new IllegalStateException("Wrong Asylum build");
  String[] args=getScriptArgs(); Path out=Path.of(args[0]);
  Files.createDirectories(out.toAbsolutePath().getParent());
  DecompInterface d=new DecompInterface(); d.openProgram(currentProgram);
  try(PrintWriter w=new PrintWriter(Files.newBufferedWriter(out))) {
   w.println("STATIC ONLY: hash-guarded Asylum executable; candidate ABI requires instruction inspection.");
   for(int n=1;n<args.length;n++) {
    long va=Long.parseUnsignedLong(args[n].replace("0x",""),16);
    disassemble(toAddr(va)); Function f=getFunctionAt(toAddr(va));
    if(f==null)f=createFunction(toAddr(va),"AA_CANDIDATE_"+Long.toHexString(va));
    w.println("\n=== "+Long.toHexString(va)+" ==="); w.flush();
    if(f==null){w.println("No function");continue;}
    var r=d.decompileFunction(f,25,monitor);
    w.println(r.decompileCompleted()?r.getDecompiledFunction().getC():r.getErrorMessage());w.flush();
   }
  } finally {d.dispose();}
 }
}
