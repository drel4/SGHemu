// Ghidra analysis helper, not runtime code. Raw binary imported at 0x01000000.
// @category SGHemu
import ghidra.app.script.GhidraScript;
import ghidra.app.cmd.disassemble.DisassembleCommand;
import ghidra.program.model.address.*;
import ghidra.program.model.lang.Register;
import ghidra.program.model.listing.*;
import java.io.*;
import java.math.BigInteger;
public class C100Boot extends GhidraScript {
    private void range(PrintWriter out, long begin, long end, int thumb) throws Exception {
        Address a = toAddr(begin), b = toAddr(end - 1);
        Register mode = currentProgram.getRegister("TMode");
        clearListing(a, b);
        currentProgram.getProgramContext().setValue(mode, a, b, BigInteger.valueOf(thumb));
        for (long at = begin; at < end; at += thumb == 1 ? 2 : 4) {
            Address next = toAddr(at);
            if (currentProgram.getListing().getInstructionContaining(next) == null)
                new DisassembleCommand(next, new AddressSet(a, b), false).applyTo(currentProgram, monitor);
        }
        InstructionIterator it = currentProgram.getListing().getInstructions(new AddressSet(a, b), true);
        while (it.hasNext()) { Instruction i = it.next(); out.println(i.getAddress() + " " + i); }
    }
    public void run() throws Exception {
        try (PrintWriter out = new PrintWriter(getScriptArgs()[0])) {
            out.println(currentProgram.getName() + " " + currentProgram.getLanguageID());
            range(out, 0x01000250L, 0x01000418L, 0);
            range(out, 0x01000418L, 0x010004c4L, 1);
            range(out, 0x01000750L, 0x01000754L, 1);
            range(out, 0x01000754L, 0x01000760L, 0);
            out.println("BOOT LITERAL POOL");
            for (long at = 0x01000580L; at < 0x010006a4L; at += 4)
                out.printf("%08x %08x%n", at, getInt(toAddr(at)));
        }
    }
}
