#Requires AutoHotkey v2.0

; Smoke test for dist/YAML.ahk, verifies the amalgamation loads and round-trips.
; NOT run in CI, mostly just a sanity check
; Run: AutoHotkey64.exe //ErrorStdOut smoke.ahk

#Include ..\dist\YAML.ahk

Pass := 0, Fail := 0
Check(label, cond) {
    global Pass, Fail
    if cond {
        Pass++
        FileAppend("  OK   " label "`n", "*")
    } else {
        Fail++
        FileAppend("  FAIL " label "`n", "*")
    }
}

try {
    Check("Parse int",        YAML.Parse("42") == 42)
    Check("Parse string",     YAML.Parse("hello") == "hello")
    Check("Parse null",       YAML.Parse("null") == "")
    Check("Parse bool true",  YAML.Parse("true") == 1)
    Check("Parse hex",        YAML.Parse("0x2A") == 42)

    m := YAML.Parse("a: 1`nb: hello")
    Check("Parse map.a",      m["a"] == 1)
    Check("Parse map.b",      m["b"] == "hello")

    arr := YAML.Parse("- 1`n- two`n- 3")
    Check("Parse arr.len",    arr.Length == 3)
    Check("Parse arr[2]",     arr[2] == "two")

    ; Config flags
    Check("NullsAsStrings default", YAML.NullsAsStrings == 1)
    Check("BoolsAsInts default",    YAML.BoolsAsInts == 1)

    YAML.NullsAsStrings := false
    Check("Null sentinel",    YAML.Parse("null") == YAML.Null)
    YAML.NullsAsStrings := true

    YAML.BoolsAsInts := false
    Check("True sentinel",    YAML.Parse("true") == YAML.True)
    Check("False sentinel",   YAML.Parse("false") == YAML.False)
    YAML.BoolsAsInts := true

    ; Dump
    y := YAML.Dump(Map("name", "alice", "age", 30))
    Check("Dump name key",    InStr(y, "name:"))
    Check("Dump age plain",   InStr(y, "age: 30") && !InStr(y, 'age: "30"'))

    ; Round-trip
    original := Map("users", [Map("name", "alice", "age", 30), Map("name", "bob", "age", 25)])
    back := YAML.Parse(YAML.Dump(original))
    Check("RT users[1].name", back["users"][1]["name"] == "alice")
    Check("RT users[2].age",  back["users"][2]["age"] == 25)

    ; Multi-doc error
    thrown := false
    try YAML.Parse("---`nfoo`n---`nbar`n")
    catch YAMLMultiDocError
        thrown := true
    Check("MultiDocError",    thrown)

    ; Parse error carries line/column
    thrown := false
    try YAML.Parse("[1, 2,`n  `tbad indent`n")
    catch YAMLParseError as e {
        thrown := (e.Line > 0)
    }
    Check("ParseError line",  thrown)

} catch Error as e {
    Fail++
    FileAppend("EXCEPTION: " e.Message "`n" (e.HasProp("Extra") ? e.Extra "`n" : ""), "*")
}

FileAppend(Format("`n{} passed, {} failed`n", Pass, Fail), "*")
ExitApp (Fail ? 1 : 0)
