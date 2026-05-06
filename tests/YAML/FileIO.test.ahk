#Requires AutoHotkey v2.0

#Include ../../dist/YAML.ahk
#Include ../YUnit/Assert.ahk

class FileIOTest {
    static _tmp(suffix := ".yml") {
        path := A_Temp "\yaml_fileio_test_" Random(0, 0xFFFFFFFF) suffix
        return path
    }

    static _writeFile(path, contents, encoding := "UTF-8") {
        f := FileOpen(path, "w", encoding)
        f.Write(contents)
        f.Close()
    }

    class Reading {
        TestParseFilePath() {
            path := FileIOTest._tmp()
            FileIOTest._writeFile(path, "name: alice`nage: 30`n")
            try {
                got := YAML.ParseFile(path)
                Assert.MapsEqual(got, Map("name", "alice", "age", 30))
            } finally {
                FileDelete(path)
            }
        }

        TestParseFileFileObject() {
            path := FileIOTest._tmp()
            FileIOTest._writeFile(path, "[1, 2, 3]`n")
            f := FileOpen(path, "r")
            try {
                got := YAML.ParseFile(f)
                Assert.ArraysEqual(got, [1, 2, 3])
                ; The caller's File handle should still be valid after the call
                ; (DuplicateHandle should leave it untouched).
                Assert.AtLeast(f.Pos, 0)
            } finally {
                f.Close()
                FileDelete(path)
            }
        }

        TestParseFileHandle() {
            path := FileIOTest._tmp()
            FileIOTest._writeFile(path, "answer: 42`n")
            f := FileOpen(path, "r")
            try {
                got := YAML.ParseFile(f.Handle)
                Assert.Equals(got["answer"], 42)
            } finally {
                f.Close()
                FileDelete(path)
            }
        }

        TestParseFileMissing() {
            Assert.Throws(() => YAML.ParseFile(A_Temp "\does_not_exist_" Random(0, 0xFFFFFFFF) ".yml"), YAMLError)
        }

        TestParseFileMultiDocThrows() {
            path := FileIOTest._tmp()
            FileIOTest._writeFile(path, "---`nfoo`n---`nbar`n")
            try {
                Assert.Throws(() => YAML.ParseFile(path), YAMLMultiDocError)
            } finally {
                FileDelete(path)
            }
        }

        TestParseAllFile() {
            path := FileIOTest._tmp()
            FileIOTest._writeFile(path, "---`nfoo`n---`nbar`n")
            try {
                docs := YAML.ParseAllFile(path)
                Assert.IsType(docs, Array)
                Assert.Equals(docs.Length, 2)
                Assert.Equals(docs[1], "foo")
                Assert.Equals(docs[2], "bar")
            } finally {
                FileDelete(path)
            }
        }
    }

    class Writing {
        TestDumpFilePath() {
            path := FileIOTest._tmp()
            try {
                YAML.DumpFile(Map("name", "alice", "age", 30), path)
                got := YAML.Parse(FileRead(path, "UTF-8"))
                Assert.MapsEqual(got, Map("name", "alice", "age", 30))
            } finally {
                try FileDelete(path)
            }
        }

        TestDumpFileMatchesDumpString() {
            path := FileIOTest._tmp()
            value := Map("a", [1, 2, 3], "b", Map("nested", true))
            try {
                YAML.DumpFile(value, path)
                fileBytes := FileRead(path, "RAW")
                stringBytes := Buffer(StrPut(YAML.Dump(value), "UTF-8") - 1, 0)
                StrPut(YAML.Dump(value), stringBytes, "UTF-8")
                ; File output should be byte-identical to Dump+Write to UTF-8.
                Assert.Equals(fileBytes.Size, stringBytes.Size)
                loop fileBytes.Size
                    Assert.Equals(NumGet(fileBytes, A_Index - 1, "UChar"),
                                  NumGet(stringBytes, A_Index - 1, "UChar"))
            } finally {
                try FileDelete(path)
            }
        }

        TestDumpFileFileObjectStillUsable() {
            path := FileIOTest._tmp()
            f := FileOpen(path, "w")
            try {
                YAML.DumpFile([1, 2, 3], f)
                ; Caller's handle remains valid - we should be able to close it
                ; ourselves without issue (DuplicateHandle protects us from
                ; double-close).
                f.Close()
                got := YAML.ParseFile(path)
                Assert.ArraysEqual(got, [1, 2, 3])
            } finally {
                try FileDelete(path)
            }
        }

        TestDumpAllFile() {
            path := FileIOTest._tmp()
            try {
                YAML.DumpAllFile(["foo", "bar", Map("k", "v")], path)
                docs := YAML.ParseAllFile(path)
                Assert.Equals(docs.Length, 3)
                Assert.Equals(docs[1], "foo")
                Assert.Equals(docs[2], "bar")
                Assert.MapsEqual(docs[3], Map("k", "v"))
            } finally {
                try FileDelete(path)
            }
        }
    }

    class Errors {
        TestParseFileBadType() {
            Assert.Throws(() => YAML.ParseFile(Map()), TypeError)
        }

        TestDumpFileBadType() {
            Assert.Throws(() => YAML.DumpFile("x", Map()), TypeError)
        }

        TestParseFileObjectNoReadPermissions() {
            path := FileIOTest._tmp()
            FileIOTest._writeFile(path, "[1, 2, 3]`n")
            f := FileOpen(path, "w")

            Assert.Throws(() => YAML.ParseFile(f), YAMLParseError)
        }
    }
}
