#Requires AutoHotkey v2.0

class CollectionsTest {
    TestBlockMapSimple() {
        m := YAML.Parse("a: 1`nb: 2`nc: 3")
        Assert.IsType(m, Map)
        Assert.Equals(m["a"], 1)
        Assert.Equals(m["b"], 2)
        Assert.Equals(m["c"], 3)
    }

    TestBlockMapMixed() {
        m := YAML.Parse("name: alice`nage: 30`nactive: true")
        Assert.Equals(m["name"], "alice")
        Assert.Equals(m["age"], 30)
        Assert.Equals(m["active"], 1)
    }

    TestBlockSeq() {
        a := YAML.Parse("- 1`n- 2`n- 3")
        Assert.IsType(a, Array)
        Assert.Equals(a.Length, 3)
        Assert.Equals(a[1], 1)
        Assert.Equals(a[3], 3)
    }

    TestBlockSeqMixed() {
        a := YAML.Parse("- 1`n- two`n- 3.14")
        Assert.Equals(a[1], 1)
        Assert.Equals(a[2], "two")
        Assert.Equals(a[3], 3.14)
    }

    TestFlowSeq() {
        a := YAML.Parse("[1, 2, 3]")
        Assert.Equals(a.Length, 3)
        Assert.Equals(a[2], 2)
    }

    TestFlowMap() {
        m := YAML.Parse("{a: 1, b: 2}")
        Assert.Equals(m["a"], 1)
        Assert.Equals(m["b"], 2)
    }

    TestNestedMapOfSeq() {
        m := YAML.Parse("users:`n  - alice`n  - bob")
        Assert.IsType(m["users"], Array)
        Assert.Equals(m["users"][1], "alice")
        Assert.Equals(m["users"][2], "bob")
    }

    TestNestedSeqOfMap() {
        a := YAML.Parse("- name: alice`n  age: 30`n- name: bob`n  age: 25")
        Assert.Equals(a.Length, 2)
        Assert.Equals(a[1]["name"], "alice")
        Assert.Equals(a[2]["age"], 25)
    }

    TestDeepNested() {
        m := YAML.Parse("a:`n  b:`n    c: deep")
        Assert.Equals(m["a"]["b"]["c"], "deep")
    }

    TestEmptyMap() {
        m := YAML.Parse("{}")
        Assert.IsType(m, Map)
        Assert.Equals(m.Count, 0)
    }

    TestEmptySeq() {
        a := YAML.Parse("[]")
        Assert.IsType(a, Array)
        Assert.Equals(a.Length, 0)
    }
}
