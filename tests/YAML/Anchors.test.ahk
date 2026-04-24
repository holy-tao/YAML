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
}
