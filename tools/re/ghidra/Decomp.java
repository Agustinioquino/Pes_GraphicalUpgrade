// Descompila funciones de PES2021.exe. Uso: -postScript Decomp.java <salida.c> <dir>[=nombre] [<dir>[=nombre] ...]
// Crea la funcion si no existe (siguiendo el flujo desde esa direccion) y le pone el nombre si se da.
// Tambien acepta "@archivo" con una direccion[=nombre] por linea.
// @category PES
import ghidra.app.decompiler.*;
import ghidra.app.script.GhidraScript;
import ghidra.app.cmd.disassemble.DisassembleCommand;
import ghidra.app.cmd.function.CreateFunctionCmd;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import java.io.*;
import java.nio.file.*;
import java.util.*;

public class Decomp extends GhidraScript {
    private Function ensure(Address a, String name) throws Exception {
        Function f = getFunctionAt(a);
        if (f == null) {
            if (getInstructionAt(a) == null) new DisassembleCommand(a, null, true).applyTo(currentProgram, monitor);
            new CreateFunctionCmd(a).applyTo(currentProgram, monitor);
            f = getFunctionAt(a);
        }
        if (f != null && name != null && !name.isEmpty() && !f.getName().equals(name)) f.setName(name, SourceType.USER_DEFINED);
        return f;
    }
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 2) { println("uso: Decomp.java salida.c dir[=nombre] ..."); return; }
        List<String> items = new ArrayList<>();
        for (int i = 1; i < args.length; i++) {
            if (args[i].startsWith("@")) { for (String l : Files.readAllLines(Paths.get(args[i].substring(1)))) { l = l.trim(); if (!l.isEmpty() && !l.startsWith("#")) items.add(l); } }
            else items.add(args[i]);
        }
        // primero crear / nombrar todas (asi las llamadas entre ellas salen con nombre)
        List<Function> fs = new ArrayList<>();
        for (String it : items) {
            String[] p = (it.startsWith("-") ? it.substring(1) : it).split("=", 2);
            Function f = ensure(toAddr(p[0]), p.length > 1 ? p[1] : null);
            fs.add(f);
        }
        DecompInterface ifc = new DecompInterface();
        DecompileOptions opts = new DecompileOptions();
        ifc.setOptions(opts);
        ifc.toggleCCode(true); ifc.toggleSyntaxTree(true); ifc.setSimplificationStyle("decompile");
        ifc.openProgram(currentProgram);
        try (PrintWriter pw = new PrintWriter(new FileWriter(args[0]))) {
            for (int i = 0; i < items.size(); i++) {
                Function f = fs.get(i);
                if (f == null) { pw.println("// no se pudo crear una funcion en " + items.get(i)); continue; }
                if (items.get(i).startsWith("-")) continue;   // "-dir=nombre": solo nombrar
                DecompileResults r = ifc.decompileFunction(f, 180, monitor);
                pw.println("// ==== " + f.getName() + " @ " + f.getEntryPoint() + " (" + f.getBody().getNumAddresses() + " bytes)");
                if (r.decompileCompleted()) pw.println(r.getDecompiledFunction().getC());
                else pw.println("// fallo: " + r.getErrorMessage());
            }
        }
        ifc.dispose();
    }
}
