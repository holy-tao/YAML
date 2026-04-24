#Requires AutoHotkey v2.0

#Include ../../dist/YAML.ahk
#Include ../YUnit/Assert.ahk

class MergeKeysTest {
    class Parsing {
        TestBasicAliasMerge() {
            result := YAML.Parse("
            (
            base: &b {a: 1, b: 2}
            derived:
              <<: *b
              c: 3
            )")
            Assert.Equals(result["derived"]["a"], 1)
            Assert.Equals(result["derived"]["b"], 2)
            Assert.Equals(result["derived"]["c"], 3)
            Assert.Falsy(result["derived"].Has("<<"))
        }

        TestExplicitOverridesMerged() {
            result := YAML.Parse("
            (
            base: &b {a: 1, b: 2}
            derived:
              <<: *b
              a: 99
            )")
            Assert.Equals(result["derived"]["a"], 99)
            Assert.Equals(result["derived"]["b"], 2)
        }

        TestExplicitOverridesRegardlessOfOrder() {
            ; Explicit key appearing BEFORE the merge must still win.
            result := YAML.Parse("
            (
            base: &b {x: 1}
            derived:
              x: 99
              <<: *b
            )")
            Assert.Equals(result["derived"]["x"], 99)
        }

        TestMergeSequenceOrdering() {
            src := "
            (
            a: &a {k: from_a}
            b: &b {k: from_b}
            merged:
              <<: [*a, *b]
            )"
            result := YAML.Parse(src)
            Assert.Equals(result["merged"]["k"], "from_a")
        }

        TestMergeSequenceCombines() {
            src := "
            (
            a: &a {x: 1}
            b: &b {z: 2}
            merged:
              <<: [*a, *b]
              w: 3
            )"
            result := YAML.Parse(src)
            Assert.Equals(result["merged"]["x"], 1)
            Assert.Equals(result["merged"]["z"], 2)
            Assert.Equals(result["merged"]["w"], 3)
        }

        TestInlineMapAsMergeValue() {
            result := YAML.Parse("
            (
            derived:
              <<: {k: v, num: 1}
              extra: hello
            )")
            Assert.Equals(result["derived"]["k"], "v")
            Assert.Equals(result["derived"]["num"], 1)
            Assert.Equals(result["derived"]["extra"], "hello")
        }

        TestMergeValueMustBeMap() {
            err := Assert.Throws(() => YAML.Parse("derived:`n  <<: 5`n"), YAMLParseError)
            Assert.Equals(err.Message, "merge value must be a mapping or sequence of mappings")
        }

        TestMergeSequenceRejectsScalar() {
            src := "
            (
            a: &a {x: 1}
            derived:
              <<: [*a, 3]
            )"

            err := Assert.Throws(() => YAML.Parse(src), YAMLParseError)
            Assert.Equals(err.Message, "merge sequence items must all be mappings")
        }

        TestMergedContainerIdentityIndependent() {
            ; Merging copies key->value bindings but the merged map must
            ; remain a distinct container from the merge source.
            result := YAML.Parse("
            (
            base: &b {x: 1}
            derived:
              <<: *b
            )")
            Assert.NotEquals(ObjPtr(result["derived"]), ObjPtr(result["base"]))
            result["derived"]["x"] := 99
            Assert.Equals(result["base"]["x"], 1)
        }

        TestFlagOffKeepsLiteralKey() {
            prev := YAML.ResolveMergeKeys
            try {
                YAML.ResolveMergeKeys := false
                result := YAML.Parse("
                (
                base: &b {x: 1}
                derived:
                  <<: *b
                )")
                Assert.Truthy(result["derived"].Has("<<"))
                Assert.Equals(result["derived"]["<<"]["x"], 1)
            } finally {
                YAML.ResolveMergeKeys := prev
            }
        }
    }
}
