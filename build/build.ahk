#Requires AutoHotkey v2.0
#SingleInstance Force

#Include ..\src\Lib\MCL\MCL.ahk

; MCL includes StdoutToVar but it's function-scoped
#IncludeAgain ..\src\Lib\MCL\Lib\StdoutToVar.ahk

/*
 * Produces dist/YAML.ahk - a single-file amalgamation of:
 *   - The public YAML / YAMLError / YAMLParseError / YAMLMultiDocError classes
 *   - An embedded standalone AHK function `_YAMLMCode()` containing both
 *     32-bit and 64-bit machine-code blobs of parse.c + dump.c + libyaml.
 *
 * End users `#Include dist\YAML.ahk` and need no C toolchain. Developers
 * iterating on C need MinGW-w64 gcc on PATH.
 */

; Minimal command line processor
HasArg(str) {
    for arg in A_Args {
        if arg = str
         return true
    }
    return false
}

; Log uncaught errors and exit with an error code
OnError((thrown, mode) => (
    FileAppend(Type(thrown) ": " thrown.Message "`n`n" thrown.Stack "`n", "*"),
    ExitApp(1)
))

root       := A_LineFile "\..\.."
libyaml    := root "\src\native\libyaml"
libyamlSrc := libyaml "\src"
native     := root "\src\native\src"
shims      := root "\src\native\shims"
dist       := root "\dist"

; Get build info
mclCommit := Trim(StdoutToVar("git rev-parse HEAD", A_ScriptDir "\..\src\Lib\MCL").Output, "`r`n`t ")
libYamlCommit := Trim(StdoutToVar("git rev-parse HEAD", A_ScriptDir "\..\src\native\libyaml").Output, "`r`n`t ")
thisCommit := Trim(StdoutToVar("git rev-parse HEAD", A_ScriptDir).Output, "`r`n`t ")
FileAppend(Format("Building at {1} with libraries:`n  LibYAML: {2}`n  MCL:     {3}`n", 
    thisCommit, libYamlCommit, mclCommit), "*")

if !DirExist(dist) {
    FileAppend("Creating dist directory at " dist "`n", "*")
    DirCreate(dist)
}

libyamlFiles := ["api.c", "reader.c", "scanner.c", "parser.c",
                 "loader.c", "writer.c", "emitter.c", "dumper.c"]

libyamlBlob := ""
for f in libyamlFiles
    libyamlBlob .= "`n/* ==== " f " ==== */`n" FileRead(libyamlSrc "\" f) "`n"

; Shell translation unit: libc shims, then libyaml .c files spliced inline
; (so their internal `#include "yaml_private.h"` resolves against our shim),
; then parse.c and dump.c.
shell := "
(
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static char* strdup(const char* s) {
    size_t n = strlen(s) + 1;
    char* p = (char*)malloc(n);
    if (p) memcpy(p, s, n);
    return p;
}

static int ferror(FILE* f) { (void)f; return 0; }

#include <yaml.h>

/*__LIBYAML_SOURCES__*/

)"

shell := StrReplace(shell, "/*__LIBYAML_SOURCES__*/", libyamlBlob)
code := shell "`n" FileRead(native "\parse.c") "`n" FileRead(native "\dump.c")

defines := ' -DYAML_VERSION_MAJOR=0 -DYAML_VERSION_MINOR=2 -DYAML_VERSION_PATCH=5 -DYAML_VERSION_STRING=\"0.2.5\" '

; Optimize for size. -mno-stack-arge-probe is required to prevent the linker error
; `Reference to undefined symbol ___chkstk_ms` that happens because gcc inserts a
; stack probe in for stack frames > 4kb, which we use in Parse to hold the object
; stack
constantFlags := "-mno-stack-arg-probe -Os"

; When the runner has debug logging on (re-run with "Enable debug logging" or
; ACTIONS_STEP_DEBUG secret), ask gcc to be loud.
if EnvGet("RUNNER_DEBUG") = "1" || HasArg("debug") {
    constantFlags .= " -v -ftime-report -fmem-report"
}

rendererOptions := {
    name: "_YAMLMCode",
    wrapper: "function",
    static: true,
    compress: true
}

for bitness in ["", 32, 64] {
    Build(bitness)
}

/**
 * Run the actual build
 * @param {32 | 64 | ""} bitness Bitness to build for, or a falsy value to build for all
 */
Build(bitness := "") {
    compilerOptions := {
        flags: constantFlags " -I `"" shims "`" -I `"" libyaml "\include`" -I `"" libyamlSrc "`" -I `"" native "`" " defines
    }

    if bitness
        compilerOptions.bitness := bitness

    msg := Format("Compiling parse.c + dump.c + libyaml ({1})...`n", 
        bitness ? bitness " bit" : "bitness-agnositc")

    FileAppend(msg, "*")
    FileAppend(Format("  code size: {} bytes`n", StrLen(code)), "*")

    try {
        FileAppend("  calling MCL.StandaloneAHKFromC...`n", "*")
        standalone := MCL.StandaloneAHKFromC(code, compilerOptions, rendererOptions)
        FileAppend(Format("  returned: {} bytes of standalone output`n", StrLen(standalone)), "*")
    } catch Any as e {
        FileAppend("`n=== CAUGHT " Type(e) " ===`n", "*")
        FileAppend("Message: " (e.HasProp("Message") ? e.Message : "<none>") "`n", "*")
        FileAppend("Extra:   " (e.HasProp("Extra")   ? e.Extra   : "<none>") "`n", "*")
        FileAppend("Stack:`n" (e.HasProp("Stack")    ? e.Stack   : "<none>") "`n", "*")
        ExitApp(1)
    }

    facade := FileRead(root "\src\YAML.ahk")

    ; Strip the per-file `#Requires` from the facade - we add one at the top of
    ; the amalgamation.
    facade := RegExReplace(facade, "^#Requires[^\r\n]*\R", "")

    ; Copy the relevant licenses into the header to be polite - luckily everything's MIT.
    header := Format("
    (comments
    /*
    [LibYAML] powered YAML library for AutoHotkey v2. Distributed under the MIT License:

    {1}
    ---

    Portions [MCL.ahk] distributed under the MIT License:

    {2}
    ---

    LibYAML is embedded as compiled code and is distributed under the MIT license:

    {3}
    ---

    [MCL.ahk]: https://github.com/G33kDude/MCL.ahk
    [LibYAML]: https://github.com/yaml/libyaml

    Generated {4} (UTC) @ {5}
    With libraries:
    LibYAML: {6}
    MCL:     {7}
    */

    #Requires AutoHotkey v2.0 {8}

    )", 
        FileRead("../LICENSE", "UTF-8"), 
        FileRead("../src/Lib/MCL/LICENSE", "UTF-8"), 
        FileRead("../src/native/libyaml/License", "UTF-8"),
        FormatTime(A_NowUTC),
        thisCommit, libYamlCommit, mclCommit,
        bitness ? bitness "-bit" : ""
    )

    out := header "`n" facade "`n" standalone "`n"

    distPath := Format("{1}\YAML{2}.ahk", dist, bitness || "")
    dest := FileOpen(distPath, "w", "UTF-8")
    dest.Write(out)
    sz := dest.Length
    dest.Close()

    FileAppend(Format("Wrote {} ({} bytes)`n", distPath, sz), "*")
}

ExitApp 0
