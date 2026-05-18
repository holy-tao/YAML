#Requires AutoHotkey v2.0

#Include ../YUnit/Stopwatch.ahk

class Bench {
    static MIN_ITERS := 3
    static MAX_ITERS := 200
    static TARGET_SECONDS := 1.0
    static WARMUP_ITERS := 1

    /**
     * Run `fn` repeatedly with auto-scaled iteration count and return timing
     * stats. After a warmup probe, iteration count is chosen so total wall
     * time targets ~1s, clamped to [MIN_ITERS, MAX_ITERS].
     *
     * @param {Func} fn no-arg callable to time
     * @returns {Object} {mean, stddev, min, max, total, iters} - all seconds
     */
    static Time(fn) {
        watch := Stopwatch()

        ; NOt sure how necessary this actually is
        loop Bench.WARMUP_ITERS
            fn.Call()

        watch.Start()
        fn.Call()
        probe := watch.Stop()

        if (probe <= 0)
            probe := 1e-6

        iters := Integer(Bench.TARGET_SECONDS / probe)
        if (iters < Bench.MIN_ITERS)
            iters := Bench.MIN_ITERS
        else if (iters > Bench.MAX_ITERS)
            iters := Bench.MAX_ITERS

        samples := Array()
        samples.Capacity := iters
        sum := 0.0
        min := 1e308
        max := 0.0

        loop iters {
            watch.Start()
            fn.Call()
            time := watch.Stop()
            samples.Push(time)
            sum += time
            if (time < min)
                min := time
            if (time > max)
                max := time
        }

        mean := sum / iters
        sqsum := 0.0
        for t in samples {
            d := t - mean
            sqsum += d * d
        }
        stddev := iters > 1 ? Sqrt(sqsum / (iters - 1)) : 0.0

        return {mean: mean, stddev: stddev, min: min, max: max,
                total: sum, iters: iters}
    }

    /**
     * Format a single result row for both console and CSV.
     * @returns {Object} {console, csv}
     */
    static FormatRow(caseName, op, bytes, stats) {
        mb_per_s := stats.mean > 0 ? (bytes / stats.mean) / (1024 * 1024) : 0.0
        mean_ms := stats.mean * 1000
        stddev_ms := stats.stddev * 1000
        min_ms := stats.min * 1000
        max_ms := stats.max * 1000

        console := Format("{1:-44s} {2:-14s} {3:10s} B {4:4s}x  "
            . "{5:9s} ms  +/-{6:7s} ms  min {7:9s}  max {8:9s}  {9:7s} MB/s",
            caseName, op, String(bytes), String(stats.iters),
            Bench._Fmt(mean_ms, 3),
            Bench._Fmt(stddev_ms, 3),
            Bench._Fmt(min_ms, 3),
            Bench._Fmt(max_ms, 3),
            Bench._Fmt(mb_per_s, 2))

        csv := Format("{1},{2},{3},{4},{5},{6},{7},{8},{9}",
            Bench._CsvEscape(caseName), Bench._CsvEscape(op), bytes, stats.iters,
            Bench._Fmt(mean_ms, 6), Bench._Fmt(stddev_ms, 6),
            Bench._Fmt(min_ms, 6), Bench._Fmt(max_ms, 6),
            Bench._Fmt(mb_per_s, 4))

        return {console: console, csv: csv}
    }

    static CsvHeader() => "case,op,bytes,iters,mean_ms,stddev_ms,min_ms,max_ms,mb_per_s"

    static _Fmt(num, places) => Format("{1:." places "f}", num)

    static _CsvEscape(s) {
        if InStr(s, ",") || InStr(s, "`"") || InStr(s, "`n") {
            s := StrReplace(s, "`"", "`"`"")
            return "`"" s "`""
        }
        return s
    }
}
