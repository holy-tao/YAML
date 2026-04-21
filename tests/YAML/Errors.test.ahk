#Requires AutoHotkey v2.0

class ErrorsTest {
    TestMultiDocThrows() {
        Assert.Throws(() => YAML.Parse("---`nfoo`n---`nbar`n"), YAMLMultiDocError)
    }

    TestParseErrorType() {
        Assert.Throws(() => YAML.Parse("[1, 2,`n  `tbad indent`n"), YAMLParseError)
    }

    TestParseErrorCarriesLine() {
        e := Assert.Throws(() => YAML.Parse("[1, 2,`n  `tbad indent`n"), YAMLParseError)
        Assert.AtLeast(e.Line, 1)
    }

    TestParseErrorCarriesColumn() {
        e := Assert.Throws(() => YAML.Parse("[1, 2,`n  `tbad indent`n"), YAMLParseError)
        Assert.HasProp(e, "Column", Integer)
    }

    TestYAMLErrorBaseClass() {
        e := Assert.Throws(() => YAML.Parse("[1, 2,`n  `tbad indent`n"), YAMLError)
        Assert.IsType(e, YAMLError)
    }

    TestMultiDocIsYAMLError() {
        Assert.Throws(() => YAML.Parse("---`nfoo`n---`nbar`n"), YAMLError)
    }
}
