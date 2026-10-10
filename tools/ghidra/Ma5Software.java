// @category SGHemu
// Local RE helper for software DLL hardware-dispatch globals and their callers.
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import java.io.*;
import java.util.*;

public class Ma5Software extends GhidraScript {
    public void run() throws Exception {
        String[] args=getScriptArgs();
        Set<Function> functions=new TreeSet<>(Comparator.comparing(f -> f.getEntryPoint()));
        for(int i=1;i<args.length;++i)
            for(Reference ref:getReferencesTo(toAddr(Long.parseUnsignedLong(args[i],16)))) {
                Function f=getFunctionContaining(ref.getFromAddress());
                if(f!=null)functions.add(f);
            }
        DecompInterface decompiler=new DecompInterface();decompiler.openProgram(currentProgram);
        try(PrintWriter out=new PrintWriter(args[0])) {
            out.println("Program: "+currentProgram.getName());
            for(Function f:functions) {
                out.println("FUNCTION "+f.getName()+" "+f.getEntryPoint());
                for(Reference ref:getReferencesTo(f.getEntryPoint())) {
                    Function caller=getFunctionContaining(ref.getFromAddress());
                    if(caller!=null)out.println("CALLER "+caller.getName()+" "+caller.getEntryPoint()+" at "+ref.getFromAddress());
                }
                DecompileResults result=decompiler.decompileFunction(f,60,monitor);
                if(result.decompileCompleted())out.println(result.getDecompiledFunction().getC());
            }
        } finally {decompiler.dispose();}
    }
}
