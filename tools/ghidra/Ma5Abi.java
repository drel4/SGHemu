// Ghidra analysis helper, not emulator runtime code.
// @category SGHemu
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import java.io.*;
public class Ma5Abi extends GhidraScript {
    public void run() throws Exception {
        try (PrintWriter out = new PrintWriter(getScriptArgs()[0])) {
            out.println("Program: " + currentProgram.getName());
            SymbolIterator syms = currentProgram.getSymbolTable().getAllSymbols(true);
            while (syms.hasNext()) {
                Symbol s = syms.next();
                if (!s.getName().startsWith("MaSound_")) continue;
                Function f = getFunctionAt(s.getAddress());
                out.println("EXPORT " + s.getName() + " " + s.getAddress());
                if (f == null) { out.println("NO FUNCTION"); continue; }
                if (f.isThunk()) f = f.getThunkedFunction(true);
                out.println("BODY " + f.getEntryPoint() + " " + f.getName());
                InstructionIterator ins = currentProgram.getListing().getInstructions(f.getBody(), true);
                while (ins.hasNext()) {
                    Instruction i = ins.next();
                    if (i.getMnemonicString().equalsIgnoreCase("RET")) out.println(i.getAddress() + " " + i);
                    if (i.getMnemonicString().equalsIgnoreCase("JMP") && i.getFlows().length == 1) {
                        Function target = getFunctionAt(i.getFlows()[0]);
                        if (target != null && !target.equals(f)) {
                            out.println("TAIL JUMP " + target.getEntryPoint());
                            InstructionIterator tail = currentProgram.getListing().getInstructions(target.getBody(), true);
                            while (tail.hasNext()) {
                                Instruction t = tail.next();
                                if (t.getMnemonicString().equalsIgnoreCase("RET")) out.println(t.getAddress() + " " + t);
                            }
                        }
                    }
                }
            }
            out.println("EXTERNAL SYMBOLS");
            SymbolIterator ext = currentProgram.getSymbolTable().getExternalSymbols();
            while (ext.hasNext()) out.println(ext.next().getName());
        }
    }
}
