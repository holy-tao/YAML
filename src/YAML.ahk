#Requires AutoHotkey v2.0

/**
 * Base class for all YAML-related errors.
 */
class YAMLError extends Error {
}

/**
 * Thrown when libyaml rejects the input. Carries the 1-based line/column
 * reported by the parser.
 */
class YAMLParseError extends YAMLError {
    __New(message, what, extra, line := 0, column := 0) {
        super.__New(message, what, extra)
        this.Line := line
        this.Column := column
    }
}

/**
 * Thrown by YAML.Parse when the stream contains more than one document.
 * Callers wanting multi-doc input should use YAML.ParseAll.
 */
class YAMLMultiDocError extends YAMLError {
}

/**
 * Provides read and dump functionality for YAML files, powered by {@link https://pyyaml.org/wiki/LibYAML LibYAML}.
 */
class YAML {
    static version := "0.1.0-dev"

    /**
     * When true (default), null values in the YAML decode as ''.
     * When false, they decode as references to YAML.Null.
     */
    static NullsAsStrings {
        get => this.lib.bNullsAsStrings
        set => this.lib.bNullsAsStrings := value
    }

    /**
     * When true (default), booleans in the YAML decode as 1/0.
     * When false, they decode as YAML.True / YAML.False.
     */
    static BoolsAsInts {
        get => this.lib.bBoolsAsInts
        set => this.lib.bBoolsAsInts := value
    }

    /**
     * When true (default), the YAML 1.1 merge key (`<<`) splices the
     * referenced mapping's entries into the current map at load time.
     * When false, `<<` is kept as a literal key with its raw value.
     */
    static ResolveMergeKeys {
        get => this.lib.bResolveMergeKeys
        set => this.lib.bResolveMergeKeys := value
    }

    /**
     * Sentinel for YAML null. Returned from Parse when NullsAsStrings=false.
     */
    static Null {
        get {
            static _ := {value: '', name: 'null'}
            return _
        }
    }

    /**
     * Sentinel for YAML true. Returned from Parse when BoolsAsInts=false.
     */
    static True {
        get {
            static _ := {value: true, name: 'true'}
            return _
        }
    }

    /**
     * Sentinel for YAML false. Returned from Parse when BoolsAsInts=false.
     */
    static False {
        get {
            static _ := {value: false, name: 'false'}
            return _
        }
    }

    static __New() {
        this.lib := this._LoadLib()

        this.lib.fnGetMap   := ObjPtr(Map)
        this.lib.fnGetArray := ObjPtr(Array)
        this.lib.objNull    := ObjPtr(this.Null)
        this.lib.objTrue    := ObjPtr(this.True)
        this.lib.objFalse   := ObjPtr(this.False)
    }

    /**
     * Internal: returns the compiled native library object.
     */
    static _LoadLib() => _YAMLMCode()

    static ParseFile(path, options?) => this.Parse(FileRead(path, options?))
    static DumpFile(val, path, pretty := 0, encoding?)
        => FileOpen(path, "w", encoding?).Write(this.Dump(val, pretty))

    /**
     * Parse a YAML string into an AHK value. Single-document only; multi-document streams throw 
     * {@link YAMLMultiDocError `YAMLMultiDocError`}.
     *
     * {@link https://yaml.org/spec/1.1/#id863390 Anchors} (`&name`) and aliases (`*name`) are resolved to shared
     * references: every alias site points at the same AHK Map/Array, so mutating through one alias is visible through
     * the others. Recursive anchors (e.g. `&a [*a]`) produce self-referential containers.
     *
     * @param {String} yaml the yaml to parse
     * @returns {Map | Array | Primitive} the parsed value
     */
    static Parse(yaml) {
        utf8 := Buffer(StrPut(yaml, "UTF-8"), 0)
        n := StrPut(yaml, utf8, "UTF-8") - 1

        out := Buffer(24, 0)
        r := this.lib.loads(utf8, n, out)
        if r {
            if r == -2
                throw YAMLMultiDocError("YAML stream contains multiple documents; use YAML.ParseAll")
            msg := StrGet(this.lib.get_err_message(), "UTF-8")
            extra := StrGet(this.lib.get_err_extra(), "UTF-8")
            throw YAMLParseError(msg, A_ThisFunc, extra, this.lib.g_err_line, this.lib.g_err_column)
        }

        result := ComValue(0x400C, out.Ptr)[]
        if IsObject(result)
            ObjRelease(ObjPtr(result))
        return result
    }

    /**
     * Parse a YAML string containing multiple documents into an array of AHK
     * values.
     * @param {String} yaml the yaml to parse
     * @returns {Array<Map | Array | Primitive>} the parsed value(s)
     */
    static LoadAll(yaml) {
        throw MethodError("Not implemented")
    }

    /**
     * Serialize an AHK value to a YAML string.
     *
     * Supported types:
     *   - Primitives: integer, float, string
     *   - Containers: Map, Array
     *   - Sentinels: YAML.Null, YAML.True, YAML.False
     */
    static Dump(val, pretty := 0) {
        varbuf := Buffer(24, 0)
        vref := ComValue(0x400C, varbuf.Ptr)
        vref[] := val

        pOutPtr := Buffer(A_PtrSize, 0)
        pOutSize := Buffer(8, 0)

        r := this.lib.dumps(varbuf, !!pretty, pOutPtr, pOutSize)

        vref[] := 0

        if r {
            msg := StrGet(this.lib.get_err_message(), "UTF-8")
            extra := StrGet(this.lib.get_err_extra(), "UTF-8")

            throw YAMLError("YAML dump failed: " msg, A_ThisFunc, extra)
        }

        bufPtr := NumGet(pOutPtr, "Ptr")
        bufSize := NumGet(pOutSize, "Int64")
        out := StrGet(bufPtr, bufSize, "UTF-8")
        this.lib.dump_free(bufPtr)
        return out
    }
}
