// @category PES
import ghidra.app.script.GhidraScript;
public class Opts extends GhidraScript {
    public void run() throws Exception {
        setAnalysisOption(currentProgram, "Decompiler Parameter ID", "false");
        setAnalysisOption(currentProgram, "Aggressive Instruction Finder", "false");
        setAnalysisOption(currentProgram, "Function Start Search", "false");
        setAnalysisOption(currentProgram, "Non-Returning Functions - Discovered", "true");
    }
}
