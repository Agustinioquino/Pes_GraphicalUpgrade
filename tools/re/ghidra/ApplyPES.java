// Crea las funciones de .pdata (funcs.txt) y nombra vtables y metodos virtuales por RTTI (rtti.txt).
// Uso: -preScript ApplyPES.java <carpeta con funcs.txt y rtti.txt>
// @category PES
import ghidra.app.script.GhidraScript;
import ghidra.app.cmd.disassemble.DisassembleCommand;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import ghidra.program.model.data.*;
import java.nio.file.*;
import java.util.*;

public class ApplyPES extends GhidraScript {
    @Override
    public void run() throws Exception {
        String dir = getScriptArgs()[0];
        AddressSpace sp = currentProgram.getAddressFactory().getDefaultAddressSpace();
        Listing L = currentProgram.getListing();
        FunctionManager fm = currentProgram.getFunctionManager();
        SymbolTable st = currentProgram.getSymbolTable();
        int nf = 0, bad = 0;
        AddressSet all = new AddressSet();
        List<long[]> fl = new ArrayList<>();
        for (String l : Files.readAllLines(Paths.get(dir, "funcs.txt"))) {
            String[] p = l.split(" ");
            long b = Long.parseLong(p[0], 16), e = Long.parseLong(p[1], 16);
            all.add(sp.getAddress(b), sp.getAddress(e - 1));
            fl.add(new long[]{b, e, Long.parseLong(p[2])});
        }
        new DisassembleCommand(all, all, true).applyTo(currentProgram, monitor);
        println("desensamblado .pdata listo");
        for (long[] f : fl) {
            if (f[2] != 0) continue;   // trozo encadenado: es parte de otra funcion
            Address a = sp.getAddress(f[0]);
            if (fm.getFunctionAt(a) != null) continue;
            try {
                fm.createFunction(null, a, new AddressSet(a, sp.getAddress(f[1] - 1)), SourceType.ANALYSIS);
                nf++;
            } catch (Exception ex) { bad++; }
            if ((nf & 0x3fff) == 0) monitor.setMessage("funciones " + nf);
        }
        println("funciones creadas " + nf + " fallidas " + bad);
        Map<Long, String> fname = new HashMap<>();
        Map<Long, Integer> uses = new HashMap<>();
        int nv = 0;
        for (String l : Files.readAllLines(Paths.get(dir, "rtti.txt"))) {
            String[] p = l.split(" ");
            long vt = Long.parseLong(p[1], 16);
            String cls = p[2]; int off = Integer.parseInt(p[3]); int n = Integer.parseInt(p[4]);
            String vn = cls + (off == 0 ? "::vftable" : "::vftable_" + off);
            try { st.createLabel(sp.getAddress(vt), vn.replace("::", "__"), SourceType.ANALYSIS); nv++; } catch (Exception ex) {}
            for (int i = 0; i < n; i++) {
                long e = Long.parseLong(p[5 + i], 16);
                uses.merge(e, 1, Integer::sum);
                if (!fname.containsKey(e) && off == 0) fname.put(e, cls.replace("::", "__") + "__vf" + i);
            }
        }
        int nn = 0;
        for (Map.Entry<Long, String> e : fname.entrySet()) {
            if (uses.get(e.getKey()) > 3) continue;   // compartida (stubs, destructores triviales): no nombrar
            Function f = fm.getFunctionAt(sp.getAddress(e.getKey()));
            if (f == null) continue;
            try { f.setName(e.getValue(), SourceType.ANALYSIS); nn++; } catch (Exception ex) {}
        }
        println("vtables " + nv + " metodos nombrados " + nn);
    }
}
