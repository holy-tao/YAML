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
