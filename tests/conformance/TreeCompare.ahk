#Requires AutoHotkey v2.0

#Include ../../dist/YAML.ahk

/**
 * Structural comparison for YAML document trees
 */
class TreeCompare {
    static FloatTolerance := 1e-9

    static Equal(a, b) {
        if a == YAML.Null || b == YAML.Null
            return a == b
        if a == YAML.True || b == YAML.True
            return a == b
        if a == YAML.False || b == YAML.False
            return a == b

        if a is Map && b is Map {
            if a.Count != b.Count
                return false
            for k, va in a {
                if !b.Has(k)
                    return false
                if !this.Equal(va, b[k])
                    return false
            }
            return true
        }

        if a is Array && b is Array {
            if a.Length != b.Length
                return false
            i := 0
            for va in a {
                i++
                if !this.Equal(va, b[i])
                    return false
            }
            return true
        }

        if IsObject(a) || IsObject(b)
            return false

        if (a is Float || b is Float) && (a is Number && b is Number) {
            if a == b
                return true
            mag := Abs(a) > Abs(b) ? Abs(a) : Abs(b)
            return Abs(a - b) <= this.FloatTolerance * (mag > 1 ? mag : 1)
        }

        return a == b
    }
}
