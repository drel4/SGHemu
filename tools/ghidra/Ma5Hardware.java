// @category SGHemu
// Local reverse-engineering helper; never packages Yamaha binaries.
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import java.io.*;

public class Ma5Hardware extends GhidraScript {
    public void run() throws Exception {
        DecompInterface decompiler = new DecompInterface();
        decompiler.openProgram(currentProgram);
        try (PrintWriter out = new PrintWriter(getScriptArgs()[0])) {
            out.println("Program: " + currentProgram.getName());
            SymbolIterator symbols = currentProgram.getSymbolTable().getAllSymbols(true);
            while (symbols.hasNext()) {
                Symbol symbol = symbols.next();
                if (!symbol.getName().startsWith("Hw_")) continue;
                Function function = getFunctionAt(symbol.getAddress());
                out.println("EXPORT " + symbol.getName() + " " + symbol.getAddress());
                if (function == null) continue;
                DecompileResults result = decompiler.decompileFunction(function, 60, monitor);
                if (result.decompileCompleted()) out.println(result.getDecompiledFunction().getC());
                InstructionIterator instructions = currentProgram.getListing().getInstructions(function.getBody(), true);
                while (instructions.hasNext()) {
                    Instruction instruction = instructions.next();
                    if (instruction.getMnemonicString().equalsIgnoreCase("RET"))
                        out.println(instruction.getAddress() + " " + instruction);
                }
            }
            for (int i = 1; i < getScriptArgs().length; ++i) {
                var address = toAddr(Long.parseUnsignedLong(getScriptArgs()[i], 16));
                Function function = getFunctionAt(address);
                if (function == null) {
                    disassemble(address);
                    function = createFunction(address, null);
                }
                if (function == null) continue;
                out.println("INTERNAL " + function.getName() + " " + function.getEntryPoint());
                DecompileResults result = decompiler.decompileFunction(function, 60, monitor);
                if (result.decompileCompleted()) out.println(result.getDecompiledFunction().getC());
            }
            out.println("EXTERNAL SYMBOLS");
            SymbolIterator externals = currentProgram.getSymbolTable().getExternalSymbols();
            while (externals.hasNext()) out.println(externals.next().getName());
        } finally { decompiler.dispose(); }
    }
}
