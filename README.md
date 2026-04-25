# YAML for AutoHotkey

A YAML 1.1 loader and emitter for AutoHotkey v2, powered by [libyaml](https://pyyaml.org/wiki/LibYAML) embedded via [MCL](https://github.com/G33kDude/MCL.ahk). Inspired by (read: shamelessly looking over the shoulder of) [`cJSON`](https://github.com/G33kDude/cJson.ahk).

> [!NOTE] 
> ***This is a Work in Progress***
> 
> Core load/dump and multi-document support work; tag handling, non-string map keys, and full [yaml-test-suite] conformance are still in progress.

[yaml-test-suite]: https://github.com/yaml/yaml-test-suite

## Install

Grab a copy of `YAML.ahk` off the Releases page, drop it into your project or a [library folder], and include it:

```ahk
#Requires AutoHotkey v2.0
#Include <YAML>
```

The single file includes both 32-bit and 64-bit machine-code blobs and selects the right one at load time.

[library folder]: https://www.autohotkey.com/docs/v2/Scripts.htm#lib

## Quick start

```ahk
#Include <YAML>

config := YAML.Parse('
(
name: my-app
version: 1.2.3
features:
  - logging
  - metrics
limits:
  timeout: 30
  retries: 3
)')

MsgBox(config["name"])                ; my-app
MsgBox(config["features"][1])         ; logging
MsgBox(config["limits"]["timeout"])   ; 30

; Round-trip
text := YAML.Dump(config, pretty := true)
FileAppend(text, "config.yml")
```

## API

| Method | Purpose |
| --- | --- |
| `YAML.Parse(str)` | Parse a single-document YAML string. Throws `YAMLMultiDocError` if the stream has more than one document. |
| `YAML.ParseAll(str)` | Parse a multi-document stream into an `Array` of values. |
| `YAML.ParseFile(path, options?)` | Convenience: [`FileRead`][FileRead] + `Parse`. |
| `YAML.Dump(val, pretty := 0)` | Serialize a value to a YAML string. |
| `YAML.DumpAll(docs, pretty := 0)` | Serialize an `Array` of values as a multi-document stream. |
| `YAML.DumpFile(val, path, pretty := 0, encoding?)` | Convenience: `Dump` + write to file. |

[FileRead]: https://www.autohotkey.com/docs/v2/lib/FileRead.htm

### Type Mappings

For the most part, YAML primitives map cleanly to AutoHotkey primitives and vice versa. YAML mappings map to `Map` objects, YAML arrays map to `Array` objects. Like [`cJSON`](https://github.com/G33kDude/cJson.ahk),  `YAML` can also parse boolean and null values into sentinel objects (see [options](#options)), otherwise they are Integers and empty strings respectively.

| YAML | AHK |
| --- | --- |
| mapping | `Map` |
| sequence | `Array` |
| string | `String` |
| int / float | numeric primitive |
| `true` / `false` | `1` / `0` (or `YAML.True` / `YAML.False`) |
| `null` / `~` / empty | `''` (or `YAML.Null`) |

[Anchors and aliases] (`&name` / `*name`) are resolved to shared references - mutating through one alias is visible through the others. Recursive anchors produce self-referential containers. When dumping, `YAML` will detect references to objects in the graph and dump them as anchors, preserving the graph. This behavior is not currently configurable.

[anchors and aliases]: https://yaml.org/spec/1.2.2/#3222-anchors-and-aliases

### Options

All flags are static properties on `YAML`:

| Property | Default | Effect |
| --- | --- | --- |
| `YAML.NullsAsStrings` | `true` | When false, `null` decodes to `YAML.Null` instead of `''`. |
| `YAML.BoolsAsInts` | `true` | When false, booleans decode to `YAML.True` / `YAML.False` sentinels. |
| `YAML.ResolveMergeKeys` | `true` | When false, the YAML 1.1 [merge key] (`<<`) is kept as a literal key instead of splicing. |
| `YAML.ImplicitBools` | `true` | When false, only YAML 1.2 booleans (`true`/`false` and case variants) parse as bools; `yes`/`no`/`on`/`off` stay strings. |

[merge key]: https://yaml.org/type/merge.html

## License

MIT. See [`LICENSE`](LICENSE). Vendored [libyaml](src/native/libyaml/License) is also MIT-licensed.
