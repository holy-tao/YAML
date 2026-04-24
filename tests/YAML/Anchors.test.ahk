#Requires AutoHotkey v2.0

#Include ../../dist/YAML.ahk
#Include ../YUnit/Assert.ahk

class AnchorsTest {
    class Parsing {
        TestScalarAlias() {
            result := YAML.Parse("a: &x hello`nb: *x`n")
            Assert.Equals(result["a"], "hello")
            Assert.Equals(result["b"], "hello")
        }

        TestMapAlias() {
            result := YAML.Parse("a: &x {k: 1}`nb: *x`n")
            Assert.Equals(ObjPtr(result["a"]), ObjPtr(result["b"]))
            result["a"]["k"] := 99
            Assert.Equals(result["b"]["k"], 99)
        }

        TestSeqAlias() {
            result := YAML.Parse("a: &x [1, 2, 3]`nb: *x`n")
            Assert.Equals(ObjPtr(result["a"]), ObjPtr(result["b"]))
            result["a"].Push(4)
            Assert.Equals(result["b"].Length, 4)
        }

        TestRecursiveAnchor() {
            result := YAML.Parse("&a [*a]`n")
            Assert.IsType(result, Array)
            Assert.Equals(ObjPtr(result[1]), ObjPtr(result))
        }

        TestUndefinedAlias() {
            err := Assert.Throws(() => YAML.Parse("a: *missing`n"), YAMLParseError)
        }

        TestAnchorRedefinition() {
            result := YAML.Parse("a: &x 1`nb: &x 2`nc: *x`n")
            Assert.Equals(result["c"], 2)
        }

        TestAliasAsDocumentRoot() {
            result := YAML.Parse("&root {k: v, self: *root}`n")
            Assert.Equals(ObjPtr(result["self"]), ObjPtr(result))
        }
    }

    class Dumping {
        TestPlainMapNoAnchor() {
            out := YAML.Dump(Map("a", 1, "b", 2))
            Assert.NotInStr(out, "&")
            Assert.NotInStr(out, "*")
        }

        TestSharedMapEmitsAnchor() {
            m := Map("x", 1)
            root := Map("a", m, "b", m)
            out := YAML.Dump(root)
            Assert.InStr(out, "&a1")
            Assert.InStr(out, "*a1")
        }

        TestSharedArrayEmitsAnchor() {
            a := [1, 2, 3]
            root := Map("first", a, "second", a)
            out := YAML.Dump(root)
            Assert.InStr(out, "&a1")
            Assert.InStr(out, "*a1")
        }

        TestRoundTripSharedRef() {
            m := Map("x", 1)
            root := Map("a", m, "b", m)
            back := YAML.Parse(YAML.Dump(root))
            Assert.Equals(ObjPtr(back["a"]), ObjPtr(back["b"]))
            back["a"]["x"] := 99
            Assert.Equals(back["b"]["x"], 99)
        }

        TestDumpSelfReferentialMap() {
            m := Map()
            m["self"] := m
            out := YAML.Dump(m)
            Assert.InStr(out, "&a1")
            Assert.InStr(out, "*a1")
        }

        TestDumpSelfReferentialArray() {
            a := []
            a.Push(a)
            out := YAML.Dump(a)
            Assert.InStr(out, "&a1")
            Assert.InStr(out, "*a1")
        }

        TestRoundTripCycle() {
            m := Map("k", "v")
            m["self"] := m
            back := YAML.Parse(YAML.Dump(m))
            Assert.Equals(back["k"], "v")
            Assert.Equals(ObjPtr(back["self"]), ObjPtr(back))
        }

        TestDistinctMapsNoAnchor() {
            ; Two maps with identical contents but distinct identity must
            ; not be collapsed into an alias.
            root := Map("a", Map("x", 1), "b", Map("x", 1))
            out := YAML.Dump(root)
            Assert.NotInStr(out, "&")
            Assert.NotInStr(out, "*")
        }

        TestMultipleAnchors() {
            m1 := Map("x", 1)
            m2 := Map("y", 2)
            root := Map("a", m1, "b", m2, "c", m1, "d", m2)
            out := YAML.Dump(root)

            Assert.InStr(out, "a: &a1")
            Assert.InStr(out, "c: *a1")
            Assert.InStr(out, "b: &a2")
            Assert.InStr(out, "d: *a2")
        }
    }
}
