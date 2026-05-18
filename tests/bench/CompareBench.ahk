#Requires AutoHotkey v2.0

; Side-by-side parse/dump timings: this library vs HotKeyIt/Yaml.
;
; Run via run-bench.ps1 -Compare. Requires:
;   - dist/YAML.ahk built
;   - tests/bench/corpus populated (fetch-corpus.ps1)
;   - tests/bench/vendor/HotKeyIt-Yaml.ahk present (fetch-hotkeyit.ps1)
;
; Output: tests/bench/results/compare-{ts}-{bits}.csv with one row per
; (case, op) where op in {Parse, Dump}, and columns for each library's
; mean_ms plus a ratio (hkit / ours; >1 means ours is faster).

#Include ../../dist/YAML.ahk
#Include vendor/HotKeyIt-Yaml.ahk
#Include Bench.ahk
#Include Cases.ahk

; HotKeyIt uses AHK_H UMap, which is just a case-insensitive Map. Not available
; in mainstream AHK
UMap() => (m := Map(), m.CaseSense := false, m)

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
csvPath := resultsRoot "\compare-" ts "-" bits ".csv"

csv := FileOpen(csvPath, "w", "UTF-8")
csv.WriteLine("case,op,bytes,ours_ms,hkit_ms,ratio_hkit_over_ours,ours_iters,hkit_iters,ours_status,hkit_status")

testCases := Cases.Build(corpusRoot)
if testCases.Length == 0 {
    FileAppend("No corpus files present under " corpusRoot ".`n", "*")
    ExitApp(1)
}

PrintLine(Format("{1:-44s} {2:-6s} {3:12s}  {4:12s}  {5:12s}  {6:8s}",
    "case", "op", "bytes", "ours ms", "hkit ms", "hkit/ours"))
PrintLine(StrRepeat("-", 110))

for c in testCases {
    RunCase(c, csv)
}

csv.Close()
FileAppend("`nResults written to " csvPath "`n", "*")

RunCase(c, csv) {
    yamlStr := FileRead(c.path, "UTF-8")
    inBytes := FileGetSize(c.path)

    ; --- Parse ---
    yoursParse := TimeOp(() => YAML.Parse(yamlStr))
    hkitParse  := TimeOp(() => HKIYaml(yamlStr))

    EmitRow(c.name, "Parse", inBytes, yoursParse, hkitParse, csv)

    ; --- Dump ---
    ; Each library dumps its own tree (fidelity is not the point here).
    yoursTree := "", hkitTree := ""
    yoursDumpStatus := "ok", hkitDumpStatus := "ok"

    try {
        yoursTree := YAML.Parse(yamlStr)
    } catch Any as e {
        yoursDumpStatus := "skip-parse:" Type(e) ": " e.Message
    }
    try {
        hkitTree := HKIYaml(yamlStr)
    } catch Any as e {
        hkitDumpStatus := "skip-parse:" Type(e) ": " e.Message
    }

    yoursDump := (yoursDumpStatus == "ok")
        ? TimeOp(() => YAML.Dump(yoursTree))
        : {status: yoursDumpStatus, mean: 0, iters: 0}
    hkitDump  := (hkitDumpStatus  == "ok")
        ? TimeOp(() => HKIYaml(hkitTree, 99))   ; positive indent = YAML output
        : {status: hkitDumpStatus,  mean: 0, iters: 0}

    EmitRow(c.name, "Dump", inBytes, yoursDump, hkitDump, csv)
}

TimeOp(fn) {
    try {
        s := Bench.Time(fn)
        return {status: "ok", mean: s.mean, iters: s.iters}
    } catch Any as e {
        return {status: "err:" Type(e) ": " e.Message, mean: 0, iters: 0}
    }
}

EmitRow(name, op, bytes, yours, hkit, csv) {
    yoursMs := yours.mean * 1000
    hkitMs  := hkit.mean  * 1000
    ratio   := (yours.status == "ok" && hkit.status == "ok" && yoursMs > 0)
        ? hkitMs / yoursMs
        : 0.0

    yoursStr := yours.status == "ok" ? Fmt(yoursMs, 3) : yours.status
    hkitStr  := hkit.status  == "ok" ? Fmt(hkitMs, 3)  : hkit.status
    ratioStr := (ratio > 0) ? Fmt(ratio, 2) "x" : "-"

    PrintLine(Format("{1:-44s} {2:-6s} {3:12s}  {4:12s}  {5:12s}  {6:8s}",
        name, op, String(bytes), yoursStr, hkitStr, ratioStr))

    csv.WriteLine(Format("{1},{2},{3},{4},{5},{6},{7},{8},{9},{10}",
        CsvEscape(name), CsvEscape(op), bytes,
        yours.status == "ok" ? Fmt(yoursMs, 6) : "",
        hkit.status  == "ok" ? Fmt(hkitMs,  6) : "",
        ratio > 0 ? Fmt(ratio, 4) : "",
        yours.iters, hkit.iters,
        CsvEscape(yours.status), CsvEscape(hkit.status)))
}

Fmt(n, places) => Format("{1:." places "f}", n)

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
