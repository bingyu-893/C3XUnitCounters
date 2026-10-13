// Export local reverse-engineering evidence for the air counter regression test.
// @category C3X
// Headless arguments: <output-directory> <gog|steam|pcg>
// Use -readOnly -noanalysis when inspecting an existing project.
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.symbol.Reference;
import java.nio.file.*;
import java.io.*;

public class ExportAirCombat extends GhidraScript {
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length != 2 || !args[1].matches("gog|steam|pcg"))
            throw new IllegalArgumentException("Expected output directory and gog|steam|pcg label");
        Path out = Paths.get(args[0]);
        Files.createDirectories(out);
        String version = args[1];
        try (PrintWriter w = new PrintWriter(Files.newBufferedWriter(out.resolve(version + ".txt")))) {
            w.println(currentProgram.getDomainFile().getPathname());
            w.println(currentProgram.getExecutableSHA256());
            for (MemoryBlock b : currentProgram.getMemory().getBlocks()) {
                if (b.getName().equals(".text") && b.isInitialized()) {
                    if (b.getStart().getOffset() != 0x401000)
                        throw new IllegalArgumentException("Unexpected .text base");
                    byte[] bytes = new byte[(int)b.getSize()];
                    b.getBytes(b.getStart(), bytes);
                    Files.write(out.resolve(version + ".text.bin"), bytes);
                    w.println("TEXT " + b.getStart() + " " + bytes.length);
                }
            }
            for (Function f : currentProgram.getFunctionManager().getFunctions(true)) {
                if (!(f.getName().equals("do_air_combat") || f.getName().equals("try_flying_over_tile"))) continue;
                w.println("FUNCTION " + f.getEntryPoint() + " " + f.getPrototypeString(true, true));
                for (Reference r : getReferencesTo(f.getEntryPoint())) w.println("XREF " + r);
                for (Instruction i : currentProgram.getListing().getInstructions(f.getBody(), true))
                    w.println(i.getAddress() + " " + i);
            }
        }
    }
}
