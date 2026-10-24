// Prepara el programa importado (_text.bin en 0x140001000) con el resto de las secciones del volcado de PES2021.exe
// 1.07.02 (exedump.cpp) y etiqueta la tabla de importaciones (imports.txt). Uso: -postScript SetupPES.java <carpeta del volcado>
// @category PES
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.mem.*;
import ghidra.program.model.symbol.*;
import ghidra.program.model.data.*;
import java.io.*;
import java.nio.file.*;
import java.util.*;

public class SetupPES extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        String dir = args.length > 0 ? args[0] : "/mnt/user-data/uploads/eFootball PES 2021/PES21FX/dump/";
        Memory mem = currentProgram.getMemory();
        AddressSpace sp = currentProgram.getAddressFactory().getDefaultAddressSpace();
        MemoryBlock tb = mem.getBlock(sp.getAddress(0x140001000L));
        if (tb != null) { tb.setName(".text"); tb.setRead(true); tb.setWrite(false); tb.setExecute(true); }
        Object[][] secs = {
            {".idata", 0x142538000L, "_idata.bin", "r"},
            {".text1", 0x1433a9000L, "_text1.bin", "rw"},
            {".pdata", 0x143ad3000L, "_pdata.bin", "r"},
            {".data2", 0x143cb0000L, "_data2.bin", "rw"},
            {".data",  0x15b479000L, "_data.bin", "r"},
        };
        for (Object[] s : secs) {
            File f = new File(dir, (String) s[2]);
            if (!f.exists()) { println("falta " + f); continue; }
            Address a = sp.getAddress((Long) s[1]);
            if (mem.getBlock(a) != null) { println("ya existe " + s[0]); continue; }
            try (InputStream is = new FileInputStream(f)) {
                MemoryBlock b = mem.createInitializedBlock((String) s[0], a, is, f.length(), monitor, false);
                b.setRead(true); b.setWrite(((String) s[3]).contains("w")); b.setExecute(false);
                println("bloque " + s[0] + " " + a + " " + f.length());
            }
        }
        // tabla de importaciones (.trace en 0x15b470000): bloque sin datos con una etiqueta por funcion importada
        Address iatA = sp.getAddress(0x15b470000L);
        if (mem.getBlock(iatA) == null) { MemoryBlock iat = mem.createUninitializedBlock(".iat", iatA, 0x8DA0, false); iat.setRead(true); }
        SymbolTable st = currentProgram.getSymbolTable();
        Path imp = Paths.get(dir, "imports.txt");
        int n = 0;
        if (Files.exists(imp)) {
            for (String l : Files.readAllLines(imp)) {
                l = l.trim(); if (l.isEmpty()) continue;
                String[] p = l.split(" ", 2); if (p.length < 2) continue;
                long rva = Long.parseLong(p[0], 16);
                String fn = p[1].substring(p[1].indexOf('!') + 1).replaceAll("[^A-Za-z0-9_#]", "_").replace('#', 'o');
                Address a = sp.getAddress(0x140000000L + rva);
                try {
                    st.createLabel(a, "imp_" + fn, SourceType.IMPORTED);
                    clearListing(a, a.add(7));
                    createData(a, PointerDataType.dataType);
                    n++;
                } catch (Exception e) { println("import " + l + ": " + e.getMessage()); }
            }
        }
        println("importaciones etiquetadas: " + n);
    }
}
