#Requires AutoHotkey v2.0

#Include ./YUnit/YUnit.ahk
#Include ./YUnit/Assert.ahk
#Include ./YUnit/ResultCounter.ahk
#Include ./YUnit/JUnit.ahk
#Include ./YUnit/Stdout.ahk

#Include ../dist/YAML.ahk

#Include ./YAML/Scalars.test.ahk
#Include ./YAML/Collections.test.ahk
#Include ./YAML/Dump.test.ahk
#Include ./YAML/RoundTrip.test.ahk
#Include ./YAML/Errors.test.ahk
#Include ./YAML/Config.test.ahk

YUnit.Use(YunitResultCounter, YUnitJUnit, YUnitStdOut).Test(
	ScalarsTest,
	CollectionsTest,
	DumpTest,
	RoundTripTest,
	ErrorsTest,
	ConfigTest,
)

Exit(-YunitResultCounter.failures)
