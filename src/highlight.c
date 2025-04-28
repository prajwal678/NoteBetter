#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include "highlight.h"
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


int isSeparator(int c) {
    return isspace(c) || c == '\0' || strchr(",.()+-/*=~%<>[];:{}#", c) != NULL;
}

void editorUpdateSyntax(ROW_DATA *row) {
    if (row == NULL || row->render == NULL || row->render_size <= 0) return;
    
    unsigned char *new_hl = realloc(row->hl, row->render_size);
    if (new_hl == NULL) return;
    row->hl = new_hl;
    
    memset(row->hl, HL_NORMAL, row->render_size);

    if (CONFIG.syntax == NULL) return;

    char **keywords = CONFIG.syntax->keywords;
    char *scs = CONFIG.syntax->single_line_comment;
    char *mcs = CONFIG.syntax->multi_line_comment_start;
    char *mce = CONFIG.syntax->multi_line_comment_end;

    int scs_len = scs ? strlen(scs) : 0;
    int mcs_len = mcs ? strlen(mcs) : 0;
    int mce_len = mce ? strlen(mce) : 0;

    int prev_sep = 1;
    int in_string = 0;
    int in_comment = (row->index > 0 && CONFIG.row[row->index - 1].hl_open_comment);
    
    if (row->render[0] == '#') {
        int i = 1;
        while (i < row->render_size && isspace(row->render[i])) i++;
        
        int start = i;
        while (i < row->render_size && !isspace(row->render[i]) && row->render[i] != '(') i++;
        
        if (i > start) {
            memset(&row->hl[0], HL_KEYWORD1, i);
        }
    }

    int i = 0;
    while (i < row->render_size) {
        char c = row->render[i];
        unsigned char prev_hl = (i > 0) ? row->hl[i - 1] : HL_NORMAL;

        if (scs_len && !in_string && !in_comment) {
            if (!strncmp(&row->render[i], scs, scs_len)) {
                memset(&row->hl[i], HL_COMMENT, row->render_size - i);
                break;
            }
        }

        if (mcs_len && mce_len && !in_string) {
            if (in_comment) {
                row->hl[i] = HL_MULTI_LINE_COMMENT;
                if (!strncmp(&row->render[i], mce, mce_len)) {
                    memset(&row->hl[i], HL_MULTI_LINE_COMMENT, mce_len);
                    i += mce_len;
                    in_comment = 0;
                    prev_sep = 1;
                    continue;
                }
                else {
                    i++;
                    continue;
                }
            }
            else if (!strncmp(&row->render[i], mcs, mcs_len)) {
                memset(&row->hl[i], HL_MULTI_LINE_COMMENT, mcs_len);
                i += mcs_len;
                in_comment = 1;
                continue;
            }
        }

        if (CONFIG.syntax->flags & HL_HIGHLIGHT_STRINGS) {
            if (in_string) {
                row->hl[i] = HL_STRING;
                if (c == '\\' && i + 1 < row->render_size) {
                    row->hl[i + 1] = HL_STRING;
                    i += 2;
                    continue;
                }
                if (c == in_string) in_string = 0;
                i++;
                prev_sep = 1;
                continue;
            }
            else {
                if (c == '"' || c == '\'') {
                    in_string = c;
                    row->hl[i] = HL_STRING;
                    i++;
                    continue;
                }
            }
        }

        if (CONFIG.syntax->flags & HL_HIGHLIGHT_NUMBERS) {
            if ((isdigit(c) && (prev_sep || prev_hl == HL_NUMBER)) ||
                (c == '.' && prev_hl == HL_NUMBER)) {
                row->hl[i] = HL_NUMBER;
                i++;
                prev_sep = 0;
                continue;
            }
        }

        if (prev_sep) {
            int j;
            for (j = 0; keywords[j]; j++) {
                int klen = strlen(keywords[j]);
                int kw2 = keywords[j][klen - 1] == '|';
                if (kw2) klen--;

                if (!strncmp(&row->render[i], keywords[j], klen) &&
                    isSeparator(row->render[i + klen])) {
                    memset(&row->hl[i], kw2 ? HL_KEYWORD2 : HL_KEYWORD1, klen);
                    i += klen;
                    break;
                }
            }
            if (keywords[j] != NULL) {
                prev_sep = 0;
                continue;
            }
        }

        prev_sep = isSeparator(c);
        i++;
    }

    int changed = (row->hl_open_comment != in_comment);
    row->hl_open_comment = in_comment;
    if (changed && row->index + 1 < CONFIG.numrows)
        editorUpdateSyntax(&CONFIG.row[row->index + 1]);
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
    if (CONFIG.filename == NULL) return;

    char *ext = strrchr(CONFIG.filename, '.');
    if (!ext) return;

    for (unsigned int j = 0; j < HLDB_ENTRIES; j++) {
        EDITOR_SYNTAX *s = &HLDB[j];
        unsigned int i = 0;
        while (s->filematch[i]) {
            int is_ext = (s->filematch[i][0] == '.');
            if ((is_ext && ext && !strcmp(ext, s->filematch[i])) ||
                (!is_ext && strstr(CONFIG.filename, s->filematch[i]))) {
                CONFIG.syntax = s;

                for (int filerow = 0; filerow < CONFIG.numrows; filerow++) {
                    editorUpdateSyntax(&CONFIG.row[filerow]);
                }
                return;
            }
            i++;
        }
    }
}