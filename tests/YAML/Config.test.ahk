#Requires AutoHotkey v2.0

class ConfigTest {
    Begin() {
        this.prevNulls := YAML.NullsAsStrings
        this.prevBools := YAML.BoolsAsInts
    }

    End() {
        YAML.NullsAsStrings := this.prevNulls
        YAML.BoolsAsInts := this.prevBools
    }

    TestNullsAsStringsDefault() => Assert.Equals(YAML.NullsAsStrings, 1)
    TestBoolsAsIntsDefault()    => Assert.Equals(YAML.BoolsAsInts, 1)

    TestNullSentinelMode() {
        YAML.NullsAsStrings := false
        Assert.Equals(YAML.Parse("null"), YAML.Null)
    }

    TestNullStringMode() {
        YAML.NullsAsStrings := true
        Assert.Equals(YAML.Parse("null"), "")
    }

    TestTrueSentinelMode() {
        YAML.BoolsAsInts := false
        Assert.Equals(YAML.Parse("true"), YAML.True)
    }

    TestFalseSentinelMode() {
        YAML.BoolsAsInts := false
        Assert.Equals(YAML.Parse("false"), YAML.False)
    }

    TestBoolIntMode() {
        YAML.BoolsAsInts := true
        Assert.Equals(YAML.Parse("true"), 1)
        Assert.Equals(YAML.Parse("false"), 0)
    }

    TestSentinelIdentityNull() {
        a := YAML.Null
        b := YAML.Null
        Assert.Equals(a, b)
    }
}
