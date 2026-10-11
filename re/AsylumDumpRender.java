import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.program.model.listing.Function;
import java.nio.file.*;
import java.io.PrintWriter;
public class AsylumDumpRender extends GhidraScript {
 public void run() throws Exception {
  if(!"4dac1f5e2ac6710b7378fdce74601f616f4753e3756cb5fda63c7519cc2eb028".equalsIgnoreCase(currentProgram.getExecutableSHA256())) throw new IllegalStateException("Wrong Asylum build");
  Path out=Path.of(getScriptArgs()[0]);Files.createDirectories(out.toAbsolutePath().getParent());
  DecompInterface decomp=new DecompInterface();decomp.openProgram(currentProgram);
  try(PrintWriter w=new PrintWriter(Files.newBufferedWriter(out))) {
   w.println("STATIC/Ghidra, Asylum exact hash, focused candidates; no live caller or ABI proof");
   for(long va:new long[]{0xc96bb0L,0xc96cd0L,0xcb2bb0L,0xcc2310L,0xcc2ab0L,0xccae50L}) {
    disassemble(toAddr(va));Function f=getFunctionAt(toAddr(va));
    if(f==null)f=createFunction(toAddr(va),"AA_CANDIDATE_"+Long.toHexString(va));
    w.println("\n=== "+Long.toHexString(va)+" ===");
    if(f==null){w.println("Could not define candidate");continue;}
    var result=decomp.decompileFunction(f,25,monitor);
    w.println(result.decompileCompleted()?result.getDecompiledFunction().getC():result.getErrorMessage());
   }
  }finally{decomp.dispose();}
 }
}
