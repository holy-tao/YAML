#Requires AutoHotkey v2.0

#Include ../../dist/YAML.ahk
#Include Bench.ahk
#Include Cases.ahk

A_FileEncoding := "UTF-8"

scriptDir   := A_ScriptDir
corpusRoot  := scriptDir "\corpus"
resultsRoot := scriptDir "\results"

if !DirExist(corpusRoot) {
    FileAppend("Corpus not found at " corpusRoot ". Run fetch-corpus.ps1 first.`n", "*")
    ExitApp(1)
}
if !DirExist(resultsRoot)
    DirCreate(resultsRoot)

bits := A_PtrSize == 8 ? "x64" : "x86"
ts := FormatTime(, "yyyyMMdd-HHmmss")
csvPath := resultsRoot "\bench-" ts "-" bits ".csv"

csv := FileOpen(csvPath, "w", "UTF-8")
csv.WriteLine(Bench.CsvHeader())

testCases := Cases.Build(corpusRoot)
if testCases.Length == 0 {
    FileAppend("No corpus files present under " corpusRoot ".`n", "*")
    ExitApp(1)
}

PrintLine(Format("{1:-44s} {2:-14s} {3:12s} {4:5s}  {5:14s}  {6:14s}  {7:14s}  {8:14s}  {9:11s}",
    "case", "op", "bytes", "iters",
    "mean", "stddev", "min", "max", "MB/s"))
PrintLine(StrRepeat("-", 155))

scratchDump := A_Temp "\yaml-bench-dump.tmp"

for c in testCases {
    RunCase(c, csv, scratchDump)
}

csv.Close()
FileAppend("`nResults written to " csvPath "`n", "*")

RunCase(c, csv, scratchDump) {
    yamlStr := FileRead(c.path, "UTF-8")
    inBytes := FileGetSize(c.path)

    ; Parse once outside the loop for dump-side ops. If parse fails (e.g.
    ; multi-document, or any other reason), skip dump-side ops for this case.
    tree := ""
    parseErr := ""
    try {
        tree := YAML.Parse(yamlStr)
    } catch Any as e {
        parseErr := Type(e) ": " e.Message
    }

    ; Probe a dump to learn the output size for MB/s on dump-side rows.
    outBytes := 0
    if !parseErr {
        try {
            dumped := YAML.Dump(tree)
            outBytes := StrPut(dumped, "UTF-8") - 1
        } catch Any as e {
            ; ignore - dump-side ops will surface the error
        }
    }

    ; Parse (string IO)
    RunOp(c.name, "Parse", inBytes, csv,
        () => YAML.Parse(yamlStr),
        parseErr)

    ; ParseFile (libyaml file IO)
    RunOp(c.name, "ParseFile", inBytes, csv,
        () => YAML.ParseFile(c.path),
        parseErr)

    if !parseErr {
        ; Dump (string IO)
        RunOp(c.name, "Dump", outBytes, csv,
            () => YAML.Dump(tree),
            "")

        ; DumpFile (libyaml file IO)
        RunOp(c.name, "DumpFile", outBytes, csv,
            () => YAML.DumpFile(tree, scratchDump),
            "")

        ; RoundTrip (string IO)
        RunOp(c.name, "RoundTrip", inBytes, csv,
            () => YAML.Dump(YAML.Parse(yamlStr)),
            "")

        ; RoundTripFile (libyaml file IO)
        RunOp(c.name, "RoundTripFile", inBytes, csv,
            () => YAML.DumpFile(YAML.ParseFile(c.path), scratchDump),
            "")
    } else {
        LogSkip(c.name, "Dump",          csv, parseErr)
        LogSkip(c.name, "DumpFile",      csv, parseErr)
        LogSkip(c.name, "RoundTrip",     csv, parseErr)
        LogSkip(c.name, "RoundTripFile", csv, parseErr)
    }

    ; Cleanup scratch file between cases
    if FileExist(scratchDump)
        FileDelete(scratchDump)
}

RunOp(name, op, bytes, csv, fn, preErr) {
    if preErr {
        LogSkip(name, op, csv, preErr)
        return
    }
    try {
        stats := Bench.Time(fn)
    } catch Any as e {
        LogSkip(name, op, csv, Type(e) ": " e.Message)
        return
    }
    row := Bench.FormatRow(name, op, bytes, stats)
    PrintLine(row.console)
    csv.WriteLine(row.csv)
}

LogSkip(name, op, csv, reason) {
    PrintLine(Format("{1:-44s} {2:-14s} {3:s}", name, op, "SKIPPED: " reason))
    csv.WriteLine(Format("{1},{2},,,,,,,SKIPPED: {3}",
        CsvEscape(name), CsvEscape(op), CsvEscape(reason)))
}

CsvEscape(s) {
    if InStr(s, ",") || InStr(s, "`"") || InStr(s, "`n") {
        s := StrReplace(s, "`"", "`"`"")
        return "`"" s "`""
    }
    return s
}

PrintLine(s) => FileAppend(s "`n", "*")

StrRepeat(s, n) {
    out := ""
    loop n
        out .= s
    return out
}
