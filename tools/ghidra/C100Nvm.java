// @category SGHemu
// RE-only helper: import XEDK1 BIN1 at 01000000, ARM:LE:32:v4t.
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

public class C100Nvm extends GhidraScript {
    static class Entry {
        String name; long address; int size;
        Entry(String n, long a, int s) { name=n; address=a; size=s; }
    }
    public void run() throws Exception {
        String[] args=getScriptArgs();
        if(args.length<2) throw new IllegalArgumentException("listing path, output path, optional function names");
        TreeMap<Long,Entry> entries=new TreeMap<>();
        Pattern p=Pattern.compile("^\\s+(\\S+)\\s+0x([0-9a-fA-F]+)\\s+Thumb Code\\s+(\\d+)\\s+.*$");
        for(String line: java.nio.file.Files.readAllLines(java.nio.file.Path.of(args[0]))) {
            Matcher m=p.matcher(line); if(!m.matches())continue;
            int size=Integer.parseInt(m.group(3)); if(size==0)continue;
            long at=Long.parseLong(m.group(2),16);
            if(currentProgram.getMemory().contains(toAddr(at)))entries.put(at,new Entry(m.group(1),at,size));
        }
        Set<String> wanted=new HashSet<>(Arrays.asList("flash_Initialize","iStore_NLT","iRead_NLT",
            "EraseAndSetupASector","FactoryEraseAndSetupASector","FactoryEraseAndSetupROSector",
            "FactoryInitEraseCounters","UpdateSectorEraseCounters","InitializeFlashAPIData",
            "ParallelInitFlashAPIData","FactoryTestMain","check_for_factory_loopback",
            "write_Flash","validate_data_packet","ft_flapi_init_callback"));
        int callerDepth=2;
        if(args.length>2) {
            wanted=new HashSet<>();
            for(int i=2;i<args.length;++i) {
                if(args[i].startsWith("depth=") || args[i].startsWith("depth:"))callerDepth=Integer.parseInt(args[i].substring(6));
                else if(args[i].equals("depth") && i+1<args.length)callerDepth=Integer.parseInt(args[++i]);
                else wanted.addAll(Arrays.asList(args[i].split(",")));
            }
        }
        Set<Long> selected=new TreeSet<>();
        for(Entry e:entries.values())if(wanted.contains(e.name))selected.add(e.address);
        List<String> calls=new ArrayList<>();
        // Two rounds of direct Thumb BL callers; raw hits require disassembly confirmation.
        for(int depth=0;depth<callerDepth;++depth) {
            Set<Long> parents=new TreeSet<>();
            for(Entry e:entries.values())for(long at=e.address;at+3<e.address+e.size;at+=2) {
                int h1=getShort(toAddr(at))&65535,h2=getShort(toAddr(at+2))&65535;
                if((h1&0xf800)!=0xf000||(h2&0xf800)!=0xf800)continue;
                int delta=((h1&0x7ff)<<12)|((h2&0x7ff)<<1);
                if((delta&0x400000)!=0)delta|=0xff800000;
                long target=at+4+delta;
                if(selected.contains(target)) {
                    parents.add(e.address);
                    String line=String.format("%08x %s -> %08x %s",at,e.name,target,entries.get(target).name);
                    if(!calls.contains(line))calls.add(line);
                }
            }
            selected.addAll(parents);
        }
        List<Function> fs=new ArrayList<>();
        for(long at:selected) {
            Entry e=entries.get(at); Address start=toAddr(at),end=toAddr(at+e.size-1);
            clearListing(start,end);
            currentProgram.getProgramContext().setValue(currentProgram.getRegister("TMode"),start,end,BigInteger.ONE);
            new DisassembleCommand(start,new AddressSet(start,end),true).applyTo(currentProgram,monitor);
            Function f=getFunctionAt(start);if(f==null)f=createFunction(start,e.name);
            if(f!=null){f.setName(e.name,SourceType.USER_DEFINED);fs.add(f);}
        }
        DecompInterface d=new DecompInterface();d.openProgram(currentProgram);
        try(PrintWriter out=new PrintWriter(args[1])) {
            out.println("/* Direct Thumb BL candidates; inspect instructions before trusting raw matches.");
            for(String line:calls) {
                Address at=toAddr(Long.parseLong(line.substring(0,8),16));
                if(currentProgram.getListing().getInstructionAt(at)==null)
                    new DisassembleCommand(at,new AddressSet(at,at.add(3)),false).applyTo(currentProgram,monitor);
                out.println(line+" ["+currentProgram.getListing().getInstructionAt(at)+"]");
            }
            out.println("*/");
            for(Function f:fs) {
                out.println("/* "+f.getEntryPoint()+" "+f.getName()+" */");
                DecompileResults r=d.decompileFunction(f,30,monitor);
                out.println(r.decompileCompleted()?r.getDecompiledFunction().getC():r.getErrorMessage());
                Entry e=entries.get(f.getEntryPoint().getOffset());
                InstructionIterator it=currentProgram.getListing().getInstructions(
                    new AddressSet(toAddr(e.address),toAddr(e.address+e.size-1)),true);
                out.println("/* Instructions:");while(it.hasNext()){Instruction i=it.next();out.println(i.getAddress()+" "+i);}out.println("*/");
            }
        }finally{d.dispose();}
    }
}
