#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static char* strdup(const char* s) {
    size_t n = strlen(s) + 1;
    char* p = (char*)malloc(n);
    if (p) memcpy(p, s, n);
    return p;
}

static int ferror(FILE* f) { (void)f; return 0; }

#include <yaml.h>

/*
 * libyaml .c contents are concatenated here by build.ahk.
 * They are spliced in textually rather than included via #include,
 * so the "current file" for their internal `#include "yaml_private.h"`
 * is the compilation temp file (not libyaml/src/), which lets the
 * shim in src/native/shims/yaml_private.h win the quote-include search.
 */
/*__LIBYAML_SOURCES__*/

int __main() {
    yaml_parser_t parser;
    if (!yaml_parser_initialize(&parser)) return 1;
    yaml_parser_delete(&parser);
    return 42;
}
