// @category SGHemu
import ghidra.app.script.GhidraScript;
import ghidra.app.cmd.disassemble.DisassembleCommand;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import java.io.*;
import java.math.BigInteger;
public class C100Io extends GhidraScript {
    public void run() throws Exception {
        Address begin = toAddr(0x01217cf0L), end = toAddr(0x01218364L);
        clearListing(begin, end);
        currentProgram.getProgramContext().setValue(currentProgram.getRegister("TMode"), begin, end, BigInteger.ONE);
        for (long at = 0x01217cf0L; at <= 0x01218364L; at += 2) {
            Address next = toAddr(at);
            if (currentProgram.getListing().getInstructionContaining(next) == null)
                new DisassembleCommand(next, new AddressSet(begin, end), false).applyTo(currentProgram, monitor);
        }
        try (PrintWriter out = new PrintWriter(getScriptArgs()[0])) {
            out.println(currentProgram.getName() + " " + currentProgram.getLanguageID());
            out.printf("Register-bank literal @01217dcc = %08x%n", getInt(toAddr(0x01217dccL)));
            out.printf("Split-bank mask literal @01217dd0 = %08x%n", getInt(toAddr(0x01217dd0L)));
            InstructionIterator it = currentProgram.getListing().getInstructions(new AddressSet(begin, end), true);
            while (it.hasNext()) { Instruction i = it.next(); out.println(i.getAddress() + " " + i); }
        }
    }
}
