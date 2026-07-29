#ifndef CONFIG_H
#define CONFIG_H

#include <termios.h>
#include <time.h>

#define NOTEBETTER_VERSION "0.0.1"
#define TAB_SPACE 4
#define QUIT_TIMES 2
#define CTRL_KEY(x) ((x) & 0x1f)


enum EditorKey {
    BACKSPACE = 127,
    ARROW_LEFT = 1000,
    ARROW_RIGHT,
    ARROW_UP,
    ARROW_DOWN,
    DELETE_KEY,
    HOME_KEY,
    END_KEY,
    PAGE_UP,
    PAGE_DOWN
};

enum EditorHighlight {
    HL_NORMAL = 0,
    HL_COMMENT,
    HL_MULTI_LINE_COMMENT,
    HL_KEYWORD1,
    HL_KEYWORD2,
    HL_STRING,
    HL_NUMBER,
    HL_MATCH
};

#define HL_HIGHLIGHT_NUMBERS (1 << 0)
#define HL_HIGHLIGHT_STRINGS (1 << 1)

typedef struct EditorSyntax {
    char *filetype;
    char **filematch;
    char **keywords;
    char *single_line_comment;
    char *multi_line_comment_start;
    char *multi_line_comment_end;
    int flags;
} EDITOR_SYNTAX;

typedef struct RowData {
    int index;
    int size;
    int render_size;
    char *string;
    char *render;
    unsigned char *hl;
    int hl_open_comment;
} ROW_DATA;

typedef struct EditorConfig {
    int cursor_x, cursor_y;
    int render_x;
    int row_offset;
    int column_offset;
    int screen_rows;
    int screen_columns;
    int gutter;                 // line number column width, 0 if off
    int text_columns;           // screen_columns minus the gutter
    int numrows;
    ROW_DATA *row;
    int dirty;
    char *filename;
    char status_message[80];
    time_t status_message_time;
    EDITOR_SYNTAX *syntax;
    struct termios pre_editor_terminal_config;
} EDITOR_CONFIG;

extern EDITOR_CONFIG CONFIG;

void initEditor(void);

#endif