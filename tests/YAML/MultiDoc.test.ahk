#Requires AutoHotkey v2.0

#Include ../YUnit/Assert.ahk
#Include ../../dist/YAML.ahk

class MultiDocTest {

    class Parsing {
        TestParseAllSingleDoc() {
            result := YAML.ParseAll("foo: 1`n")
            Assert.IsType(result, Array)
            Assert.Equals(result.Length, 1)
            Assert.Equals(result[1]["foo"], 1)
        }

        TestParseAllTwoScalarDocs() {
            result := YAML.ParseAll("---`nfoo`n---`nbar`n")
            Assert.IsType(result, Array)
            Assert.Equals(result.Length, 2)
            Assert.Equals(result[1], "foo")
            Assert.Equals(result[2], "bar")
        }

        TestParseAllEmptyStream() {
            result := YAML.ParseAll("")
            Assert.IsType(result, Array)
            Assert.Equals(result.Length, 0)
        }

        TestParseAllMixedDocs() {
            result := YAML.ParseAll("---`n42`n---`nkey: value`n---`n- a`n- b`n")
            Assert.Equals(result.Length, 3)
            Assert.Equals(result[1], 42)
            Assert.Equals(result[2]["key"], "value")
            Assert.Equals(result[3][1], "a")
            Assert.Equals(result[3][2], "b")
        }

        TestParseAllSurfacesParseError() {
            Assert.Throws(() => YAML.ParseAll("---`nok`n---`n[1, 2,`n  `tbad indent`n"), YAMLParseError)
        }

        TestParseAllAnchorsAreDocLocal() {
            Assert.Throws(() => YAML.ParseAll("---`n&a foo`n---`n*a`n"), YAMLParseError)
        }

        TestParseStillThrowsOnMultiDoc() {
            Assert.Throws(() => YAML.Parse("---`nfoo`n---`nbar`n"), YAMLMultiDocError)
        }

        TestDumpAllSingleDoc() {
            out := YAML.DumpAll(["hello"])
            Assert.Equals(YAML.ParseAll(out).Length, 1)
            Assert.Equals(YAML.ParseAll(out)[1], "hello")
        }
    }

    class Dumping {
        TestDumpAllRoundTripScalars() {
            docs := ["foo", 42, 3.14]
            back := YAML.ParseAll(YAML.DumpAll(docs))
            Assert.Equals(back.Length, 3)
            Assert.Equals(back[1], "foo")
            Assert.Equals(back[2], 42)
            Assert.Equals(back[3], 3.14)
        }

        TestDumpAllRoundTripContainers() {
            docs := [Map("a", 1), [1, 2, 3], Map("nested", Map("x", "y"))]
            back := YAML.ParseAll(YAML.DumpAll(docs))
            Assert.Equals(back.Length, 3)
            Assert.MapsEqual(back[1], docs[1])
            Assert.ArraysEqual(back[2], docs[2])
            Assert.MapsEqual(back[3], docs[3])
        }

        TestDumpAllEmpty() {
            out := YAML.DumpAll([])
            Assert.Equals(YAML.ParseAll(out).Length, 0)
        }

        TestDumpAllRejectsNonArray() {
            Assert.Throws(() => YAML.DumpAll(Map("a", 1)), TypeError)
        }
    }
}
