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
    static version := "0.1.0"

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
     * When true (default), the parser will use the YAML 1.1 boolean
     * values, including string-like values like "yes" and "on". When
     * false, only YAML 1.2 booleans parsed as booleans ("true"/"True"/"TRUE"
     * and "false"/"False"/"FALSE") - values like "off" are parsed as
     * strings
     */
    static ImplicitBools {
        get => !this.lib.bStrictBools
        set => this.lib.bStrictBools := !value
    }

    /**
     * When true (default), an unresolved custom tag on parse - unknown class,
     * missing static FromYAML, or FromYAML throwing - raises YAMLParseError
     * with the offending tag in the .Extra field. When false, the tag is
     * silently dropped and the underlying Map / Array / scalar is returned.
     *
     * Standard `tag:yaml.org,2002:*` tags (`!!str`, `!!int`, etc.) are always
     * honored; this flag only governs the custom-class protocol below.
     */
    static StrictTags {
        get => this.lib.bStrictTags
        set => this.lib.bStrictTags := value
    }

    /**
     * URI prefix for tags emitted/recognized as custom AHK class instances.
     * The class's dotted name is appended (e.g. "MyClass" or "Outer.Inner").
     *
     * Format follows RFC 4151: `tag:<authority>,<date>:<specific>`.
     */
    static TagPrefix => "tag:github.com,2026:holy-tao/yaml/ahk/object/"

    /**
     * Shorthand handle registered as a `%TAG` directive at document start
     * when dumping objects whose tag URI begins with `TagPrefix`. Lets the
     * emitter write `!ahk!ClassName` instead of the full URI at every site.
     * Kept in sync with AHK_TAG_HANDLE in src/native/src/dump.c.
     */
    static TagHandle => "!ahk!"

    /**
     * Get the tag string for an object.
     * 
     * @param {Object} obj the object to get the tag for 
     * @returns {String} the tag.
     */
    static TagFor(obj) => Format("{1}{2}", this.TagPrefix, obj.__Class)

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

        ; Persistent cdecl callbacks for the C side to invoke when parsing
        ; custom tags or dumping classes that implement ToYAML. "F" runs in
        ; the current thread (the DllCall is already synchronous).
        this.lib.pObjFromYAML := CallbackCreate(this._ObjFromYAML.Bind(this), "C F", 3)
        this.lib.pObjToYAML   := CallbackCreate(this._ObjToYAML.Bind(this),   "C F", 3)
    }

    /**
     * Internal: returns the compiled native library object.
     */
    static _LoadLib() => _YAMLMCode()

    /**
     * Parse the contents of a file into an AHK value. See {@link YAML.Parse `YAML.Parse`}
     * 
     * @param {String} path Path to the file to parse 
     * @param {String} options Any `FileRead` options
     * @returns {Map | Array | Primitive} the parsed value
     */
    static ParseFile(path, options?) => this.Parse(FileRead(path, options?))

    /**
     * Serialize an AHK value into a YAML string and write it to `path`, overwriting it if it exists.
     * See {@link YAML.Dump `YAML.Dump`}
     * 
     * @param {Map | Array | Primitive} val The value to serialize
     * @param {String} path Path to the file to write the serialized value to
     * @param {Integer} pretty If true, pretty-print the YAML string
     * @param {String} encoding File encoding to use (e.g. "UTF-8", "CP0")
     * @returns {Integer} 
     */
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
            this._ThrowYamlParseError()
        }

        result := ComValue(0x400C, out.Ptr)[]
        if IsObject(result)
            ObjRelease(ObjPtr(result))
        return result
    }

    /**
     * Parse a YAML string containing one or more documents into an Array of
     * AHK values. Always returns an Array (length 0 for an empty stream,
     * length 1 for a single-document stream).
     *
     * Anchors and aliases are resolved per-document; an anchor defined in
     * one document is not visible to subsequent documents.
     *
     * @param {String} yaml the yaml to parse
     * @returns {Array<Map | Array | Primitive>} the parsed value(s)
     */
    static ParseAll(yaml) {
        utf8 := Buffer(StrPut(yaml, "UTF-8"), 0)
        n := StrPut(yaml, utf8, "UTF-8") - 1

        out := Buffer(24, 0)
        if this.lib.loads_all(utf8, n, out)
            this._ThrowYamlParseError()

        result := ComValue(0x400C, out.Ptr)[]
        if IsObject(result)
            ObjRelease(ObjPtr(result))
        return result
    }

    /**
     * Internal: invoked from C (parse path) when a non-standard tag is seen
     * on a scalar/map/sequence. Resolves the tag to an AHK class and calls
     * its static FromYAML.
     * 
     * @param {Integer} pTag pointer to a UTF-8 string containing the tag
     * @param {Integer} pIn pointer to the input value
     * @param {Integer} pOut output pointer in which to store the output VARIANT
     * @returns {Integer} Value indicating success or failure:
     *   0 - handled, *pOut populated with the FromYAML result
     *   1 - unhandled (foreign prefix, or non-strict failure); C continues to parse
     *   2 - strict-mode failure; err globals already populated
     */
    static _ObjFromYAML(pTag, pIn, pOut) {
        static OK := 0, EUNHANDLED := 1, ESTRICT := 2

        try {
            tag := StrGet(pTag, "UTF-8")

            prefixLen := StrLen(this.TagPrefix)
            if (StrLen(tag) <= prefixLen || SubStr(tag, 1, prefixLen) != this.TagPrefix)
                return EUNHANDLED

            dottedName := SubStr(tag, prefixLen + 1)

            cls := ""
            try {
                for part in StrSplit(dottedName, ".")
                    cls := cls is Class ? cls.%part% : %part%
            }
            catch Error as err {
                this._SetCallbackErr(Format("Could not resolve '{1}' to a class: {2}", dottedName, err.Message), tag)
                return this.lib.bStrictTags? ESTRICT : EUNHANDLED
            }

            if !(cls is Class) {
                this._SetCallbackErr(Format("'{1}' is not a Class", dottedName), tag)
                return this.lib.bStrictTags? ESTRICT : EUNHANDLED
            }
            else if !HasMethod(cls, "FromYAML", 1) {
                this._SetCallbackErr(Format("Class '{1}' has no compatible 'FromYAML' method", dottedName), tag)
                return this.lib.bStrictTags? ESTRICT : EUNHANDLED
            }

            ; Note unlike _ObjToYAML this is a borrow, the C lib owns the VARIANT and any IDispatch
            ; pointers it may hold.
            inValue := ComValue(0x400C, pIn)[]

            try {
                result := cls.FromYAML(inValue)
            }
            catch Error as err {
                this._SetCallbackErr(Format("{1}.FromYAML threw a(n) {2}: {3}", 
                    dottedName, Type(err), err.Message), err.Extra)
                return ESTRICT
            }

            ; Marshal result into the output VARIANT. Assignment AddRefs IDispatch
            ; via VariantCopy; AHK releases its own ref when `result` goes out of
            ; scope, leaving C with the single ref it expects to own.
            outRef := ComValue(0x400C, pOut)
            outRef[] := result
            return OK
        }
        catch Error as err {
            this._SetCallbackErr(Format("Unhandled {1}: {2}", Type(err), err.Message), err.Extra)
            return ESTRICT
        }
    }

    /**
     * Internal: invoked from C (dump path) when an object with a `ToYAML`
     * method is encountered. Calls obj.ToYAML(), writes the resolved tag URI
     * into the 256-byte buffer at pTagOut, marshals the replacement value
     * into the VARIANT at pReplOut.
     *
     * Returns 0 on success, 1 on failure (err globals populated).
     */
    static _ObjToYAML(pObj, pTagOut, pReplOut) {
        try {
            obj := ObjFromPtrAddRef(pObj)
            replacement := obj.ToYAML()

            tag := this.TagFor(obj)
            ; pTagOut points at a 256-byte char buffer; leave a null terminator.
            StrPut(tag, pTagOut, 255, "UTF-8")

            outRef := ComValue(0x400C, pReplOut)
            outRef[] := replacement
            return 0
        } 
        catch Error as err {
            tag := ""
            try tag := this.TagFor(obj)
            this._SetCallbackErr(Format("Unhandled {1}: {2}", Type(err), err.Message), tag)
            return 1
        }
    }

    /**
     * Internal: write the error message + extra back into the C-side error
     * globals from a CallbackCreate'd callback. Used on strict-tag failures
     * so YAMLParseError / YAMLError carry the offending tag in `.Extra`.
     */
    static _SetCallbackErr(msg, extra := "") {
        msgBuf := Buffer(StrPut(msg, "UTF-8"), 0)
        StrPut(msg, msgBuf, "UTF-8")
        extraBuf := Buffer(StrPut(extra, "UTF-8"), 0)
        StrPut(extra, extraBuf, "UTF-8")
        this.lib.set_err(msgBuf, extraBuf, 0, 0)
    }

    static _ThrowYamlParseError() {
        msg := StrGet(this.lib.get_err_message(), "UTF-8")
        extra := StrGet(this.lib.get_err_extra(), "UTF-8")

        throw YAMLParseError(msg, A_ThisFunc, extra, this.lib.g_err_line, this.lib.g_err_column)
    }

    /**
     * Serialize an AHK value to a YAML string.
     * 
     * @param {Map | Array | Primitive} val The value to serialize 
     * @param {Integer} pretty If true, pretty-print the string
     * @returns {String} the serialized value
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

    /**
     * Serialize an Array of AHK values to a multi-document YAML stream.
     * Each element of `docs` becomes one YAML document, separated by `---`.
     *
     * @param {Array<Map | Array | Primitive>} docs Array of values to emit (one document per element)
     * @param {Integer} pretty If true, pretty-print the string
     * @returns {String} the serialized value
     */
    static DumpAll(docs, pretty := 0) {
        if !(docs is Array)
            throw TypeError("Expected an Array but got a(n) " Type(docs), , docs)

        varbuf := Buffer(24, 0)
        vref := ComValue(0x400C, varbuf.Ptr)
        vref[] := docs

        pOutPtr := Buffer(A_PtrSize, 0)
        pOutSize := Buffer(8, 0)

        r := this.lib.dumps_all(varbuf, !!pretty, pOutPtr, pOutSize)

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
