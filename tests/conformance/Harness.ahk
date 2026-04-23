#Requires AutoHotkey v2.0

#Include ../../dist/YAML.ahk

class ConformanceHarness {
    static Dimensions := ["parse", "parse-error", "round-trip", "dump-json"]

    __New(suiteDir, reportPath) {
        this.suiteDir := suiteDir
        this.reportPath := reportPath
        this.results := Map()
        for dim in ConformanceHarness.Dimensions
            this.results[dim] := { pass: 0, total: 0, failures: [] }
    }

    Run() {
        YAML.NullsAsStrings := false
        YAML.BoolsAsInts := false

        tests := this._EnumerateTests()
        for t in tests
            this._RunOne(t)

        this._WriteReport(tests.Length)
    }

    _EnumerateTests() {
        out := []
        Loop Files, this.suiteDir "\*", "D" {
            name := A_LoopFileName
            if !RegExMatch(name, "^[0-9A-Z]{4}$")
                continue
            out.Push({
                id:       name,
                dir:      A_LoopFileFullPath,
                hasError: FileExist(A_LoopFileFullPath "\error") != "",
                hasJson:  FileExist(A_LoopFileFullPath "\in.json") != "",
                hasYaml:  FileExist(A_LoopFileFullPath "\in.yaml") != "",
                label:    FileExist(A_LoopFileFullPath "\===") ? Trim(FileRead(A_LoopFileFullPath "\===", "UTF-8"), " `t`r`n") : ""
            })
        }
        return out
    }

    _RunOne(t) {
        if !t.hasYaml
            return

        yamlText := FileRead(t.dir "\in.yaml", "UTF-8")

        parsed := unset
        parseOk := false
        parseThrew := false
        parseWhatThrew := "[Unknown]"
        parseWasYamlError := false
        parseErrDesc := ""

        try {
            parsed := YAML.Parse(yamlText)
            parseOk := true
        } catch YAMLError as e {
            parseThrew := true
            parseWasYamlError := true
            parseErrDesc := Type(e) ": " e.Message
            parseWhatThrew := StrLen(e.What) > 0 ? e.What : "[Anonymous]"
        } catch Any as e {
            parseThrew := true
            parseWasYamlError := false
            parseErrDesc := Type(e) ": " e.Message
            parseWhatThrew := StrLen(e.What) > 0 ? e.What : "[Anonymous]"
        }

        if t.hasError {
            this._Record("parse-error", t, parseThrew && parseWasYamlError,
                parseOk ? "parse succeeded but error expected"
                        : parseThrew ? "non-YAMLError thrown (" parseErrDesc ")" : "")
            return
        }

        if t.hasJson {
            expected := unset
            jsonOk := false
            try {
                expected := JsonAdapter.Load(FileRead(t.dir "\in.json", "UTF-8"))
                jsonOk := true
            } catch as e {
                this._Record("parse", t, false, "in.json load failed: " e.Message)
            }

            if jsonOk {
                if !parseOk {
                    this._Record("parse", t, false, parseWhatThrew " threw a(n) " parseErrDesc)
                } else {
                    match := TreeCompare.Equal(parsed, expected)
                    this._Record("parse", t, match, match ? "" : "tree mismatch")
                }

                this._RunDumpFromJson(t, expected)
            }
        }

        if parseOk
            this._RunRoundTrip(t, parsed)
    }

    _RunRoundTrip(t, parsed) {
        try {
            dumped := YAML.Dump(parsed)
            reparsed := YAML.Parse(dumped)
            match := TreeCompare.Equal(parsed, reparsed)
            this._Record("round-trip", t, match, match ? "" : "reparsed tree mismatch")
        } catch as e {
            this._Record("round-trip", t, false, e.Message)
        }
    }

    _RunDumpFromJson(t, tree) {
        try {
            dumped := YAML.Dump(tree)
            reparsed := YAML.Parse(dumped)
            match := TreeCompare.Equal(tree, reparsed)
            this._Record("dump-json", t, match, match ? "" : "reparsed tree mismatch")
        } catch as e {
            this._Record("dump-json", t, false, e.Message)
        }
    }

    _Record(dim, t, pass, detail) {
        r := this.results[dim]
        r.total++
        if pass
            r.pass++
        else
            r.failures.Push({ id: t.id, label: t.label, detail: detail })
    }

    _WriteReport(totalTests) {
        lines := []
        lines.Push("yaml-test-suite conformance - " FormatTime(, "yyyy-MM-dd HH:mm:ss") " local")
        lines.Push("suite: " this.suiteDir)
        lines.Push("tests discovered: " totalTests)
        lines.Push("")
        lines.Push("totals:")
        for dim in ConformanceHarness.Dimensions {
            r := this.results[dim]
            pct := r.total > 0 ? Format("{:.1f}%", 100.0 * r.pass / r.total) : "n/a"
            lines.Push(Format("  {:-11s} : {:4} / {:4} pass  ({})", dim, r.pass, r.total, pct))
        }
        lines.Push("")
        lines.Push("failures by dimension:")
        for dim in ConformanceHarness.Dimensions {
            r := this.results[dim]
            if r.failures.Length = 0
                continue
            lines.Push("  [" dim "]")
            for f in r.failures {
                line := "    " f.id
                if f.label != ""
                    line .= '  "' f.label '"'
                if f.detail != ""
                    line .= "  -- " f.detail
                lines.Push(line)
            }
        }

        text := ""
        for l in lines
            text .= l "`r`n"

        dir := RegExReplace(this.reportPath, "\\[^\\]+$")
        if !DirExist(dir)
            DirCreate(dir)
        if FileExist(this.reportPath)
            FileDelete(this.reportPath)
        FileAppend(text, this.reportPath, "UTF-8")

        FileAppend(text, "*", "UTF-8")
    }
}
