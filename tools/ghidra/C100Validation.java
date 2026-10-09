// @category SGHemu
import ghidra.app.script.GhidraScript;
import ghidra.app.cmd.disassemble.DisassembleCommand;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import java.io.*;
import java.math.BigInteger;
public class C100Validation extends GhidraScript {
    private void range(PrintWriter out, long begin, long end) throws Exception {
        Address a = toAddr(begin), b = toAddr(end - 1);
        clearListing(a, b);
        currentProgram.getProgramContext().setValue(currentProgram.getRegister("TMode"), a, b, BigInteger.ONE);
        for (long at = begin; at < end; at += 2) {
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
            range(out, 0x01122ceaL, 0x01122d02L);
            range(out, 0x01122d9eL, 0x01122eeeL);
            range(out, 0x0115c740L, 0x0115c82eL);
            out.println("IDENTITY CLASS LISTS AND DRIVER TABLE POINTERS");
            for (int table = 0; table < 4; ++table) {
                long at = 0x013d3cb0L + table * 0x5c;
                int list = getInt(toAddr(at));
                out.printf("Table %d @%08x class list @%08x:", table, at, list);
                for (int i = 0; i < 20; ++i) { int value = getByte(toAddr((list & 0xffffffffL) + i)) & 255; out.printf(" %d", value); if (value == 0) break; }
                out.println();
                for (int i = 1; i < 23; ++i) out.printf("  +%02x %08x%n", i * 4, getInt(toAddr(at + i * 4)));
            }
        }
    }
}
