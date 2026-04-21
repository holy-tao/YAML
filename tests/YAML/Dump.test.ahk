#Requires AutoHotkey v2.0

class DumpTest {
    TestDumpInt() => Assert.InStr(YAML.Dump(42), "42")
    TestDumpString() => Assert.InStr(YAML.Dump("hello"), "hello")

    TestDumpMapKey() {
        y := YAML.Dump(Map("name", "alice"))
        Assert.InStr(y, "name:")
        Assert.InStr(y, "alice")
    }

    TestDumpArray() {
        y := YAML.Dump([1, 2, 3])
        Assert.InStr(y, "1")
        Assert.InStr(y, "2")
        Assert.InStr(y, "3")
    }

    TestNumericStringQuoted() {
        ; A string "42" must not round-trip as int 42.
        y := YAML.Dump(Map("x", "42"))
        Assert.InStr(y, '"42"')
    }

    TestPlainIntNotQuoted() {
        y := YAML.Dump(Map("age", 30))
        Assert.InStr(y, "age: 30")
        Assert.NotInStr(y, 'age: "30"')
    }

    TestBoolLikeStringQuoted() {
        ; "true" as string must be quoted to distinguish from bool.
        y := YAML.Dump(Map("v", "true"))
        Assert.InStr(y, '"true"')
    }

    TestNullSentinel() {
        y := YAML.Dump(Map("x", YAML.Null))
        Assert.InStr(y, "x:")
    }

    TestTrueSentinel() {
        y := YAML.Dump(Map("x", YAML.True))
        Assert.InStr(y, "true")
    }

    TestFalseSentinel() {
        y := YAML.Dump(Map("x", YAML.False))
        Assert.InStr(y, "false")
    }
}
