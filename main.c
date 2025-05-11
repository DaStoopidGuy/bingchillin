#include <assert.h>
#include <ctype.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <raylib.h>
#include <stdlib.h>
#include <string.h>
#ifdef BUILD_RELEASE
#include "build/font.h"
#endif
#include "dynamic_array.h"

#define LOG(...) TraceLog(LOG_DEBUG, TextFormat(__VA_ARGS__))

// Configuration requested by Abdullah Rashid -_- //
#define TEXT_COLOR       GREEN
#define UI_COLOR         TEXT_COLOR
#define BG_COLOR         BLACK
#define CURSOR_COLOR     PINK
#define SELECTION_COLOR  YELLOW
#define DEFAULT_FONTSIZE 30

// TYPES
typedef struct {
    size_t start;
    size_t end;
} Line;

typedef struct {
    Line *items;
    size_t size;
    size_t count;
} Lines;

typedef struct {
    char *items;
    size_t size;
    size_t count;
} Buffer;

typedef struct {
    size_t  start;
    size_t  end;
    bool    exists;
} Selection;

typedef struct {
    size_t pos; // cursor position in buffer

    // for UI position
    size_t row;
    size_t col;
    int x;
    int y;
} Cursor;

typedef struct {
    char*  items; // message string
    size_t size;
    size_t count;

    double timer;
} Notification;

typedef struct {
    Color text;
    Color ui;
    Color bg;
    Color cursor;
    Color selection;
} Colors;

typedef struct {
    bool cursor_up;
    bool cursor_down;
    bool cursor_right;
    bool cursor_left;
    bool cursor_right_word;
    bool cursor_left_word;
    bool cursor_line_start;
    bool cursor_line_end;
    bool cursor_file_start;
    bool cursor_file_end;
    bool cursor_prev_empty_line;
    bool cursor_next_empty_line;
    bool page_up;
    bool page_down;
    bool zoom_in;
    bool zoom_out;
    bool backspace;
    bool delete;
    bool backspace_word;
    bool delete_word;
    bool enter;
    bool tab;
    bool select; // shift held down
    bool select_all;
    bool escape;
    bool copy;
    bool cut;
    bool paste;
    bool save_file;
    bool quit;
} Inputs;

typedef struct {
    Cursor c;
    Buffer buffer;
    Lines  lines;
    Selection selection;

    int scrollX;
    int scrollY;
    
    const char * filename;

    Inputs inputs;

    Notification notif;

    int fontSize;
    int fontSpacing;
    Font font;

    int leftMargin;

    Colors colors;
} Editor;

void notification_update(Notification *n) {
    if (n->timer <= 0) 
        return;
    n->timer -= GetFrameTime();
}

void notification_issue(Notification *n, const char* message, double timeout) {
    da_reserve(n, strlen(message)+1);
    strcpy(n->items, message);

    n->timer = timeout;
}

void notification_clear(Notification *n) {
    n->timer = 0.0;
}

size_t cursor_get_row(Cursor *c, Lines lines) {
    assert(lines.count > 0);
    for (size_t i=0; i<lines.count; i++)
    {
        Line line = lines.items[i];
        if (c->pos >= line.start && c->pos <= line.end)
            return i;
    }
    // HACK: might cause bugs later?
    // - return last line as current row
    //   if cursor isn't found in any line
    return lines.count - 1;
}

size_t cursor_get_col(Cursor *c, Lines lines) {
    Line currentLine = lines.items[c->row];
    return c->pos - currentLine.start;
}

int editor_measure_text(Editor *e, const char *textStart, int n) {
    int width = 0;
    char *textToMeasure = malloc(sizeof(char) * (n + 1));
    strncpy(textToMeasure, textStart, n);
    textToMeasure[n] = '\0'; // null terminating it so MeasureText..() can detect the end
    if (n > 0)
        width = (int) MeasureTextEx(e->font, textToMeasure, e->fontSize, e->fontSpacing).x;

    free(textToMeasure);
    return width;
}

int editor_measure_str(Editor *e, const char *str) {
    int width = MeasureTextEx(e->font, str, e->fontSize, e->fontSpacing).x;
    return width;
}

void editor_cursor_update(Editor *e) {
    // find current row
    e->c.row = cursor_get_row(&e->c, e->lines);
    
    // find current col
    e->c.col = cursor_get_col(&e->c, e->lines);

    // calculate cursor X and Y position on screen
    // Y position
    e->c.y = e->c.row * e->fontSize;

    // X position
    // measure the text from line start upto cursor position
    const Line currentLine = e->lines.items[e->c.row];
    const int requiredSize = e->c.pos - currentLine.start;

    e->c.x = editor_measure_text(e, &e->buffer.items[currentLine.start], requiredSize) + e->leftMargin;
}

void editor_cursor_right(Editor *e) {
    if (e->buffer.count == 0) return;
    if (e->c.pos > e->buffer.count - 1) return;
    e->c.pos++;
}

void editor_cursor_left(Editor *e) {
    if (e->c.pos == 0) return;
    e->c.pos--;
}

void editor_cursor_down(Editor *e) {
    if (e->c.row+1 > e->lines.count - 1) return;

    Line nextLine = e->lines.items[e->c.row+1];
    size_t nextLineSize = nextLine.end - nextLine.start;

    if (nextLineSize >= e->c.col)
        e->c.pos = nextLine.start + e->c.col;
    else
        e->c.pos = nextLine.end;
}

void editor_cursor_up(Editor *e) {
    if (e->c.row == 0) return;

    Line prevLine = e->lines.items[e->c.row-1];
    size_t prevLineSize = prevLine.end - prevLine.start;

    if (prevLineSize >= e->c.col)
        e->c.pos = prevLine.start + e->c.col;
    else
        e->c.pos = prevLine.end;
}

void editor_cursor_to_next_word(Editor *e) {
    bool foundWhitespace = false;

    for (size_t i=e->c.pos; i<e->buffer.count; i++)
    {
        const char c = e->buffer.items[i];
        const bool checkWhitespace = c==' ' || c=='\n';

        if (checkWhitespace)
            foundWhitespace = true;

        // check if char is whitespace
        if (foundWhitespace && ( !checkWhitespace || c=='\n' ))
        {
            e->c.pos = i;
            return;
        }
    }
    // if no next word found
    const Line line = e->lines.items[e->c.row];
    e->c.pos = line.end;
}

void editor_cursor_to_prev_word(Editor *e) {
    bool foundWhitespace = false;
    
    for (size_t i=e->c.pos; i!=0; i--)
    {
        const char c = e->buffer.items[i-1];
        const bool checkWhitespace = c==' ' || c=='\n';

        if (checkWhitespace)
            foundWhitespace = true;

        // check if char is whitespace
        if (foundWhitespace && ( !checkWhitespace || c=='\n' ))
        {
            e->c.pos = i;
            return;
        }
    }
    // no prev word found
    const Line line = e->lines.items[e->c.row];
    e->c.pos = line.start;
}

void editor_cursor_to_line_start(Editor *e) {
    Line line = e->lines.items[e->c.row];
    e->c.pos = line.start;
}

void editor_cursor_to_line_end(Editor *e) {
    Line line = e->lines.items[e->c.row];
    e->c.pos = line.end;
}

void editor_cursor_to_first_line(Editor *e) {
    Line firstLine = e->lines.items[0];
    e->c.pos = firstLine.start;
}

void editor_cursor_to_last_line(Editor *e) {
    Line lastLine = e->lines.items[e->lines.count - 1];
    e->c.pos = lastLine.end;
}

// returns if action was successfull or not
bool editor_cursor_to_line_number(Editor *e, size_t lineNumber) {
    // TODO: for now just move to line start, maybe it is the behaviour i want lol
    if (lineNumber < 1 || lineNumber >= e->lines.count) return false;

    size_t lineIndex = lineNumber - 1;
    Line requiredLine = e->lines.items[lineIndex];
    e->c.pos = requiredLine.start;
    return true;
}

void editor_cursor_to_next_empty_line(Editor *e) {
    for (size_t i=e->c.row+1; i<e->lines.count; i++)
    {
        Line line = e->lines.items[i];
        size_t lineSize = line.end - line.start;
        if (lineSize == 0)
        {
            e->c.pos = line.start;
            return;
        }
    }
    // move to last line if no next empty line found
    Line lastLine = e->lines.items[e->lines.count - 1];
    e->c.pos = lastLine.start;
    return;
}

void editor_cursor_to_prev_empty_line(Editor *e) {
    if (e->c.row == 0 || e->c.row >= e->lines.count) return;
    for (size_t i=e->c.row-1; i!=0; i--)
    {
        Line line = e->lines.items[i];
        size_t lineSize = line.end - line.start;
        if (lineSize == 0)
        {
            e->c.pos = line.start;
            return;
        }
    }
    // move to first line if no previous empty line found
    Line firstLine = e->lines.items[0];
    e->c.pos = firstLine.start;
    return;
}

void editor_calculate_lines(Editor *e) {
    da_free(&e->lines);
    Line line = {0};
    line.start = 0;
    for (size_t i=0; i<e->buffer.count; i++)
    {
        char c = e->buffer.items[i];
        if (c == '\n')
        {
            line.end = i;
            da_append(&e->lines, line);
            line.start = i+1;
        }
    }

    // there's always atleast one line 
    // a lot of code depends upon that assumption
    da_append(&e->lines, ((Line){
        line.start,
        e->buffer.count,
    }));
}

// Initialize Editor struct
void editor_init(Editor *e) {
    e->c = (Cursor) {0};
    e->buffer = (Buffer) {0};
    da_init(&e->buffer);
    e->lines = (Lines) {0};
    da_init(&e->lines);

    e->scrollX = 0;
    e->scrollY = 0;
    
    e->filename = NULL;

    e->inputs = (Inputs) {0};

    e->notif = (Notification) {0};
    da_init(&e->notif);

#ifdef BUILD_RELEASE
    e->font = LoadFont_Font();
#else
    e->font = LoadFont("monogram.ttf");
#endif
    e->fontSize = DEFAULT_FONTSIZE;
    e->fontSpacing = 0;
    SetTextLineSpacing(e->fontSize);

    e->leftMargin = 0;
    editor_calculate_lines(e); // NOTE: running this once results in there
                               // being atleast one `Line`

    { // init colors
        // default colors
        e->colors = (Colors) {
            .text = TEXT_COLOR,
            .ui = UI_COLOR,
            .bg = BG_COLOR,
            .cursor = CURSOR_COLOR,
            .selection = SELECTION_COLOR,
        };
    }
}

void editor_deinit(Editor *e) {
    da_free(&e->buffer);
    da_free(&e->lines);
    da_free(&e->notif);
#ifndef BUILD_RELEASE
    UnloadFont(e->font);
#endif
}

void editor_insert_char_at_cursor(Editor *e, char c) {
    da_append(&e->buffer, '\0'); // increases count by one
    // move memory from cursor to end right by one
    char *src = e->buffer.items + e->c.pos;
    char *dest = src + 1;
    size_t len = e->buffer.count - 1 - e->c.pos;

    memmove(dest, src, len);
    // set the char on cursor pos
    e->buffer.items[e->c.pos] = c;

    // move cursor right by one character
    e->c.pos++;

    editor_calculate_lines(e);
}

void editor_remove_char_before_cursor(Editor *e) {
    if (e->c.pos == 0) return;

    char *src = e->buffer.items + e->c.pos;
    char *dest = e->buffer.items + e->c.pos - 1;
    size_t len = e->buffer.count - e->c.pos;
    memmove(dest, src, len);
    
    // set cursor position & buffer count
    e->c.pos--;
    e->buffer.count--;

    editor_calculate_lines(e);
}

void editor_remove_char_at_cursor(Editor *e) {
    if (e->buffer.count == 0) return;
    if (e->c.pos > e->buffer.count - 1) return;

    char *src = e->buffer.items + e->c.pos + 1;
    char *dest = e->buffer.items + e->c.pos;
    size_t len = e->buffer.count - (e->c.pos + 1);
    memmove(dest, src, len);

    e->buffer.count--;

    editor_calculate_lines(e);
}

void editor_select(Editor *e, size_t startingPos) {
    if (e->buffer.count == 0) return;
    Selection *s = &e->selection;
    if (!s->exists)
    {
        *s = (Selection) {
            .start = startingPos,
            .end = e->c.pos,
            .exists = true,
        };
    } else
    {
        s->end = e->c.pos;
    }
    LOG("Selection After %lu - %lu", s->start, s->end);
}

void editor_selection_clear(Editor *e) {
    e->selection = (Selection) {
        .start = 0,
        .end = 0,
        .exists = false,
    };
    LOG("Selection cleared");
}

void editor_selection_delete(Editor *e) {
    Selection *s = &e->selection;
    int start, end;
    if (s->start <= s->end) {
        start = s->start;
        end = s->end;
    } else {
        start = s->end;
        end = s->start;
    }

    char* src = e->buffer.items + end;
    char* dest = e->buffer.items + start;
    size_t len = e->buffer.count - end;

    memmove(dest, src, len);

    e->c.pos = start;
    e->buffer.count -= end - start;
    editor_selection_clear(e);
    editor_calculate_lines(e);
}

void editor_select_all(Editor *e) {
    const Line firstLine = e->lines.items[0];
    const Line lastLine  = e->lines.items[e->lines.count - 1];

    e->selection = (Selection) {
        .start = firstLine.start,
        .end   = lastLine.end,
        .exists = true,
    };
    LOG("Selected all");
}

void editor_remove_word_before_cursor(Editor *e) {
    int startingPos = e->c.pos;
    editor_cursor_to_prev_word(e);
    editor_select(e, startingPos);
    editor_selection_delete(e);
}

void editor_remove_word_after_cursor(Editor *e) {
    int startingPos = e->c.pos;
    editor_cursor_to_next_word(e);
    editor_select(e, startingPos);
    editor_selection_delete(e);
}

void editor_copy(Editor *e) {
    //get selected text or current line
    char *text = NULL;
    if (e->selection.exists)
    {
        int start, end = 0;

        if (e->selection.end > e->selection.start)
        {
            start = e->selection.start;
            end = e->selection.end;
        }
        else
        {
            start = e->selection.end;
            end = e->selection.start;
        }

        const int length = end - start;
        text = malloc(sizeof(char) * (length + 1));
        strncpy(text, &e->buffer.items[start], length);
        text[length] = '\0';
    }
    else
    {
        Line currentLine = e->lines.items[e->c.row];
        const int length = currentLine.end - currentLine.start;
        text = malloc(sizeof(char) * (length + 1));
        strncpy(text, &e->buffer.items[currentLine.start], length);
        text[length] = '\0';
    }

    //put it into clipboard lol
    SetClipboardText(text);
    free(text);
    LOG("Copied text");
}

void editor_cut(Editor *e) {
    editor_copy(e);
    if (e->selection.exists)
        editor_selection_delete(e);
    else
    {   // delete current line
        Line currentLine = e->lines.items[e->c.row];
        e->selection = (Selection) {
            .exists = true,
            .start  = currentLine.start,
            .end    = currentLine.end,
        };
        editor_selection_delete(e);
    }
}

void editor_paste(Editor *e) {
    const char *text = GetClipboardText();

    if (e->selection.exists) 
        editor_selection_delete(e);

    for (int i=0; i<strlen(text); i++)
        editor_insert_char_at_cursor(e, text[i]);
    // TODO: editor_insert_str_at_cursor() needed?
    LOG("Pasted into editor");
}

void editor_set_font_size(Editor *e, int newFontSize) {
    if (newFontSize <= 0) return;
    e->fontSize = newFontSize;
    SetTextLineSpacing(e->fontSize);
    LOG("font size changed to %d", e->fontSize);
}

void editor_load_file(Editor *e, const char *filename) {
    LOG("Opening file: %s", filename);
    e->filename = filename;
    SetWindowTitle(TextFormat("%s | the bingchillin text editor", e->filename));

    // get size of the file
    FILE *f = fopen(filename, "r");
    if (f == NULL)
    {
        perror("Error opening file");
        //exit(1);
        return;
    }

    fseek(f, 0L, SEEK_END);
    long int size = ftell(f);
    rewind(f); // set cursor back to beginning?
    LOG("size of file(%s):%ld\n", filename, size);
    
    // allocate that much memory in the buffer
    da_reserve(&e->buffer, (size_t)size);

    // copy file's contents to buffer
    fread(e->buffer.items, 1, size, f);
    e->buffer.count = size;

    // remember to close file
    fclose(f);

    editor_calculate_lines(e);
}

void editor_save_file(Editor *e) {
    if (e->filename == NULL)
    {
        notification_issue(&e->notif, "Can not save: File does not exist", 1);
        return;
    }
    notification_issue(&e->notif, TextFormat("Saving to file: %s", e->filename), 1);

    // open file for writing
    FILE *f = fopen(e->filename, "w");
    if (f == NULL)
    {
        perror("Cannot open file for saving");
        exit(1);
    }
    // write the buffer to the previously opened file
    fwrite(e->buffer.items, 1, e->buffer.count, f);

    fclose(f);
}

bool editor_key_pressed(KeyboardKey key) {
    return IsKeyPressed(key) || IsKeyPressedRepeat(key);
}

void editor_draw_text(Editor *e, const char* text, Vector2 pos, Color color) {
    DrawTextEx(e->font, text, pos, e->fontSize, e->fontSpacing, color);
}

void inputs_update(Inputs *i) {
    *i = (Inputs) {0}; // reset

    bool ctrl = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
    i->select = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);

    if (ctrl)
    {
        i->zoom_in = editor_key_pressed(KEY_EQUAL);
        i->zoom_out = editor_key_pressed(KEY_MINUS);
        i->select_all = IsKeyPressed(KEY_A);

        i->save_file = IsKeyPressed(KEY_S);
        i->quit = IsKeyPressed(KEY_Q);

        i->copy = IsKeyPressed(KEY_C);
        i->cut = IsKeyPressed(KEY_X);
        i->paste = editor_key_pressed(KEY_V);

        i->cursor_right_word = editor_key_pressed(KEY_RIGHT);
        i->cursor_left_word = editor_key_pressed(KEY_LEFT);
        i->cursor_next_empty_line = editor_key_pressed(KEY_DOWN);
        i->cursor_prev_empty_line = editor_key_pressed(KEY_UP);

        i->cursor_file_start = IsKeyPressed(KEY_HOME);
        i->cursor_file_end = IsKeyPressed(KEY_END);

        i->backspace_word = IsKeyPressed(KEY_BACKSPACE);
        i->delete_word = IsKeyPressed(KEY_DELETE);
    }
    else
    {
        i->cursor_right = editor_key_pressed(KEY_RIGHT);
        i->cursor_left = editor_key_pressed(KEY_LEFT);
        i->cursor_up = editor_key_pressed(KEY_UP);
        i->cursor_down = editor_key_pressed(KEY_DOWN);

        i->cursor_line_start = IsKeyPressed(KEY_HOME);
        i->cursor_line_end = IsKeyPressed(KEY_END);

        i->backspace = IsKeyPressed(KEY_BACKSPACE);
        i->delete = IsKeyPressed(KEY_DELETE);
    }

    i->page_up = editor_key_pressed(KEY_PAGE_UP);
    i->page_down = editor_key_pressed(KEY_PAGE_DOWN);

    i->enter = editor_key_pressed(KEY_ENTER);
    i->tab = IsKeyPressed(KEY_TAB);
    i->escape = IsKeyPressed(KEY_ESCAPE);
}

bool editor_update(Editor *e) {
    inputs_update(&e->inputs);

    if (IsKeyDown(KEY_LEFT_CONTROL)) {
        if (editor_key_pressed(KEY_EQUAL))
            editor_set_font_size(e, e->fontSize + 1);

        if (editor_key_pressed(KEY_MINUS))
            editor_set_font_size(e, e->fontSize - 1);

        if (IsKeyPressed(KEY_A)) editor_select_all(e);

        if (IsKeyPressed(KEY_S)) editor_save_file(e);
        if (IsKeyPressed(KEY_Q)) return true;

        if (IsKeyPressed(KEY_C)) editor_copy(e);
        if (IsKeyPressed(KEY_X)) editor_cut(e);
        if (editor_key_pressed(KEY_V)) editor_paste(e);
    }

    // -------------------
    // Movement stuff
    size_t startingPos = e->c.pos;
    bool cursorMoved = false;

    if (e->inputs.cursor_right) {
        cursorMoved = true;
        LOG("Cursor right");
        editor_cursor_right(e);
    }

    if (e->inputs.cursor_left)
    {
        cursorMoved = true;
        LOG("Cursor left");
        editor_cursor_left(e);
    }

    if (e->inputs.cursor_up) {
        cursorMoved = true;
        LOG("Cursor up");
        editor_cursor_up(e);
    }

    if (e->inputs.cursor_down) {
        cursorMoved = true;
        LOG("Cursor down");
        editor_cursor_down(e);
    }

    if (e->inputs.cursor_right_word) {
        cursorMoved = true;
        LOG("Cursor right word");
        editor_cursor_to_next_word(e);
    }

    if (e->inputs.cursor_left_word) {
        cursorMoved = true;
        LOG("Cursor left word");
        editor_cursor_to_prev_word(e);
    }

    if (e->inputs.cursor_up) {
        cursorMoved = true;
        LOG("Cursor up");
        editor_cursor_up(e);
    }
    if (e->inputs.cursor_down) {
        cursorMoved = true;
        LOG("Cursor down");
        editor_cursor_down(e);
    }

    if (e->inputs.cursor_line_start) {
        cursorMoved = true;
        LOG("Cursor line start");
        editor_cursor_to_line_start(e);
    }

    if (e->inputs.cursor_line_end) {
        cursorMoved = true;
        LOG("Cursor line end");
        editor_cursor_to_line_end(e);
    }

    if (e->inputs.cursor_file_start) {
        cursorMoved = true;
        LOG("Cursor first line");
        editor_cursor_to_first_line(e);
    }

    if (e->inputs.cursor_file_end) {
        cursorMoved = true;
        LOG("Cursor last line");
        editor_cursor_to_last_line(e);
    }

    if(e->inputs.page_up) {
        cursorMoved = true;
        LOG("PageUp key pressed");
        if (!editor_cursor_to_line_number(e, e->c.row+1 - 10))
            editor_cursor_to_first_line(e);
    }

    if(e->inputs.page_down)
    {
        cursorMoved = true;
        LOG("PageDown key pressed");
        if (!editor_cursor_to_line_number(e, e->c.row+1 + 10))
            editor_cursor_to_last_line(e);
    }

    if (e->inputs.select && cursorMoved) editor_select(e, startingPos);
    else if (cursorMoved) editor_selection_clear(e);

    // Movement stuff ends
    // -------------------

    if (e->inputs.enter) {
        LOG("Enter key pressed");
        if (e->selection.exists) editor_selection_delete(e);
        // finds number of spaces on current line
        int spaces = 0;
        {
            const Line currentLine = e->lines.items[e->c.row];
            for (; e->buffer.items[currentLine.start + spaces] == ' '; spaces++);
        }
        // puts same amount of spaces on the new line
        editor_insert_char_at_cursor(e, '\n');
        for (int i = 0; i<spaces; i++) editor_insert_char_at_cursor(e, ' ');
    }

    if (e->inputs.tab) {   // TODO: implement proper tab behaviour
        LOG("Tab key pressed");
        for (int i=0; i<4; i++) editor_insert_char_at_cursor(e, ' ');
    }

    if (e->inputs.escape) {
        editor_selection_clear(e);
        notification_clear(&e->notif);
    }

    if (e->inputs.backspace) {
        LOG("Backspace pressed");
        if (e->selection.exists)
            editor_selection_delete(e);
        else
            editor_remove_char_before_cursor(e);
    }

    if (e->inputs.delete) {
        LOG("Delete pressed");
        if (e->selection.exists)
            editor_selection_delete(e);
        else
            editor_remove_char_at_cursor(e);
    }

    if (e->inputs.backspace_word) {
        LOG("Backspace word");
        editor_remove_word_before_cursor(e);
    }

    if (e->inputs.delete_word) {
        LOG("Delete word");
        editor_remove_word_after_cursor(e);
    }


    char key = GetCharPressed();
    if (key) {
        LOG("%c - character pressed", key);
        if (e->selection.exists) editor_selection_delete(e);
        editor_insert_char_at_cursor(e, key);
    }

    notification_update(&e->notif);
    
    { // Update Editor members
        editor_cursor_update(e);
    }

    { // update editor scroll offset
      // NOTE: do not use old e->c.row
      // - update it first `the cursor_update() function`

        const int winWidth = GetScreenWidth();
        const int winHeight = GetScreenHeight();

        // X offset calculation
        const int cursorX = e->c.x;
        const int winRight = winWidth - e->scrollX;
        const int winLeft = 0 - e->scrollX + e->leftMargin;

        if ( cursorX > winRight )
            e->scrollX = winWidth-cursorX-1;
        else if ( cursorX < winLeft )
            e->scrollX = -cursorX + e->leftMargin;

        // Y offset calulation
        const int cursorTop = e->c.y;
        const int cursorBottom = cursorTop + e->fontSize;
        const int winBottom = winHeight - e->scrollY;
        const int winTop = 0 - e->scrollY;

        if (cursorBottom > winBottom)
            e->scrollY = winHeight - cursorBottom;
        else if (cursorTop < winTop)
            e->scrollY = -cursorTop;
    }
    return 0;
}

void editor_draw(Editor *e) {
        BeginDrawing();
        ClearBackground(BG_COLOR);

        { // Render Text Buffer
            // null terminate buffer before drawing it
            da_append(&e->buffer, '\0');

            Vector2 pos = {
                e->leftMargin+e->scrollX, 
                0+e->scrollY,
            };
            editor_draw_text(e, e->buffer.items, pos, e->colors.text);
            // remove \0
            da_remove(&e->buffer);
        }

        { // Render selection
            const Selection s = e->selection;

            if (s.exists) {
                size_t start, end = 0;
                if (s.start <= s.end) {
                    start = s.start;
                    end = s.end;
                }
                else {
                    start = s.end;
                    end = s.start;
                }

                bool selectionFound = false;

                for (size_t i=0; i<e->lines.count; i++)
                {
                    const Line line = e->lines.items[i];

                    Rectangle rect = {
                        .height = e->fontSize,
                        .width = editor_measure_text(e, &e->buffer.items[line.start], line.end - line.start),
                        .x = 0,
                        .y = (int)i * e->fontSize,
                    };
                    bool startInLine = false;
                    if (start >= line.start && start <= line.end)
                    {
                        startInLine = selectionFound = true;
                        int chars = abs((int)line.start - (int)start);
                        rect.x = editor_measure_text(e, &e->buffer.items[line.start], chars);
                        rect.width = rect.width - rect.x;
                    }

                    if (end >= line.start && end <= line.end)
                    {
                        if (startInLine) {
                            int chars = end - start;
                            rect.width = editor_measure_text(e, &e->buffer.items[start], chars);
                        }
                        else {
                            int chars = abs((int)line.start - (int)end);
                            rect.width = editor_measure_text(e, &e->buffer.items[line.start], chars);
                        }
                    }

                    if (line.start > end || line.end < start)
                        selectionFound = false;

                    if (selectionFound)
                    {
                        DrawRectangleLines(rect.x + e->scrollX + e->leftMargin, rect.y + e->scrollY, rect.width, rect.height, e->colors.selection);
                    }
                }
            }
        }

        { // Render line numbers
            // blank box under line numbers
            DrawRectangle(0, 0, e->leftMargin, GetScreenHeight(), e->colors.bg);

            // draw vertical line seperating the line nums
            DrawLine(e->leftMargin-1, 0, e->leftMargin-1, GetScreenHeight(), e->colors.ui);
            
            // the line numbers
            const char *strLineNum;
            for (size_t i=0; i<e->lines.count; i++)
            {
                strLineNum = TextFormat("%lu", i+1);
                Vector2 pos = {
                    0,
                    (int)(e->fontSize*i) + e->scrollY,
                    // NOTE: i being size_t causes HUGE(obviously) underflow on line 0 when scrollY < 0
                    // -  solution cast to (int): may cause issue later (pain) :( 
                };
                editor_draw_text(e, strLineNum, pos, e->colors.ui);
            }
            e->leftMargin = strlen(strLineNum) + 2;
            e->leftMargin *= editor_measure_str(e, "a");
        }

        { // Render cursor (atleast trying to)
            DrawLine(e->c.x + e->scrollX + 1, e->c.y + e->scrollY, e->c.x + e->scrollX + 1, e->c.y + e->scrollY + e->fontSize, e->colors.cursor);
        }

        // Render Notification
        if (e->notif.timer > 0.0) {
            int textW = editor_measure_str(e, e->notif.items);
            int textH = e->fontSize;

            Vector2 textPos = {
                GetScreenWidth()/2.0f - (double)textW/2,
                GetScreenHeight()/2.0f - (double)textH/2,
            };
            // render blank box
            const int padding = 5;
            DrawRectangle(textPos.x-padding, textPos.y-padding, textW+(padding*2), textH+(padding*2), e->colors.bg);
            DrawRectangleLines(textPos.x-padding, textPos.y-padding, textW+(padding*2), textH+(padding*2), e->colors.cursor);
            // render notification message
            editor_draw_text(e, e->notif.items, textPos, e->colors.cursor);
        }

        EndDrawing();
}

int main(int argc, char **argv) {
#ifdef BUILD_RELEASE
    SetTraceLogLevel(LOG_ERROR);
#else
    SetTraceLogLevel(LOG_DEBUG);
#endif
    InitWindow(800, 600, "the bingchillin text editor");
    SetWindowState(FLAG_WINDOW_RESIZABLE); // HACK: not fully tested with resizing enabled
                                           // might cause some bugs
    SetExitKey(KEY_NULL);
    SetTargetFPS(60);

    Editor editor = {0};

    editor_init(&editor);

    if (argc > 1) {
        editor_load_file(&editor, argv[1]);
    }
    
    bool shouldQuit = false;
    while(!WindowShouldClose() && !shouldQuit)
    {
        shouldQuit = editor_update(&editor);
        editor_draw(&editor);
    }

    editor_deinit(&editor);
    CloseWindow();

    return 0;
}
