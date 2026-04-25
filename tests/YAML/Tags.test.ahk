#Requires AutoHotkey v2.0

#Include ../YUnit/Assert.ahk
#Include ../../dist/YAML.ahk

; --- Custom-class fixtures (must be top-level so YAML._ObjFromYAML can resolve
; them via dynamic %name% lookup against the global scope) ---

;@ahkunit-ignore
class TagFixturePerson {
    __New(name := "", age := 0) {
        this.Name := name
        this.Age  := age
    }
    ToYAML() => Map("name", this.Name, "age", this.Age)
    static FromYAML(m) => TagFixturePerson(m["name"], m["age"])
}

;@ahkunit-ignore
class TagFixturePoint {
    __New(x := 0, y := 0) {
        this.X := x
        this.Y := y
    }
    ToYAML() => [this.X, this.Y]
    static FromYAML(arr) => TagFixturePoint(arr[1], arr[2])
}

;@ahkunit-ignore
class TagFixtureEmail {
    __New(addr := "") {
        this.Addr := addr
    }
    ToYAML() => this.Addr
    static FromYAML(s) => TagFixtureEmail(s)
}

;@ahkunit-ignore
class TagFixtureOuter {
    class Inner {
        __New(v := 0) => this.V := v
        ToYAML() => Map("v", this.V)
        static FromYAML(m) => TagFixtureOuter.Inner(m["v"])
    }
}

;@ahkunit-ignore
class TagFixtureBoom {
    ToYAML() {
        throw Error("intentional ToYAML failure")
    }
    static FromYAML(m) => TagFixtureBoom()
}

;@ahkunit-ignore
class TagFixtureBoomOnLoad {
    ToYAML() => Map("k", "v")
    static FromYAML(m) {
        throw Error("intentional FromYAML failure")
    }
}

class TagsTest {
    class RoundTrip {
        TestMapShaped() {
            p := TagFixturePerson("alice", 30)
            src := YAML.Dump(p)
            FileAppend(src, "*")

            Assert.Truthy(InStr(src, "tag:github.com,2026:holy-tao/yaml/ahk/object/TagFixturePerson"))
            back := YAML.Parse(src)
            refCount := ObjAddRef(ObjPtr(back)) - 1
            ObjRelease(ObjPtr(back))

            Assert.Equals(refCount, 1)

            Assert.IsType(back, TagFixturePerson)
            Assert.Equals(back.Name, "alice")
            Assert.Equals(back.Age, 30)
        }

        TestArrayShaped() {
            pt := TagFixturePoint(3, 4)
            src := YAML.Dump(pt)
            back := YAML.Parse(src)

            ; Check for C-side memory leaks
            refCount := ObjAddRef(ObjPtr(back)) - 1
            ObjRelease(ObjPtr(back))

            Assert.Equals(refCount, 1)
            Assert.IsType(back, TagFixturePoint)
            Assert.Equals(back.X, 3)
            Assert.Equals(back.Y, 4)
        }

        TestScalarShaped() {
            e := TagFixtureEmail("a@b.c")
            src := YAML.Dump(e)
            back := YAML.Parse(src)
            Assert.IsType(back, TagFixtureEmail)
            Assert.Equals(back.Addr, "a@b.c")
        }

        TestNestedClass() {
            v := TagFixtureOuter.Inner(99)
            src := YAML.Dump(v)
            Assert.Truthy(InStr(src, "ahk/object/TagFixtureOuter.Inner"))
            back := YAML.Parse(src)
            Assert.IsType(back, TagFixtureOuter.Inner)
            Assert.Equals(back.V, 99)
        }
    }

    class Strictness {
        TestUnknownTagStrict() {
            saved := YAML.StrictTags
            YAML.StrictTags := true
            try {
                src := "!<tag:github.com,2026:holy-tao/yaml/ahk/object/NoSuchClass>`nfoo: 1`n"
                e := Assert.Throws(() => YAML.Parse(src), YAMLParseError)
                Assert.Truthy(InStr(e.Extra, "NoSuchClass"))
            } finally {
                YAML.StrictTags := saved
            }
        }

        TestUnknownTagLenient() {
            saved := YAML.StrictTags
            YAML.StrictTags := false
            try {
                src := "!<tag:github.com,2026:holy-tao/yaml/ahk/object/NoSuchClass>`nfoo: 1`n"
                back := YAML.Parse(src)
                Assert.IsType(back, Map)
                Assert.Equals(back["foo"], 1)
            } finally {
                YAML.StrictTags := saved
            }
        }

        TestForeignTagIgnored() {
            ; Tag we don't know about (different prefix) should not error in
            ; strict mode either - it's not "ours" to resolve.
            saved := YAML.StrictTags
            YAML.StrictTags := true
            try {
                back := YAML.Parse("!<tag:example.com,1999:thing>`nfoo: 1`n")
                Assert.IsType(back, Map)
                Assert.Equals(back["foo"], 1)
            } finally {
                YAML.StrictTags := saved
            }
        }

        TestToYAMLThrows() {
            e := Assert.Throws(() => YAML.Dump(TagFixtureBoom()), YAMLError)
            Assert.InStr(e.Message, "intentional ToYAML failure")
        }

        ; FIXME: exit code 3221226356
        TestFromYAMLThrows() {
            saved := YAML.StrictTags
            YAML.StrictTags := true
            try {
                src := "
                (comments
                    ; Hand-crafted YAML so we don't depend on Dump path working.
                    !<tag:github.com,2026:holy-tao/yaml/ahk/object/TagFixtureBoomOnLoad>
                    key: val
                )"
                e := Assert.Throws(() => YAML.Parse(src), YAMLParseError)
                Assert.InStr(e.Extra, "TagFixtureBoomOnLoad")
            } finally {
                YAML.StrictTags := saved
            }
        }
    }

    class StandardTags {
        TestStrTagForcesString() {
            Assert.Equals(YAML.Parse("!!str 42"), "42")
        }

        TestStrFullURIForcesString() {
            Assert.Equals(YAML.Parse("!<tag:yaml.org,2002:str> 42"), "42")
        }

        TestIntTagCoerces() {
            Assert.Equals(YAML.Parse('!!int "3"'), 3)
        }

        TestNullTagAlwaysNull() {
            Assert.Equals(YAML.Parse("!!null anything"), "")
        }

        TestBoolTagYes() {
            Assert.Equals(YAML.Parse("!!bool yes"), 1)
        }

        TestFloatTagFromInt() {
            v := YAML.Parse("!!float 1")
            Assert.Truthy(IsFloat(v))
            Assert.Equals(v, 1.0)
        }

        TestIntTagMismatch() {
            e := Assert.Throws(() => YAML.Parse("!!int notanumber"), YAMLParseError)
            Assert.Truthy(InStr(e.Extra, "tag:yaml.org,2002:int"))
        }

        TestBoolTagMismatch() {
            e := Assert.Throws(() => YAML.Parse("!!bool maybe"), YAMLParseError)
            Assert.Truthy(InStr(e.Extra, "tag:yaml.org,2002:bool"))
        }
    }

    class AnchorsAndTags {
        TestSharedTaggedObjectIdentity() {
            ; A tagged map referenced by *alias should reconstruct to the same
            ; FromYAML-produced instance at every alias site.
            src := ""
              . "a: !<tag:github.com,2026:holy-tao/yaml/ahk/object/TagFixturePerson> &p`n"
              . "  name: alice`n"
              . "  age: 30`n"
              . "b: *p`n"
            back := YAML.Parse(src)
            Assert.IsType(back["a"], TagFixturePerson)
            Assert.Equals(ObjPtr(back["a"]), ObjPtr(back["b"]))
        }
    }
}
