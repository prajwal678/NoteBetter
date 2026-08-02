#ifndef HIGHLIGHT_H
#define HIGHLIGHT_H

#include "config.h"


void editorHighlightRow(ROW_DATA *row);
void editorScanCommentChain(int from, int stop_when_stable);
void editorScanCommentChainAll(void);

// full colour of one row incl chain fixup, used by the background worker
void editorUpdateSyntax(ROW_DATA *row);

int editorSyntaxToColor(int hl);
void editorSelectSyntaxHighlight(void);
void editorSyntaxPrepare(void);

int editorRowEndState(const char *buf, int len, int in_comment);
int editorRowEndStateSlow(const char *buf, int len, int in_comment);

#endif