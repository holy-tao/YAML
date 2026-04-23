# Run all tests on 32 and 64-bit ahk

Write-Output "Compiling..."
AutoHotkey64.exe /ErrorStdOut=UTF-8 ./build/build.ahk 2>&1 | Write-Host

Write-Output "32-Bit tests:"
AutoHotkey32.exe /ErrorStdOut=UTF-8 ./tests/RunTests.ahk 2>&1 | Write-Output

Write-Output "64-bit tests:"
AutoHotkey64.exe /ErrorStdOut=UTF-8 ./tests/RunTests.ahk 2>&1 | Write-Output

Write-Output "Conformance tests:"
AutoHotkey64.exe /ErrorStdOut=UTF-8 ./tests/RunConformance.ahk 2>&1 | Write-Output