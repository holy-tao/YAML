#Requires AutoHotkey v2.0
#SingleInstance Force

#Include ..\src\Lib\MCL\MCL.ahk

root       := A_LineFile "\..\.."
libyaml    := root "\src\native\libyaml"
libyamlSrc := libyaml "\src"
native     := root "\src\native\src"
shims      := root "\src\native\shims"

libyamlFiles := ["api.c", "reader.c", "scanner.c", "parser.c",
                 "loader.c", "writer.c", "emitter.c", "dumper.c"]

libyamlBlob := ""
for f in libyamlFiles
    libyamlBlob .= "`n/* ==== " f " ==== */`n" FileRead(libyamlSrc "\" f) "`n"

; See spike.c
code := StrReplace(FileRead(native "\spike.c"), "/*__LIBYAML_SOURCES__*/", libyamlBlob)

defines := ' -DYAML_VERSION_MAJOR=0 -DYAML_VERSION_MINOR=2 -DYAML_VERSION_PATCH=5 -DYAML_VERSION_STRING=\"0.2.5\" '

options := {
    flags: " -I `"" shims "`" -I `"" libyaml "\include`" -I `"" libyamlSrc "`" -I `"" native "`" " defines,
    bitness: 64
}

try {
    pCode := MCL.FromC(code, options)
} catch Error as e {
    FileAppend("=== COMPILER ERROR ===`n" e.Message "`n", "*")
    ExitApp 1
}

Result := DllCall(pCode, "CDecl Int")
FileAppend("Spike returned: " Result "`n", "*")
ExitApp (Result == 42 ? 0 : 2)
