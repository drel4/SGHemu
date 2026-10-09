// Reverse-engineering helper only; emulator runtime remains C17.
import ghidra.app.script.GhidraScript;
import ghidra.app.cmd.disassemble.DisassembleCommand;
import ghidra.app.decompiler.*;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.SourceType;
import java.io.*;
import java.math.BigInteger;
import java.util.*;
import java.util.regex.*;
public class C100Irom extends GhidraScript {
 public void run() throws Exception {
  String[] args=getScriptArgs();
  Set<String> wanted=new HashSet<>(Arrays.asList("mmi_VersionInit","mmi_BuildHWVersionScr","UHGetIromVersion","uhConvertI16toA","IromBistResults","GenerateCRC","IrDATxDMA","SendUAresp","SendRR","SendRNR","SendData","SendDiscoveryResponse","DoCRCTest","IrFrameSearch","IrCreateFrame","gprsComputePPP16bitFCS","gprsPutHDLC","gprsCheckPPP16bitFCS","gprsCopyPPPRawCirc","gprsDecodePPPHDLC","memcpy"));
  Pattern p=Pattern.compile("^\\s+(\\S+)\\s+0x([0-9a-fA-F]+)\\s+Thumb Code\\s+(\\d+)\\s+.*$");
  List<Function> fs=new ArrayList<>();
  for(String line: java.nio.file.Files.readAllLines(java.nio.file.Path.of(args[0]))) {
   Matcher m=p.matcher(line); if(!m.matches()||!wanted.contains(m.group(1)))continue;
   long at=Long.parseLong(m.group(2),16);int len=Integer.parseInt(m.group(3));if(len==0)len=46;
   Address start=toAddr(at),end=toAddr(at+len-1);if(!currentProgram.getMemory().contains(start))continue;
   clearListing(start,end);
   currentProgram.getProgramContext().setValue(currentProgram.getRegister("TMode"),start,end,BigInteger.ONE);
   new DisassembleCommand(start,new AddressSet(start,end),true).applyTo(currentProgram,monitor);
   Function f=getFunctionAt(start);if(f==null)f=createFunction(start,m.group(1));
   if(f!=null){f.setName(m.group(1),SourceType.USER_DEFINED);fs.add(f);}
  }
  DecompInterface d=new DecompInterface();d.openProgram(currentProgram);
  try(PrintWriter out=new PrintWriter(args[1])) {
   for(Function f:fs){out.println("/* "+f.getEntryPoint()+" "+f.getName()+" */");DecompileResults r=d.decompileFunction(f,30,monitor);out.println(r.decompileCompleted()?r.getDecompiledFunction().getC():r.getErrorMessage());}
  }finally{d.dispose();}
 }
}
