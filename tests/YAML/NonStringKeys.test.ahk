#Requires AutoHotkey v2.0

#Include ../../dist/YAML.ahk
#Include ../YUnit/Assert.ahk

class NonStringKeysTest {
    Begin() {
        this.prevNulls := YAML.NullsAsStrings
        this.prevBools := YAML.BoolsAsInts
    }

    End() {
        YAML.NullsAsStrings := this.prevNulls
        YAML.BoolsAsInts := this.prevBools
    }

    TestIntKey() {
        m := YAML.Parse("1: one`n2: two")
        Assert.IsType(m, Map)
        Assert.Equals(m[1], "one")
        Assert.Equals(m[2], "two")
    }

    TestFloatKey() {
        m := YAML.Parse("1.5: x")
        Assert.Equals(m[1.5], "x")
    }

    TestBoolKeySentinel() {
        YAML.BoolsAsInts := false
        m := YAML.Parse("true: yes-val`nfalse: no-val")
        Assert.Equals(m[YAML.True], "yes-val")
        Assert.Equals(m[YAML.False], "no-val")
    }

    TestBoolKeyAsInt() {
        YAML.BoolsAsInts := true
        m := YAML.Parse("true: yes-val`nfalse: no-val")
        Assert.Equals(m[1], "yes-val")
        Assert.Equals(m[0], "no-val")
    }

    TestNullKeySentinel() {
        YAML.NullsAsStrings := false
        m := YAML.Parse("~: nil-val")
        Assert.Equals(m[YAML.Null], "nil-val")
    }

    TestSequenceKey() {
        m := YAML.Parse("? [1, 2]`n: pair")
        Assert.IsType(m, Map)
        ; The complex key is the only entry; pull it out via __Enum.
        for k, v in m {
            Assert.IsType(k, Array)
            Assert.Equals(k[1], 1)
            Assert.Equals(k[2], 2)
            Assert.Equals(v, "pair")
        }
    }

    TestMapKey() {
        m := YAML.Parse("? {a: 1}`n: nested")
        for k, v in m {
            Assert.IsType(k, Map)
            Assert.Equals(k["a"], 1)
            Assert.Equals(v, "nested")
        }
    }

    TestMixedKeys() {
        m := YAML.Parse("foo: 1`n2: bar`n3.5: baz")
        Assert.Equals(m["foo"], 1)
        Assert.Equals(m[2], "bar")
        Assert.Equals(m[3.5], "baz")
    }

    TestRoundTripIntKey() {
        original := Map(1, "one", 2, "two")
        Assert.MapsEqual(YAML.Parse(YAML.Dump(original)), original)
    }

    TestRoundTripMixedKeys() {
        original := Map("a", 1, 2, "b", 3.5, "c")
        Assert.MapsEqual(YAML.Parse(YAML.Dump(original)), original)
    }

    TestMergeKeyStillSpecial() {
        ; Bare BSTR `<<` triggers merge; covered by MergeKeysTest already, but
        ; re-check after the VARIANT-key refactor.
        m := YAML.Parse("base: &b {alpha: 1, beta: 2}`nderived:`n  <<: *b`n  gamma: 3")
        Assert.Equals(m["derived"]["alpha"], 1)
        Assert.Equals(m["derived"]["beta"], 2)
        Assert.Equals(m["derived"]["gamma"], 3)
        Assert.Falsy(m["derived"].Has("<<"))
    }

    TestMergeCopiesIntKeys() {
        ; Merge source has int keys; they should propagate into the target.
        m := YAML.Parse("base: &b {1: one, 2: two}`nderived:`n  <<: *b`n  3: three")
        Assert.Equals(m["derived"][1], "one")
        Assert.Equals(m["derived"][2], "two")
        Assert.Equals(m["derived"][3], "three")
    }
}
