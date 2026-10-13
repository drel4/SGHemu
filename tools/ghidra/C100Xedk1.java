// C100XEDK1 full-image setup for headless Ghidra.
//
// The XEDK1 firmware is delivered as two raw images (LOAD at 0x01000000 and
// RDATA at 0x02000000) plus the armarm linker listing C100XEDK1.lst.  The
// first image is imported normally; this script attaches the second image as
// a memory block, then applies the listing's ARM/Thumb mapping symbols and the
// local/global symbol table before full auto-analysis runs.
//
// Usage (as a -preScript):
//   C100Xedk1.java <C100XEDK1_2.bin> <C100XEDK1.lst>
//
// @category SGHemu
import ghidra.app.script.GhidraScript;
import ghidra.app.cmd.disassemble.DisassembleCommand;
import ghidra.program.model.address.*;
import ghidra.program.model.lang.Register;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.*;
import ghidra.program.model.symbol.SourceType;
import java.io.*;
import java.math.BigInteger;
import java.nio.file.*;
import java.util.*;
import java.util.regex.*;

public class C100Xedk1 extends GhidraScript {
    private static final long RDATA_BASE = 0x02000000L;

    private static final Pattern MAP_PAT =
        Pattern.compile("^\\s+(\\$\\S)\\s+0x([0-9a-fA-F]+)\\s+(\\S+)\\s*$");
    private static final Pattern SYM_PAT =
        Pattern.compile("^\\s{4}(\\S+)\\s+0x([0-9a-fA-F]+)\\s+(ARM Code|Thumb Code|Data)\\s+(\\d+)\\s+(\\S.*?)\\s*$");

    private static final class MapEntry {
        final long address;
        final boolean thumb;
        final boolean code;
        MapEntry(long address, boolean thumb, boolean code) {
            this.address = address;
            this.thumb = thumb;
            this.code = code;
        }
    }

    private static final class Sym {
        final String name;
        final long address;
        final int size;
        final boolean thumb;
        final boolean code;
        Sym(String name, long address, int size, boolean thumb, boolean code) {
            this.name = name;
            this.address = address;
            this.size = size;
            this.thumb = thumb;
            this.code = code;
        }
    }

    private Register tmode;

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 2) {
            throw new IllegalArgumentException("usage: C100Xedk1.java <RDATA bin> <listing>");
        }
        tmode = currentProgram.getRegister("TMode");
        if (tmode == null) {
            throw new IllegalStateException("program has no TMode register (not an ARM language?)");
        }

        attachImage(args[0], RDATA_BASE, "RDATA");

        List<String> lines = Files.readAllLines(Path.of(args[1]));
        List<MapEntry> maps = new ArrayList<>();
        List<Sym> syms = new ArrayList<>();
        for (String line : lines) {
            Matcher map = MAP_PAT.matcher(line);
            if (map.matches()) {
                String sym = map.group(1);
                long address = Long.parseUnsignedLong(map.group(2), 16);
                maps.add(new MapEntry(address, sym.equals("$t"),
                    sym.equals("$t") || sym.equals("$a")));
                continue;
            }
            Matcher sym = SYM_PAT.matcher(line);
            if (sym.matches()) {
                String type = sym.group(3);
                syms.add(new Sym(sym.group(1),
                    Long.parseUnsignedLong(sym.group(2), 16),
                    Integer.parseInt(sym.group(4)),
                    type.equals("Thumb Code"), !type.equals("Data")));
            }
        }
        maps.sort(Comparator.comparingLong(entry -> entry.address));
        syms.sort(Comparator.comparingLong(entry -> entry.address));
        println("listing: " + maps.size() + " mapping symbols, " + syms.size() + " named symbols");

        // ARM/Thumb context from the linker mapping symbols.
        for (int i = 0; i < maps.size(); ++i) {
            MapEntry entry = maps.get(i);
            if (!entry.code) {
                continue;
            }
            long end = i + 1 < maps.size() ? maps.get(i + 1).address - 1 : entry.address;
            setMode(entry.address, end, entry.thumb);
        }

        // Named code symbols become functions; named data symbols become labels.
        long namedFunctions = 0;
        long namedData = 0;
        long skipped = 0;
        for (Sym sym : syms) {
            Address start = toAddr(sym.address);
            if (!currentProgram.getMemory().contains(start)) {
                continue;
            }
            if (sym.code) {
                if (sym.size <= 0) {
                    continue; // section/file symbols without an extent
                }
                Address end = toAddr(sym.address + sym.size - 1L);
                // Do not clearListing here for the common case: a handful of listing
                // symbols are nested inside another (e.g. B_Main inside !!!).  The
                // TMode context plus a constrained disassembly is enough to seed the
                // function; the full auto-analysis pass refines the body afterwards.
                try {
                    applyMode(start, end, sym.thumb);
                    new DisassembleCommand(start, new AddressSet(start, end), true)
                        .applyTo(currentProgram, monitor);
                    Function function = getFunctionAt(start);
                    if (function == null) {
                        function = createFunction(start, sym.name);
                    }
                    if (function != null && !function.getName().equals(sym.name)) {
                        try {
                            function.setName(sym.name, SourceType.USER_DEFINED);
                        }
                        catch (Exception duplicate) {
                            // A different alias already owns this entry point.
                        }
                    }
                    ++namedFunctions;
                }
                catch (Exception failed) {
                    // Keep going: one unreconcilable listing symbol must not abort the
                    // whole import, and auto-analysis will still recover the region.
                    ++skipped;
                    if (skipped <= 20) {
                        println("skipped " + start + " " + sym.name + ": " + failed);
                    }
                }
                if (namedFunctions % 2000 == 0) {
                    println("functions: " + namedFunctions);
                }
            }
            else {
                if (getSymbolAt(start) == null) {
                    try {
                        createLabel(start, sym.name, false, SourceType.IMPORTED);
                        ++namedData;
                    }
                    catch (Exception bad) {
                        // Invalid or duplicate label name; leave the address unnamed.
                    }
                }
            }
        }
        println("applied " + namedFunctions + " code functions and " + namedData
            + " data labels (" + skipped + " skipped)");

        if (currentProgram.getMemory().contains(toAddr(0x01000250L))) {
            try {
                createLabel(toAddr(0x01000250L), "IMAGE_ENTRY_POINT", false, SourceType.IMPORTED);
            }
            catch (Exception ignored) {
                // Already labelled by the listing.
            }
        }
    }

    /** Attaches a raw image as a read-only initialized block when it is absent. */
    private void attachImage(String path, long base, String name) throws Exception {
        Address start = toAddr(base);
        if (currentProgram.getMemory().getBlock(start) != null) {
            println("block already present at " + start + ", skipping " + path);
            return;
        }
        File file = new File(path);
        if (!file.isFile()) {
            throw new FileNotFoundException(path);
        }
        try (InputStream in = new BufferedInputStream(new FileInputStream(file))) {
            MemoryBlock block = currentProgram.getMemory().createInitializedBlock(
                name + "_IMAGE", start, in, file.length(), monitor, false);
            block.setRead(true);
            block.setWrite(false);
            block.setExecute(false);
        }
        println("attached " + path + " at " + start + " (" + file.length() + " bytes)");
    }

    /** Applies TMode over [start,end], clearing instructions that disagree first. */
    private void applyMode(Address start, Address end, boolean thumb) throws Exception {
        try {
            setMode(start.getOffset(), end.getOffset(), thumb);
        }
        catch (ContextChangeException conflict) {
            // A nested/overlapping listing symbol was already disassembled with the
            // other mode.  The current symbol owns this range, so redo it.  Widen the
            // clear to any instruction straddling start/end: clearListing() only
            // removes instructions that *begin* in [start,end], so an instruction
            // reaching in from before start would re-trigger the same conflict.
            Address clearStart = start;
            Instruction before = getInstructionContaining(start);
            if (before != null && before.getMinAddress().compareTo(clearStart) < 0) {
                clearStart = before.getMinAddress();
            }
            Address clearEnd = end;
            Instruction after = getInstructionContaining(end);
            if (after != null && after.getMaxAddress().compareTo(clearEnd) > 0) {
                clearEnd = after.getMaxAddress();
            }
            clearListing(clearStart, clearEnd);
            setMode(start.getOffset(), end.getOffset(), thumb);
        }
    }

    /** Applies TMode over [start,end], clamped to the program's memory blocks. */
    private void setMode(long start, long end, boolean thumb) throws Exception {
        if (start > end) {
            return;
        }
        for (MemoryBlock block : currentProgram.getMemory().getBlocks()) {
            long low = Math.max(start, block.getStart().getOffset());
            long high = Math.min(end, block.getEnd().getOffset());
            if (low <= high) {
                currentProgram.getProgramContext().setValue(
                    tmode, toAddr(low), toAddr(high), thumb ? BigInteger.ONE : BigInteger.ZERO);
            }
        }
    }
}
