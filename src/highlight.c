#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include "highlight.h"
#include "row.h"
#include "config.h"

static EDITOR_SYNTAX HLDB[] = {
    {
        "c",
        (char *[]){ ".c", ".h", NULL },
        (char *[]){
            "auto", "break", "case", "continue", "default", "do", "else", "enum",
            "extern", "for", "goto", "if", "register", "return", "sizeof", "static",
            "struct", "switch", "typedef", "union", "volatile", "while", "_Alignas",
            "_Alignof", "_Atomic", "_Bool", "_Complex", "_Generic", "_Imaginary",
            "_Noreturn", "_Static_assert", "_Thread_local", "inline", "restrict",

            // preprocessor words
            "#include", "#define", "#ifndef", "#ifdef", "#endif", "#if", "#else", "#elif",
            "#pragma", "#error", "#undef",

            // data types
            "int|", "long|", "double|", "float|", "char|", "unsigned|", "signed|",
            "void|", "short|", "size_t|", "ptrdiff_t|", "const|", "FILE|", NULL
        },
        "//",
        "/*",
        "*/",
        HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS
    },
    {
        "cpp",
        (char *[]){ ".cpp", ".cc", ".hpp", ".hxx", NULL },
        (char *[]){
            // c++ keywords
            "alignas", "alignof", "and", "and_eq", "asm", "auto", "bitand", "bitor",
            "bool", "break", "case", "catch", "class", "compl", "concept", "const",
            "consteval", "constexpr", "constinit", "const_cast", "continue", "co_await",
            "co_return", "co_yield", "decltype", "default", "delete", "do", "dynamic_cast",
            "else", "enum", "explicit", "export", "extern", "false", "for", "friend",
            "goto", "if", "inline", "mutable", "namespace", "new", "noexcept", "not",
            "not_eq", "nullptr", "operator", "or", "or_eq", "private", "protected", "public",
            "reflexpr", "register", "reinterpret_cast", "requires", "return", "sizeof",
            "static", "static_assert", "static_cast", "struct", "switch", "template", "this",
            "thread_local", "throw", "true", "try", "typedef", "typeid", "typename", "union",
            "using", "virtual", "volatile", "while", "xor", "xor_eq",

            // preprocessor words
            "#include", "#define", "#ifndef", "#ifdef", "#endif", "#if", "#else", "#elif",
            "#pragma", "#error", "#undef",

            // data types
            "int|", "long|", "double|", "float|", "char|", "unsigned|", "signed|",
            "void|", "short|", "string|", "vector|", "map|", "set|", "auto|", "const|",
            "size_t|", "bool|", "FILE|", NULL
        },
        "//",
        "/*",
        "*/",
        HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS
    },
    {
        "python",
        (char *[]){ ".py", NULL },
        (char *[]){
            "and", "as", "assert", "async", "await", "break", "class", "continue",
            "def", "del", "elif", "else", "except", "False", "finally", "for",
            "from", "global", "if", "import", "in", "is", "lambda", "None",
            "nonlocal", "not", "or", "pass", "raise", "return", "True", "try",
            "while", "with", "yield",

            "int|", "float|", "str|", "list|", "dict|", "set|", "bool|", "tuple|",
            "bytes|", "object|", "range|", NULL
        },
        "#",
        "'''",
        "'''",
        HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS
    }
};

#define HLDB_ENTRIES (sizeof(HLDB) / sizeof(HLDB[0]))

static unsigned char sep_table[256];
static int sep_table_ready;

static void buildSepTable(void) {
    for (int c = 0; c < 256; c++) {
        sep_table[c] = isspace(c) || strchr(",.()+-/*=~%<>[];:{}#", c) != NULL; // this line is ai "enhanced" fam icl but was better than my bs so it stays
    }
    sep_table[0] = 1;
    sep_table_ready = 1;
}

static int isSeparator(int c) {
    if (!sep_table_ready) {
        buildSepTable();
    }

    return sep_table[(unsigned char)c];
}

// cached per syntax so scanRow does not strlen the same tokens per char
static EDITOR_SYNTAX *tok_syntax;
static int scs_len, mcs_len, mce_len;
static int *kw_len;
static unsigned char kw_first[256];  // first char of any keyword, skip fast

// bytes that can change comment/string state, everything else the state only
// scan skips without looking, this is what makes opening a big file fast
static unsigned char interest_code[256];  // while outside a comment
static char mce0;  // first byte of the block comment end token

static inline int matchAt(const char *buf, int len, int i, const char *pat, int patlen) {
    return ((patlen > 0) && (i + patlen <= len) && (memcmp(&buf[i], pat, patlen) == 0));
}

static void buildTokenCache(void) {
    if (tok_syntax == CONFIG.syntax) {
        return;
    }
    tok_syntax = CONFIG.syntax;

    free(kw_len);
    kw_len = NULL;
    memset(kw_first, 0, sizeof(kw_first));
    memset(interest_code, 0, sizeof(interest_code));
    mce0 = 0;
    scs_len = mcs_len = mce_len = 0;
    if (CONFIG.syntax == NULL) {
        return;
    }

    scs_len = CONFIG.syntax->single_line_comment ? (int)strlen(CONFIG.syntax->single_line_comment) : 0;
    mcs_len = CONFIG.syntax->multi_line_comment_start ? (int)strlen(CONFIG.syntax->multi_line_comment_start) : 0;
    mce_len = CONFIG.syntax->multi_line_comment_end ? (int)strlen(CONFIG.syntax->multi_line_comment_end) : 0;

    char **kw = CONFIG.syntax->keywords;
    int n = 0;
    while (kw[n]) {
        n++;
    }

    kw_len = malloc(sizeof(int) * (n ? n : 1));
    if (kw_len == NULL) {
        return;
    }

    for (int j = 0; j < n; j++) {
        int len = (int)strlen(kw[j]);
        if (kw[j][len - 1] == '|') {
            len--;
        }
        kw_len[j] = len;
        kw_first[(unsigned char)kw[j][0]] = 1;
    }

    if (scs_len) {
        interest_code[(unsigned char)CONFIG.syntax->single_line_comment[0]] = 1;
    }
    if (mcs_len) {
        interest_code[(unsigned char)CONFIG.syntax->multi_line_comment_start[0]] = 1;
    }
    if (mce_len) {
        mce0 = CONFIG.syntax->multi_line_comment_end[0];
    }
    if (CONFIG.syntax->flags & HL_HIGHLIGHT_STRINGS) {
        interest_code['"'] = 1;
        interest_code['\''] = 1;
    }
}

/*
 * same decisions as scanRow, but it skips whole runs of bytes that cannot possibly change state instead of testing every one
 * this is 90% of file open time on a big file, hence the separate loop
 */
static int scanRowState(const char *buf, int len, int in_comment) {
    if (CONFIG.syntax == NULL || buf == NULL) {
        return 0;
    }

    EDITOR_SYNTAX *sx = CONFIG.syntax;
    const char *scs = sx->single_line_comment;
    const char *mcs = sx->multi_line_comment_start;
    const char *mce = sx->multi_line_comment_end;
    int strings = sx->flags & HL_HIGHLIGHT_STRINGS;

    int i = 0;
    int in_string = 0;

    while (i < len) {
        if (in_comment) {
            if (mce_len == 0) {
                break;
            }

            // only one byte can end a comment, memchr do skip
            const char *hit = memchr(&buf[i], mce0, (size_t)(len - i));
            if (hit == NULL) {
                break;
            }
            i = (int)(hit - buf);

            if (matchAt(buf, len, i, mce, mce_len)) {
                i += mce_len;
                in_comment = 0;
            }
            else {
                i++;
            }
            continue;
        }

        if (in_string) {
            while ((i < len) && (buf[i] != '\\') && (buf[i] != in_string)) {
                i++;
            }
            if (i >= len) {
                break;
            }

            if (buf[i] == '\\') {
                i += 2;
            }
            else {
                in_string = 0;
                i++;
            }
            continue;
        }

        while ((i < len) && (!interest_code[(unsigned char)buf[i]])) {
            i++;
        }
        if (i >= len) {
            break;
        }

        if (scs_len && matchAt(buf, len, i, scs, scs_len)) {
            return 0;
        }

        if (mcs_len && mce_len && (matchAt(buf, len, i, mcs, mcs_len))) {
            i += mcs_len;
            in_comment = 1;
            continue;
        }

        if (strings && (buf[i] == '"' || buf[i] == '\'')) {
            in_string = buf[i];
            i++;
            continue;
        }

        i++;
    }

    return in_comment;
}

void editorSyntaxPrepare(void) {
    if (!sep_table_ready) {
        buildSepTable();
    }
    buildTokenCache();
}

/*
 * hl == NULL means state only pass: no writes, no keyword matching, no allocation
 * keywords never contain a quote or comment token so skipping them cannot change the comment/string state this returns
 * both callers share this code so the cheap pass and the colour pass never disagree where a block comment ends
 */
static int scanRow(const char *buf, int len, int in_comment, unsigned char *hl) {
    if (CONFIG.syntax == NULL || buf == NULL) {
        return 0;
    }

    EDITOR_SYNTAX *sx = CONFIG.syntax;
    char **keywords = sx->keywords;
    const char *scs = sx->single_line_comment;
    const char *mcs = sx->multi_line_comment_start;
    const char *mce = sx->multi_line_comment_end;

    int prev_sep = 1;
    int in_string = 0;

    // preprocessor directive at column 0 gets keyword colour even if unknown
    if (hl && len > 0 && buf[0] == '#') {
        int i = 1;
        while (i < len && isspace((unsigned char)buf[i])) {
            i++;
        }
        while (i < len && !isspace((unsigned char)buf[i]) && (buf[i] != '(')) {
            i++;
        }
        if (i > 0) {
            memset(&hl[0], HL_KEYWORD1, i);
        }
    }

    int i = 0;
    while (i < len) {
        char c = buf[i];
        unsigned char prev_hl = (hl && i > 0) ? hl[i - 1] : HL_NORMAL;

        if (scs_len && !in_string && !in_comment && (matchAt(buf, len, i, scs, scs_len))) {
            if (hl) {
                memset(&hl[i], HL_COMMENT, len - i);
            }
            break;
        }

        if (mcs_len && mce_len && !in_string) {
            if (in_comment) {
                if (matchAt(buf, len, i, mce, mce_len)) {
                    if (hl) {
                        memset(&hl[i], HL_MULTI_LINE_COMMENT, mce_len);
                    }
                    i += mce_len;
                    in_comment = 0;
                    prev_sep = 1;
                }
                else {
                    if (hl) {
                        hl[i] = HL_MULTI_LINE_COMMENT;
                    }
                    i++;
                }
                continue;
            }
            else if (matchAt(buf, len, i, mcs, mcs_len)) {
                if (hl) {
                    memset(&hl[i], HL_MULTI_LINE_COMMENT, mcs_len);
                }
                i += mcs_len;
                in_comment = 1;
                continue;
            }
        }

        if (sx->flags & HL_HIGHLIGHT_STRINGS) {
            if (in_string) {
                if (hl) {
                    hl[i] = HL_STRING;
                }
                if (c == '\\' && i + 1 < len) {
                    if (hl) {
                        hl[i + 1] = HL_STRING;
                    }
                    i += 2;
                    continue;
                }
                if (c == in_string) {
                    in_string = 0;
                }
                i++;
                prev_sep = 1;
                continue;
            }
            else if (c == '"' || c == '\'') {
                in_string = c;
                if (hl) {
                    hl[i] = HL_STRING;
                }
                i++;
                continue;
            }
        }

        if (hl && (sx->flags & HL_HIGHLIGHT_NUMBERS)) {
            if ((isdigit((unsigned char)c) && (prev_sep || prev_hl == HL_NUMBER)) || (c == '.' && prev_hl == HL_NUMBER)) {
                hl[i] = HL_NUMBER;
                i++;
                prev_sep = 0;
                continue;
            }
        }

        if (hl && prev_sep && kw_len && kw_first[(unsigned char)c]) {
            int j, hit = 0;
            for (j = 0; keywords[j]; j++) {
                int klen = kw_len[j];
                if (!matchAt(buf, len, i, keywords[j], klen)) {
                    continue;
                }
                if (!isSeparator(i + klen < len ? buf[i + klen] : '\0')) {
                    continue;
                }

                memset(&hl[i], keywords[j][klen] == '|' ? HL_KEYWORD2 : HL_KEYWORD1, klen);
                i += klen;
                hit = 1;
                break;
            }
            if (hit) {
                prev_sep = 0;
                continue;
            }
        }

        prev_sep = isSeparator((unsigned char)c);
        i++;
    }

    return in_comment;
}

void editorHighlightRow(ROW_DATA *row) {
    if (row == NULL || row->render == NULL || row->hl_valid) {
        return;
    }

    int n = row->render_size > 0 ? row->render_size : 1;
    unsigned char *hl = realloc(row->hl, n);
    if (hl == NULL) {
        return;
    }
    row->hl = hl;
    memset(row->hl, HL_NORMAL, n);

    if (CONFIG.syntax && row->render_size > 0) {
        int start = (row->index > 0) ? CONFIG.row[row->index - 1].hl_open_comment : 0;
        scanRow(row->render, row->render_size, start, row->hl);
    }

    row->hl_valid = 1;
}

// walk a row range with a known start state, writing per row results
static void chainWriteRange(int lo, int hi, int state) {
    for (int i = lo; i < hi; i++) {
        ROW_DATA *r = &CONFIG.row[i];
        state = scanRowState(r->string, r->size, state);
        r->hl_open_comment = state;
        r->hl_valid = 0;
    }
}

/*
 * it looks strictly sequential (row N needs row N-1), so to use more than one core we guess instead;
 * split into chunks and scan every chunk from BOTH possible start states at once
 */
void editorScanCommentChainAll(void) {
    int n = CONFIG.numrows;
    if (n <= 0) {
        return;
    }

    if (CONFIG.syntax == NULL) {
        for (int i = 0; i < n; i++) {
            CONFIG.row[i].hl_open_comment = 0;
            CONFIG.row[i].hl_valid = 0;
        }

        return;
    }

    editorSyntaxPrepare();

    chainWriteRange(0, n, 0);
}

void editorScanCommentChain(int from, int stop_when_stable) {
    if (CONFIG.row == NULL) {
        return;
    }
    if (from < 0) {
        from = 0;
    }

    for (int i = from; i < CONFIG.numrows; i++) {
        ROW_DATA *r = &CONFIG.row[i];
        int in = (i > 0) ? CONFIG.row[i - 1].hl_open_comment : 0;
        int end = CONFIG.syntax ? scanRowState(r->string, r->size, in) : 0;

        int same = (end == r->hl_open_comment);
        r->hl_open_comment = end;
        r->hl_valid = 0;

        // output unchanged, so next row sees same input, so nothing downstream moves
        if (stop_when_stable && same) {
            break;
        }
    }
}

int editorSyntaxToColor(int hl) {
    switch (hl) {
        case HL_COMMENT:
        case HL_MULTI_LINE_COMMENT: return 36;    // cyan
        case HL_KEYWORD1: return 33;              // yellow
        case HL_KEYWORD2: return 32;              // green
        case HL_STRING: return 35;                // magenta
        case HL_NUMBER: return 31;                // red
        case HL_MATCH: return 34;                 // blue
        default: return 37;                       // white
    }
}

void editorSelectSyntaxHighlight(void) {
    CONFIG.syntax = NULL;
    if (CONFIG.filename == NULL) {
        return;
    }

    char *ext = strrchr(CONFIG.filename, '.');

    for (unsigned int j = 0; j < HLDB_ENTRIES; j++) {
        EDITOR_SYNTAX *s = &HLDB[j];
        for (unsigned int i = 0; s->filematch[i]; i++) {
            int is_ext = (s->filematch[i][0] == '.');
            if ((is_ext && ext && !strcmp(ext, s->filematch[i])) ||
                (!is_ext && strstr(CONFIG.filename, s->filematch[i]))) {
                CONFIG.syntax = s;
                buildTokenCache();
                editorScanCommentChainAll();

                return;
            }
        }
    }
}


int editorRowEndState(const char *buf, int len, int in_comment) {
    return scanRowState(buf, len, in_comment);
}

int editorRowEndStateSlow(const char *buf, int len, int in_comment) {
    unsigned char *hl = malloc(len > 0 ? (size_t)len : 1);
    if (hl == NULL) {
        return in_comment;
    }
    memset(hl, HL_NORMAL, len > 0 ? (size_t)len : 1);
    int r = scanRow(buf, len, in_comment, hl);
    free(hl);

    return r;
}
