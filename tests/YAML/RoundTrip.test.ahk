#Requires AutoHotkey v2.0

class RoundTripTest {
    TestRTInt() => Assert.Equals(YAML.Parse(YAML.Dump(42)), 42)
    TestRTFloat() => Assert.Equals(YAML.Parse(YAML.Dump(3.14)), 3.14)
    TestRTString() => Assert.Equals(YAML.Parse(YAML.Dump("hello")), "hello")
    TestRTNumericString() => Assert.Equals(YAML.Parse(YAML.Dump("42")), "42")
    TestRTBoolString() => Assert.Equals(YAML.Parse(YAML.Dump("true")), "true")

    TestRTFlatMap() {
        original := Map("a", 1, "b", "two", "c", 3.14)
        Assert.MapsEqual(YAML.Parse(YAML.Dump(original)), original)
    }

    TestRTFlatArray() {
        original := [1, "two", 3.14]
        Assert.ArraysEqual(YAML.Parse(YAML.Dump(original)), original)
    }

    TestRTNestedMapOfArrays() {
        original := Map("users", [Map("name", "alice", "age", 30), Map("name", "bob", "age", 25)])
        Assert.MapsEqual(YAML.Parse(YAML.Dump(original)), original)
    }

    TestRTDeepNested() {
        original := Map("a", Map("b", Map("c", [1, 2, 3])))
        Assert.MapsEqual(YAML.Parse(YAML.Dump(original)), original)
    }

    TestRTEmptyContainers() {
        original := Map("empty_map", Map(), "empty_arr", [])
        back := YAML.Parse(YAML.Dump(original))
        Assert.IsType(back["empty_map"], Map)
        Assert.IsType(back["empty_arr"], Array)
    }
}
