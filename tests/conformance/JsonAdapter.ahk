#Requires AutoHotkey v2.0

#Include <JSON>

#Include ../../dist/YAML.ahk

class JsonAdapter {
    static Load(jsonText) {
        prevNulls := JSON.NullsAsStrings
        prevBools := JSON.BoolsAsInts
        JSON.NullsAsStrings := false
        JSON.BoolsAsInts := false
        try {
            parsed := JSON.Load(jsonText)
        } finally {
            JSON.NullsAsStrings := prevNulls
            JSON.BoolsAsInts := prevBools
        }
        return this._Translate(parsed)
    }

    /**
     * Load a sequence of concatenated top-level JSON values (NDJSON-style).
     * The yaml-test-suite uses this format for `in.json` of multi-document
     * fixtures: one JSON value per YAML document, separated by whitespace.
     * Returns an Array of trees (one per value).
     */
    static LoadAll(jsonText) {
        out := []
        for slice in this._SplitConcatenated(jsonText)
            out.Push(this.Load(slice))
        return out
    }

    /**
     * Split a string of concatenated top-level JSON values into an Array of
     * slice strings. Walks the input value-by-value: at each position we're
     * either between values (skipping whitespace) or consuming exactly one
     * value (container, string, or scalar literal).
     */
    static _SplitConcatenated(text) {
        out := []
        n := StrLen(text)
        i := 1

        while i <= n {
            c := SubStr(text, i, 1)
            if c == " " || c == "`t" || c == "`r" || c == "`n" {
                i++
                continue
            }

            start := i
            if c == "[" || c == "{" {
                depth := 1
                i++
                while i <= n && depth > 0 {
                    ch := SubStr(text, i, 1)
                    if ch == '"' {
                        i := this._SkipString(text, i, n)
                        continue
                    }
                    if ch == "[" || ch == "{"
                        depth++
                    else if ch == "]" || ch == "}"
                        depth--
                    i++
                }
            } else if c == '"' {
                i := this._SkipString(text, i, n)
            } else {
                ; Top-level scalar literal (number, true, false, null) -
                ; read until next whitespace.
                while i <= n {
                    ch := SubStr(text, i, 1)
                    if ch == " " || ch == "`t" || ch == "`r" || ch == "`n"
                        break
                    i++
                }
            }

            out.Push(SubStr(text, start, i - start))
        }
        return out
    }

    /**
     * Advance past a JSON string starting at `i` (which must point at the
     * opening `"`). Returns the index just past the closing `"`.
     */
    static _SkipString(text, i, n) {
        i++
        while i <= n {
            c := SubStr(text, i, 1)
            if c == "\\" {
                i += 2
                continue
            }
            if c == '"' {
                return i + 1
            }
            i++
        }
        return i
    }

    /**
     * Convert cJSON sentinels into YAML sentinels for comparisons
     */
    static _Translate(v) {
        if v == JSON.Null
            return YAML.Null
        if v == JSON.True
            return YAML.True
        if v == JSON.False
            return YAML.False
        if v is Map {
            out := Map()
            for k, val in v
                out[k] := this._Translate(val)
            return out
        }
        if v is Array {
            out := Array()
            for val in v
                out.Push(this._Translate(val))
            return out
        }
        return v
    }
}
