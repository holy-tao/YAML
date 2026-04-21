#Requires AutoHotkey v2.0

#Include ../dist/YAML.ahk
#Include ./conformance/JsonAdapter.ahk
#Include ./conformance/TreeCompare.ahk
#Include ./conformance/Harness.ahk

suiteDir   := A_ScriptDir "\yaml-test-suite"
reportPath := A_ScriptDir "\conformance\report.txt"

if !DirExist(suiteDir) {
    FileAppend("yaml-test-suite submodule not initialized at " suiteDir "`r`n"
             . "Run: git submodule update --init tests/yaml-test-suite`r`n", "*", "UTF-8")
    ExitApp(2)
}

ConformanceHarness(suiteDir, reportPath).Run()
