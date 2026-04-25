#Requires AutoHotkey v2.0

class ScalarsTest {
    TestInt()          => Assert.Equals(YAML.Parse("42"), 42)
    TestNegativeInt()  => Assert.Equals(YAML.Parse("-17"), -17)
    TestHex()          => Assert.Equals(YAML.Parse("0x2A"), 42)
    TestOctal()        => Assert.Equals(YAML.Parse("0o17"), 15)
    TestFloat()        => Assert.Equals(YAML.Parse("3.14"), 3.14)
    TestNegFloat()     => Assert.Equals(YAML.Parse("-2.5"), -2.5)
    TestScientific()   => Assert.Equals(YAML.Parse("1e3"), 1000.0)
    TestInfPos()       => Assert.Truthy(YAML.Parse(".inf") > 1e308)
    TestInfNeg()       => Assert.Truthy(YAML.Parse("-.inf") < -1e308)

    TestBareString()   => Assert.Equals(YAML.Parse("hello"), "hello")
    TestQuotedString() => Assert.Equals(YAML.Parse('"hello"'), "hello")
    TestSingleQuoted() => Assert.Equals(YAML.Parse("'hello'"), "hello")
    TestQuotedDigits() => Assert.Equals(YAML.Parse('"42"'), "42")

    TestNullDefault()    => Assert.Equals(YAML.Parse("null"), "")
    TestNullTilde()      => Assert.Equals(YAML.Parse("~"), "")
    TestNullEmpty()      => Assert.Equals(YAML.Parse(""), "")
    TestNullUppercase()  => Assert.Equals(YAML.Parse("Null"), "")
    TestNullAllCaps()    => Assert.Equals(YAML.Parse("NULL"), "")

    TestBoolTrueDefault() => Assert.Equals(YAML.Parse("true"), 1)
    TestBoolFalseDefault() => Assert.Equals(YAML.Parse("false"), 0)
    TestBoolYes()         => Assert.Equals(YAML.Parse("yes"), 1)
    TestBoolNo()          => Assert.Equals(YAML.Parse("no"), 0)
    TestBoolOn()          => Assert.Equals(YAML.Parse("on"), 1)
    TestBoolOff()         => Assert.Equals(YAML.Parse("off"), 0)
    TestBoolTrueCaps()    => Assert.Equals(YAML.Parse("True"), 1)

    TestBoolNoImplicit() {
        YAML.ImplicitBools := false
        
        try {
            Assert.Equals(YAML.Parse("yes"), "yes")
            Assert.Equals(YAML.Parse("on"), "on")
            Assert.Equals(YAML.Parse("off"), "off")

            Assert.Equals(YAML.Parse("true"), 1)
            Assert.Equals(YAML.Parse("False"), 0)
        }
        finally {
            YAML.ImplicitBools := true
        }
    }
}
