#Requires AutoHotkey v2.0

/**
 * Returns an Array of {name, file} entries describing the corpus to bench.
 *
 * Files are resolved relative to corpusRoot. Missing files are silently
 * skipped (so the harness still runs if the corpus is partially fetched).
 */
class Cases {
    static Build(corpusRoot) {
        candidates := [
            ; Real-world configs
            {name: "k8s-swagger",         file: "k8s-swagger.yaml"},
            {name: "appveyor",            file: "rapidyaml/appveyor.yml"},
            {name: "travis",              file: "rapidyaml/travis.yml"},
            {name: "compile_commands",    file: "rapidyaml/compile_commands.json"},

            ; Shape stress: maps
            {name: "maps_blck_o1000_i10", file: "rapidyaml/style_maps_blck_outer1000_inner10.yml"},
            {name: "maps_flow_o1000_i10", file: "rapidyaml/style_maps_flow_outer1000_inner10.yml"},
            {name: "maps_json_o1000_i10", file: "rapidyaml/style_maps_json_outer1000_inner10.yml"},

            ; Shape stress: sequences
            {name: "seqs_blck_o1000_i10", file: "rapidyaml/style_seqs_blck_outer1000_inner10.yml"},
            {name: "seqs_flow_o1000_i10", file: "rapidyaml/style_seqs_flow_outer1000_inner10.yml"},
            {name: "seqs_json_o1000_i10", file: "rapidyaml/style_seqs_json_outer1000_inner10.yml"},

            ; Scalar stress
            {name: "scalar_block_folded",       file: "rapidyaml/scalar_block_folded.yml"},
            {name: "scalar_block_folded_ml",    file: "rapidyaml/scalar_block_folded_multiline.yml"},
            {name: "scalar_block_literal",      file: "rapidyaml/scalar_block_literal.yml"},
            {name: "scalar_block_literal_ml",   file: "rapidyaml/scalar_block_literal_multiline.yml"},
            {name: "scalar_dquoted",            file: "rapidyaml/scalar_dquoted.yml"},
            {name: "scalar_dquoted_ml",         file: "rapidyaml/scalar_dquoted_multiline.yml"},
            {name: "scalar_squoted",            file: "rapidyaml/scalar_squoted.yml"},
            {name: "scalar_squoted_ml",         file: "rapidyaml/scalar_squoted_multiline.yml"},
            {name: "scalar_plain",              file: "rapidyaml/scalar_plain.yml"},
            {name: "scalar_plain_ml",           file: "rapidyaml/scalar_plain_multiline.yml"},
        ]

        out := Array()
        for c in candidates {
            path := corpusRoot "\" StrReplace(c.file, "/", "\")
            if FileExist(path)
                out.Push({name: c.name, path: path})
        }
        return out
    }
}
