// Exporta: functions.tsv (dir, nombre, tamano, llamadores), strings_xref.tsv (cadena -> funciones que la usan)
// y el C descompilado de todas las funciones en archivos de ~3000 funciones. Uso: -postScript ExportPES.java <salida> [solo_tsv]
// @category PES
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.app.decompiler.parallel.*;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import ghidra.program.model.data.*;
import ghidra.util.task.TaskMonitor;
import java.io.*;
import java.util.*;

public class ExportPES extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] a = getScriptArgs();
        File out = new File(a[0]); out.mkdirs();
        boolean onlyTsv = a.length > 1;
        FunctionManager fm = currentProgram.getFunctionManager();
        ReferenceManager rm = currentProgram.getReferenceManager();
        List<Function> all = new ArrayList<>();
        for (Function f : fm.getFunctions(true)) all.add(f);
        try (PrintWriter pw = new PrintWriter(new BufferedWriter(new FileWriter(new File(out, "functions.tsv"))))) {
            pw.println("dir\tnombre\tbytes\tllamadores");
            for (Function f : all) pw.println(f.getEntryPoint() + "\t" + f.getName() + "\t" + f.getBody().getNumAddresses() + "\t" + rm.getReferenceCountTo(f.getEntryPoint()));
        }
        int ns = 0;
        try (PrintWriter pw = new PrintWriter(new BufferedWriter(new FileWriter(new File(out, "strings_xref.tsv"))))) {
            pw.println("dir_cadena\tcadena\tfunciones");
            for (Data d : currentProgram.getListing().getDefinedData(true)) {
                if (!(d.getDataType() instanceof AbstractStringDataType)) continue;
                Object v = d.getValue(); if (v == null) continue;
                String s = v.toString().replace("\t", " ").replace("\n", "\\n").replace("\r", "");
                if (s.length() < 4) continue;
                Set<String> fs = new TreeSet<>();
                for (Reference r : rm.getReferencesTo(d.getAddress())) {
                    Function f = fm.getFunctionContaining(r.getFromAddress());
                    fs.add(f != null ? f.getName() + "@" + f.getEntryPoint() : "?" + r.getFromAddress());
                    if (fs.size() > 30) break;
                }
                pw.println(d.getAddress() + "\t" + s + "\t" + String.join(",", fs)); ns++;
            }
        }
        println("funciones " + all.size() + " cadenas " + ns);
        if (onlyTsv) return;
        final int CH = 3000;
        for (int i = 0; i < all.size(); i += CH) {
            List<Function> part = all.subList(i, Math.min(all.size(), i + CH));
            String nm = String.format("c_%s.c", part.get(0).getEntryPoint());
            File fo = new File(out, "c/" + nm); fo.getParentFile().mkdirs();
            if (fo.exists()) continue;
            DecompilerCallback<String> cb = new DecompilerCallback<String>(currentProgram, new DecompileConfigurer() {
                public void configure(DecompInterface d) {
                    DecompileOptions o = new DecompileOptions(); d.setOptions(o);
                    d.toggleCCode(true); d.toggleSyntaxTree(false); d.setSimplificationStyle("decompile");
                }
            }) {
                public String process(DecompileResults r, TaskMonitor m) {
                    Function f = r.getFunction();
                    String h = "// ==== " + f.getName() + " @ " + f.getEntryPoint() + "\n";
                    if (r.decompileCompleted()) return h + r.getDecompiledFunction().getC();
                    return h + "// fallo: " + r.getErrorMessage() + "\n";
                }
            };
            cb.setTimeout(60);
            List<String> res = ParallelDecompiler.decompileFunctions(cb, part, monitor);
            cb.dispose();
            Map<String, String> byAddr = new TreeMap<>();
            for (String s : res) if (s != null) byAddr.put(s.substring(s.indexOf(" @ ") + 3, s.indexOf("\n")), s);
            File tmp = new File(fo.getPath() + ".tmp");
            try (PrintWriter pw = new PrintWriter(new BufferedWriter(new FileWriter(tmp)))) { for (String s : byAddr.values()) pw.println(s); }
            tmp.renameTo(fo);
            println("C " + nm + " " + (i + part.size()) + "/" + all.size());
        }
    }
}
