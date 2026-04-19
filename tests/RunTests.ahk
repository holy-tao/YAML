#Requires AutoHotkey v2.0

#Include ./YUnit/YUnit.ahk
#Include ./YUnit/ResultCounter.ahk
#Include ./YUnit/JUnit.ahk
#Include ./YUnit/Stdout.ahk

YUnit.Use(YunitResultCounter, YUnitJUnit, YUnitStdOut).Test(
	; Add test classes here
)

Exit(-YunitResultCounter.failures)