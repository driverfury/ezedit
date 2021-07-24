/*
 * TODO:
 *
 * - Message before exiting unsaved
 * - Ask when closing/opening a new file (if changes have been made)
 * - Ctrl+O (Open file)
 * - You can't do anything in void buffers file (you can't move nor insert)
 *   Maybe, always add a space character or a new line character??
 * - Undo, Redo system
 * - Search
 * - Search&Replace
 * - Help page
 * - What about non-ASCII chars? Check if file is ASCII of utf-8 before opening it
 *   into the editor?
 * - Syntax highlighting
 * - UTF-8 support
 * - Multi-editor support (split screen)
 *
 */

#define EZ_IMPLEMENTATION
#define EZ_NO_CRT_LIB
#include "ez.h"

#include <windows.h>

/******************************************************************************/
/**                                   DEFS                                   **/
/******************************************************************************/

#define CHAR_FULL_BLOCK   '\xdb'
#define CHAR_DARK_SHADE   '\xb2'
#define CHAR_MEDIUM_SHADE '\xb1'
#define CHAR_LIGHT_SHADE  '\xb0'

#define COLOR_BLACK          0
#define COLOR_BLUE           1
#define COLOR_GREEN          2
#define COLOR_CYAN           3
#define COLOR_RED            4
#define COLOR_MAGENTA        5
#define COLOR_YELLOW         6
#define COLOR_WHITE          7
#define COLOR_BRIGHT_BLACK   8
#define COLOR_BRIGHT_BLUE    9
#define COLOR_BRIGHT_GREEN   10
#define COLOR_BRIGHT_CYAN    11
#define COLOR_BRIGHT_RED     12
#define COLOR_BRIGHT_MAGENTA 13
#define COLOR_BRIGHT_YELLOW  14
#define COLOR_BRIGHT_WHITE   15

#define FG_BLACK          COLOR_BLACK
#define FG_BLUE           COLOR_BLUE
#define FG_GREEN          COLOR_GREEN
#define FG_CYAN           COLOR_CYAN
#define FG_RED            COLOR_RED
#define FG_MAGENTA        COLOR_MAGENTA
#define FG_YELLOW         COLOR_YELLOW
#define FG_WHITE          COLOR_WHITE
#define FG_BRIGHT_BLACK   COLOR_BRIGHT_BLACK
#define FG_BRIGHT_BLUE    COLOR_BRIGHT_BLUE
#define FG_BRIGHT_GREEN   COLOR_BRIGHT_GREEN
#define FG_BRIGHT_CYAN    COLOR_BRIGHT_CYAN
#define FG_BRIGHT_RED     COLOR_BRIGHT_RED
#define FG_BRIGHT_MAGENTA COLOR_BRIGHT_MAGENTA
#define FG_BRIGHT_YELLOW  COLOR_BRIGHT_YELLOW
#define FG_BRIGHT_WHITE   COLOR_BRIGHT_WHITE

#define BG_BLACK          (COLOR_BLACK << 4)
#define BG_BLUE           (COLOR_BLUE << 4)
#define BG_GREEN          (COLOR_GREEN << 4)
#define BG_CYAN           (COLOR_CYAN << 4)
#define BG_RED            (COLOR_RED << 4)
#define BG_MAGENTA        (COLOR_MAGENTA << 4)
#define BG_YELLOW         (COLOR_YELLOW << 4)
#define BG_WHITE          (COLOR_WHITE << 4)
#define BG_BRIGHT_BLACK   (COLOR_BRIGHT_BLACK << 4)
#define BG_BRIGHT_BLUE    (COLOR_BRIGHT_BLUE << 4)
#define BG_BRIGHT_GREEN   (COLOR_BRIGHT_GREEN << 4)
#define BG_BRIGHT_CYAN    (COLOR_BRIGHT_CYAN << 4)
#define BG_BRIGHT_RED     (COLOR_BRIGHT_RED << 4)
#define BG_BRIGHT_MAGENTA (COLOR_BRIGHT_MAGENTA << 4)
#define BG_BRIGHT_YELLOW  (COLOR_BRIGHT_YELLOW << 4)
#define BG_BRIGHT_WHITE   (COLOR_BRIGHT_WHITE << 4)

#define CTRL_KEY(k) ((k) & 0x1f)

typedef enum
editor_mode
{
    MODE_NORMAL,
    MODE_INSERT,
} editor_mode;

/******************************************************************************/
/**                               GAP BUFFER                                 **/
/******************************************************************************/

/*
 * Gap buffer
 *
 * text text text $$$$$ text text text
 *                ^    ^
 *              start end
 *
 * gap_len = gap_end - gap_start
 */
typedef struct
gapbuff
{
    size_t  size;
    char   *content;
    size_t  gap_start;
    size_t  gap_end;
} gapbuff;

int
buff_is_gap(gapbuff *buff, size_t pos)
{
    if( pos < buff->gap_start ||
        pos >= buff->gap_end)
    {
        return(0);
    }
    return(1);
}

void
buff_insert(gapbuff *buff, char c)
{
    /* Grow the buffer if the gap end is reached */
    if(buff->gap_start == buff->gap_end)
    {
        size_t more_size = 10;
        char *newbuff = ez_mem_alloc((buff->size + more_size)*sizeof(char));

        for(size_t i = 0;
            i <= buff->gap_start;
            ++i)
        {
            newbuff[i] = buff->content[i];
        }
        for(size_t i = buff->gap_end;
            i < buff->size;
            ++i)
        {
            newbuff[i + more_size] = buff->content[i];
        }

        ez_mem_free(buff->content);
        buff->content = newbuff;
        buff->size += more_size;
        buff->gap_end += more_size;
    }

    /* Insert character */
    ez_assert(buff->gap_start < buff->gap_end);
    buff->content[buff->gap_start] = c;
    ++buff->gap_start;
}

void
buff_move_gap(gapbuff *buff, size_t pos)
{
    size_t gap_len = buff->gap_end - buff->gap_start;

    /*
     * Move gap to the left
     *
     * 0123456789abc
     * hello$$ world
     *  ^ pos 1
     * h$$ello world
     */
    if(pos < buff->gap_start)
    {
        for(int64_t i = buff->gap_start - 1;
            i >= (int64_t)pos;
            --i)
        {
            buff->content[i + gap_len] = buff->content[i];
        }
        buff->gap_start = pos;
        buff->gap_end = buff->gap_start + gap_len;
    }

    /*
     * Move gap to the right
     *
     * 0123456789abc
     * hello$$ world
     *            ^ pos 9
     * hello wor$$ld
     */
    if(pos > buff->gap_start)
    {
        for(int64_t i = buff->gap_end;
            i < (int64_t)pos;
            ++i)
        {
            buff->content[i - gap_len] = buff->content[i];
        }
        buff->gap_start = pos - gap_len;
        buff->gap_end = buff->gap_start + gap_len;
    }
}

/*
 * This code maps (cx, cy) to buffer position.
 * (cx, cy) -> pos
 *
 * Position is stored in *pos, return value is 1 if position is found,
 * otherwise return value is 0.
 **/
int
buff_pos(gapbuff *buff, int cx, int cy, size_t *pos)
{
    int findx = 0;
    int findy = 0;
    size_t i;
    for(i = 0;
        i < buff->size;
        ++i)
    {
        if(!buff_is_gap(buff, i))
        {
            if(findx == cx && findy == cy)
            {
                *pos = i;
                return(1);
            }

            if(buff->content[i] == '\n')
            {
                ++findy;
                findx = 0;
                if(findx == cx && findy == cy)
                {
                    do
                    {
                        ++i;
                    }
                    while(i >= buff->gap_start && i < buff->gap_end);
                    *pos = i;
                    return(1);
                }
            }
            else
            {
                ++findx;
            }
        }
    }

    return(0);
}

/******************************************************************************/
/**                                  EDITOR                                  **/
/******************************************************************************/

#define DEBUG_SECTION_HEIGHT 5
#define COMMAND_BAR_HEIGHT 2
#define EDITOR_START_Y 1

typedef struct
editor
{
    editor_mode mode;
    int x;
    int y;
    int cols;
    int rows;
    int cx;
    int cy;
    int coloffset;
    int rowoffset;
    char *file_name;
    size_t file_name_len;
} editor;

void
ed_cursor_move_left(editor *ed)
{
    --ed->cx;
    if(ed->cx < 0)
    {
        ed->coloffset -= 0 - ed->cx;
        ed->coloffset = ez_max(0, ed->coloffset);
        ed->cx = 0;
    }
}

void
ed_cursor_move_right(editor *ed)
{
    ++ed->cx;
    if(ed->cx > ed->cols - 1)
    {
        ed->coloffset += ed->cx - (ed->cols - 1);
        ed->cx = ed->cols - 1;
    }
}

void
ed_cursor_move_up(editor *ed)
{
    --ed->cy;
    if(ed->cy < 0)
    {
        ed->rowoffset -= 0 - ed->cy;
        ed->rowoffset = ez_max(0, ed->rowoffset);
        ed->cy = 0;
    }
}

void
ed_cursor_move_down(editor *ed)
{
    ++ed->cy;
    if(ed->cy > ed->rows - 2)
    {
        ed->rowoffset += ed->cy - (ed->rows - 2);
        ed->cy = ed->rows - 2;
    }
}

int
get_indent_level(gapbuff *buff, int row)
{
    int indent;
    size_t pos;

    indent = 0;
    if(buff_pos(buff, 0, row, &pos))
    {
        while(pos < buff->size && buff->content[pos] == ' ')
        {
            ++indent;
            do
            {
                ++pos;
            }
            while(buff_is_gap(buff, pos));
        }
    }

    return(indent);
}

void
insert_new_line(editor *ed, gapbuff *buff)
{
    int indent;
    int i;

    indent = get_indent_level(buff, ed->cy);

    buff_insert(buff, '\n');
    ed_cursor_move_down(ed);
    ed->cx = 0;

    for(i = 0;
        i < indent;
        ++i)
    {
        buff_insert(buff, ' ');
        ed_cursor_move_right(ed);
    }
}

char **
parse_args(int *argc)
{
    char **argv;

    char *cmd_line;
    int argv_cap;

    char *start;
    int len;

    char *ptr;

    cmd_line = GetCommandLineA();

    argv_cap = 4;
    argv = ez_mem_alloc(sizeof(char *)*argv_cap);

    *argc = 0;
    while(*cmd_line)
    {
        switch(*cmd_line)
        {
            case '"':
            {
                ++cmd_line;
                start = cmd_line;
                len = 0;
                while(*cmd_line && *cmd_line != '"')
                {
                    ++cmd_line;
                    ++len;
                }
                ptr = (char *)ez_mem_alloc(len + 1);
                ez_str_copy_max(start, ptr, len);
                ptr[len] = 0;

                if(*argc >= argv_cap)
                {
                    argv_cap *= 2;
                    argv = ez_mem_realloc(argv, sizeof(char *)*argv_cap);
                }
                argv[*argc] = ptr;
                *argc += 1;
            } break;

            case  ' ': case '\t': case '\v':
            case '\n': case '\r': case '\f':
            {
                ++cmd_line;
            } break;

            default:
            {
                start = cmd_line;
                len = 0;
                while(*cmd_line &&
                      *cmd_line !=  ' ' && *cmd_line != '\t' &&
                      *cmd_line != '\v' && *cmd_line != '\n' &&
                      *cmd_line != '\r' && *cmd_line != '\f')
                {
                    ++cmd_line;
                    ++len;
                }
                ptr = (char *)ez_mem_alloc(len + 1);
                ez_str_copy_max(start, ptr, len);
                ptr[len] = 0;

                if(*argc >= argv_cap)
                {
                    argv_cap *= 2;
                    argv = ez_mem_realloc(argv, sizeof(char *)*argv_cap);
                }
                argv[*argc] = ptr;
                *argc += 1;
            } break;
        }
    }

    return(argv);
}

HANDLE stdout;
HANDLE stdin;

void
screen_refresh(
    CHAR_INFO *stdout_buff,
    int cols, int rows,
    editor *ed, gapbuff *buff)
{
    int x;
    int y;

    /* Draw status bar on top */
    y = 0;
    for(x = 0;
        x < ed->cols;
        ++x)
    {
        stdout_buff[(y)*ed->cols + x].Char.AsciiChar = ' ';
        stdout_buff[(y)*ed->cols + x].Attributes     = FG_BLACK|BG_WHITE;
    }

    if(ed->file_name)
    {
        x = (ed->cols - (int)ed->file_name_len)/2;
        char *ptr = ed->file_name;
        while(*ptr)
        {
            stdout_buff[(y)*ed->cols + x++].Char.AsciiChar = *ptr++;
        }
    }

    if(ed->cols >= 50)
    {
#define set_status_bar_char(x, y, c, color)\
        stdout_buff[(y)*ed->cols + x].Char.AsciiChar = c;\
        stdout_buff[(y)*ed->cols + x].Attributes     = color;

        x = 0;
            set_status_bar_char(x, y, CHAR_FULL_BLOCK,   FG_GREEN|BG_BRIGHT_GREEN);
        ++x;set_status_bar_char(x, y, CHAR_FULL_BLOCK,   FG_GREEN|BG_BRIGHT_GREEN);
        ++x;set_status_bar_char(x, y, CHAR_FULL_BLOCK,   FG_GREEN|BG_BRIGHT_GREEN);
        ++x;set_status_bar_char(x, y, CHAR_DARK_SHADE,   FG_GREEN|BG_BRIGHT_GREEN);
        ++x;set_status_bar_char(x, y, CHAR_MEDIUM_SHADE, FG_GREEN|BG_BRIGHT_GREEN);
        ++x;set_status_bar_char(x, y, CHAR_LIGHT_SHADE,  FG_GREEN|BG_BRIGHT_GREEN);
        ++x;set_status_bar_char(x, y, CHAR_FULL_BLOCK,   FG_BRIGHT_GREEN|BG_BRIGHT_CYAN);
        ++x;set_status_bar_char(x, y, CHAR_FULL_BLOCK,   FG_BRIGHT_GREEN|BG_BRIGHT_CYAN);
        ++x;set_status_bar_char(x, y, CHAR_FULL_BLOCK,   FG_BRIGHT_GREEN|BG_BRIGHT_CYAN);
        ++x;set_status_bar_char(x, y, CHAR_FULL_BLOCK,   FG_BRIGHT_GREEN|BG_BRIGHT_CYAN);
        ++x;set_status_bar_char(x, y, CHAR_DARK_SHADE,   FG_BRIGHT_GREEN|BG_BRIGHT_CYAN);
        ++x;set_status_bar_char(x, y, CHAR_MEDIUM_SHADE, FG_BRIGHT_GREEN|BG_BRIGHT_CYAN);
        ++x;set_status_bar_char(x, y, CHAR_LIGHT_SHADE,  FG_BRIGHT_GREEN|BG_BRIGHT_CYAN);
        ++x;set_status_bar_char(x, y, CHAR_FULL_BLOCK,   FG_BRIGHT_CYAN|BG_WHITE);
        ++x;set_status_bar_char(x, y, CHAR_FULL_BLOCK,   FG_BRIGHT_CYAN|BG_WHITE);
        ++x;set_status_bar_char(x, y, CHAR_FULL_BLOCK,   FG_BRIGHT_CYAN|BG_WHITE);
        ++x;set_status_bar_char(x, y, CHAR_FULL_BLOCK,   FG_BRIGHT_CYAN|BG_WHITE);
        ++x;set_status_bar_char(x, y, CHAR_FULL_BLOCK,   FG_BRIGHT_CYAN|BG_WHITE);
        ++x;set_status_bar_char(x, y, CHAR_FULL_BLOCK,   FG_BRIGHT_CYAN|BG_WHITE);
        ++x;set_status_bar_char(x, y, CHAR_FULL_BLOCK,   FG_BRIGHT_CYAN|BG_WHITE);
        ++x;set_status_bar_char(x, y, CHAR_FULL_BLOCK,   FG_BRIGHT_CYAN|BG_WHITE);
        ++x;set_status_bar_char(x, y, CHAR_FULL_BLOCK,   FG_BRIGHT_CYAN|BG_WHITE);
        ++x;set_status_bar_char(x, y, CHAR_FULL_BLOCK,   FG_BRIGHT_CYAN|BG_WHITE);
        ++x;set_status_bar_char(x, y, CHAR_FULL_BLOCK,   FG_BRIGHT_CYAN|BG_WHITE);
        ++x;set_status_bar_char(x, y, CHAR_DARK_SHADE,   FG_BRIGHT_CYAN|BG_WHITE);
        ++x;set_status_bar_char(x, y, CHAR_MEDIUM_SHADE, FG_BRIGHT_CYAN|BG_WHITE);
        ++x;set_status_bar_char(x, y, CHAR_LIGHT_SHADE,  FG_BRIGHT_CYAN|BG_WHITE);

        x = ed->cols - 1;
            set_status_bar_char(x, y, CHAR_FULL_BLOCK,   FG_GREEN|BG_BRIGHT_GREEN);
        --x;set_status_bar_char(x, y, CHAR_FULL_BLOCK,   FG_GREEN|BG_BRIGHT_GREEN);
        --x;set_status_bar_char(x, y, CHAR_FULL_BLOCK,   FG_GREEN|BG_BRIGHT_GREEN);
        --x;set_status_bar_char(x, y, CHAR_DARK_SHADE,   FG_GREEN|BG_BRIGHT_GREEN);
        --x;set_status_bar_char(x, y, CHAR_MEDIUM_SHADE, FG_GREEN|BG_BRIGHT_GREEN);
        --x;set_status_bar_char(x, y, CHAR_LIGHT_SHADE,  FG_GREEN|BG_BRIGHT_GREEN);
        --x;set_status_bar_char(x, y, CHAR_FULL_BLOCK,   FG_BRIGHT_GREEN|BG_BRIGHT_CYAN);
        --x;set_status_bar_char(x, y, CHAR_FULL_BLOCK,   FG_BRIGHT_GREEN|BG_BRIGHT_CYAN);
        --x;set_status_bar_char(x, y, CHAR_FULL_BLOCK,   FG_BRIGHT_GREEN|BG_BRIGHT_CYAN);
        --x;set_status_bar_char(x, y, CHAR_FULL_BLOCK,   FG_BRIGHT_GREEN|BG_BRIGHT_CYAN);
        --x;set_status_bar_char(x, y, CHAR_DARK_SHADE,   FG_BRIGHT_GREEN|BG_BRIGHT_CYAN);
        --x;set_status_bar_char(x, y, CHAR_MEDIUM_SHADE, FG_BRIGHT_GREEN|BG_BRIGHT_CYAN);
        --x;set_status_bar_char(x, y, CHAR_LIGHT_SHADE,  FG_BRIGHT_GREEN|BG_BRIGHT_CYAN);
        --x;set_status_bar_char(x, y, CHAR_FULL_BLOCK,   FG_BRIGHT_CYAN|BG_WHITE);
        --x;set_status_bar_char(x, y, CHAR_FULL_BLOCK,   FG_BRIGHT_CYAN|BG_WHITE);
        --x;set_status_bar_char(x, y, CHAR_FULL_BLOCK,   FG_BRIGHT_CYAN|BG_WHITE);
        --x;set_status_bar_char(x, y, CHAR_FULL_BLOCK,   FG_BRIGHT_CYAN|BG_WHITE);
        --x;set_status_bar_char(x, y, CHAR_FULL_BLOCK,   FG_BRIGHT_CYAN|BG_WHITE);
        --x;set_status_bar_char(x, y, CHAR_FULL_BLOCK,   FG_BRIGHT_CYAN|BG_WHITE);
        --x;set_status_bar_char(x, y, CHAR_FULL_BLOCK,   FG_BRIGHT_CYAN|BG_WHITE);
        --x;set_status_bar_char(x, y, CHAR_FULL_BLOCK,   FG_BRIGHT_CYAN|BG_WHITE);
        --x;set_status_bar_char(x, y, CHAR_FULL_BLOCK,   FG_BRIGHT_CYAN|BG_WHITE);
        --x;set_status_bar_char(x, y, CHAR_FULL_BLOCK,   FG_BRIGHT_CYAN|BG_WHITE);
        --x;set_status_bar_char(x, y, CHAR_FULL_BLOCK,   FG_BRIGHT_CYAN|BG_WHITE);
        --x;set_status_bar_char(x, y, CHAR_DARK_SHADE,   FG_BRIGHT_CYAN|BG_WHITE);
        --x;set_status_bar_char(x, y, CHAR_MEDIUM_SHADE, FG_BRIGHT_CYAN|BG_WHITE);
        --x;set_status_bar_char(x, y, CHAR_LIGHT_SHADE,  FG_BRIGHT_CYAN|BG_WHITE);
#undef set_status_bar_char
    }

    /* Draw buffer */
    for(y = 0;
        y < ed->rows;
        ++y)
    {
        for(x = 0;
            x < ed->cols;
            ++x)
        {
            stdout_buff[(y + ed->y)*ed->cols + (x + ed->x)].Attributes = FG_BRIGHT_WHITE|BG_BLACK;
            stdout_buff[(y + ed->y)*ed->cols + (x + ed->x)].Char.AsciiChar = ' ';
        }
    }
    x = 0;
    y = 0;
    for(size_t i = 0;
        i < buff->size;
        ++i)
    {
        if( i < buff->gap_start ||
            i >= buff->gap_end)
        {
            if(buff->content[i] == '\n')
            {
                ++y;
                x = 0;
                if(y + ed->y - ed->rowoffset >= ed->rows)
                {
                    break;
                }
                else if(y - ed->rowoffset >= 0)
                {
                    stdout_buff[((y + ed->y)-ed->rowoffset)*ed->cols + (0 + ed->x)].Char.AsciiChar = ' ';
                }
            }
            else
            {
                if(y >= ed->rowoffset && x >= ed->coloffset && x < ed->coloffset + ed->cols)
                {
                    stdout_buff[((y + ed->y)-ed->rowoffset)*ed->cols + ((x + ed->x)-ed->coloffset)].Char.AsciiChar = buff->content[i];
                }
                ++x;
            }
        }
    }
    for(y = y + 1;
        y < ed->rows;
        ++y)
    {
        stdout_buff[(y + ed->y)*ed->cols + (0 + ed->x)].Char.AsciiChar = '~';
    }

    /* Draw cursor */
    if(ed->mode == MODE_NORMAL)
    {
        stdout_buff[(ed->cy + ed->y)*ed->cols + (ed->cx + ed->x)].Attributes = BG_BRIGHT_GREEN|FG_BLACK;
    }
    else
    {
        stdout_buff[(ed->cy + ed->y)*ed->cols + (ed->cx + ed->x)].Attributes = BG_RED|FG_BRIGHT_WHITE;
    }

#if 1
    /* DEBUG: Write gap buffer in text$$$text format */
    y = (rows - DEBUG_SECTION_HEIGHT - COMMAND_BAR_HEIGHT);
    for(size_t i = 0;
        i < buff->size && i < DEBUG_SECTION_HEIGHT*cols;
        ++i)
    {
        stdout_buff[y*cols + i].Attributes = FG_BRIGHT_BLACK|BG_BLACK;
        if( i < buff->gap_start ||
            i >= buff->gap_end)
        {
            stdout_buff[y*cols + i].Char.AsciiChar = buff->content[i];
        }
        else
        {
            stdout_buff[y*cols + i].Char.AsciiChar = '$';
        }
    }
#endif

    /* Draw command bar to the bottom */
    y = rows - COMMAND_BAR_HEIGHT;
    for(x = 0;
        x < cols;
        ++x)
    {
        stdout_buff[y*cols + x].Char.AsciiChar = ' ';
        stdout_buff[y*cols + x].Attributes     = FG_BLACK|BG_BRIGHT_WHITE;
    } 
    y = rows - COMMAND_BAR_HEIGHT + 1;
    for(x = 0;
        x < cols;
        ++x)
    {
        stdout_buff[y*cols + x].Char.AsciiChar = ' ';
        stdout_buff[y*cols + x].Attributes     = FG_BRIGHT_WHITE|BG_BLACK;
    }
    if(ed->mode == MODE_INSERT)
    {
        char *str_mode = "--INSERT--";
        size_t str_mode_len = ez_str_len(str_mode);
        y = rows - COMMAND_BAR_HEIGHT + 1;
        for(x = 0;
            x < (int)ez_min(cols, str_mode_len);
            ++x)
        {
            stdout_buff[y*cols + x].Char.AsciiChar = str_mode[x];
        }
    }

    /* Write screen buffer to the console */
    COORD stdout_coord;
    SMALL_RECT write_region;
    stdout_coord.X = (SHORT)cols;
    stdout_coord.Y = (SHORT)rows;
    COORD coord;
    coord.X = 0;
    coord.Y = 0;
    write_region.Top    = 0;
    write_region.Left   = 0;
    write_region.Right  = (SHORT)cols;
    write_region.Bottom = (SHORT)rows;
    WriteConsoleOutput(
        stdout, stdout_buff, stdout_coord,
        coord, &write_region);
}

void
main(void)
{
    editor ed = {0};

    /* Create a new screen buffer and set it as active */
    stdout = GetStdHandle(STD_OUTPUT_HANDLE);
    stdin  = GetStdHandle(STD_INPUT_HANDLE);
    if(stdout == INVALID_HANDLE_VALUE ||
       stdin  == INVALID_HANDLE_VALUE)
    {
        ExitProcess(1);
    }
    int cols;
    int rows;
    COORD coord;
    CONSOLE_SCREEN_BUFFER_INFO sbinfo = {0};
    if(!GetConsoleScreenBufferInfo(stdout, &sbinfo))
    {
        ExitProcess(2);
    }
    stdout = CreateConsoleScreenBuffer(
        GENERIC_READ|GENERIC_WRITE, 0, 0,
        CONSOLE_TEXTMODE_BUFFER, 0);
    cols = sbinfo.srWindow.Right  - sbinfo.srWindow.Left + 1;
    rows = sbinfo.srWindow.Bottom - sbinfo.srWindow.Top + 1;
    ed.x    = 0;
    ed.y    = EDITOR_START_Y;
    ed.cols = cols;
    ed.rows = rows - DEBUG_SECTION_HEIGHT - COMMAND_BAR_HEIGHT;
    coord.X = (SHORT)cols;
    coord.Y = (SHORT)rows;
    if(!SetConsoleScreenBufferSize(stdout, coord))
    {
        ExitProcess(3);
    }
    size_t buffsize = cols*rows;
    CHAR_INFO *stdout_buff = ez_mem_alloc(buffsize*sizeof(CHAR_INFO));
    if(!stdout_buff)
    {
        ExitProcess(88);
    }
    for(size_t i = 0;
        i < buffsize;
        ++i)
    {
        stdout_buff[i].Char.AsciiChar = ' ';
        stdout_buff[i].Attributes     = FG_BRIGHT_WHITE|BG_BLACK;
    }
    if(!SetConsoleActiveScreenBuffer(stdout))
    {
        ExitProcess(4);
    }
    SetConsoleTitleA("ezedit");

    /* Enable raw mode */
    DWORD stdin_mode;
    if(!GetConsoleMode(stdin, &stdin_mode))
    {
        ExitProcess(5);
    }
    stdin_mode &= ~(ENABLE_ECHO_INPUT|ENABLE_LINE_INPUT|
                    ENABLE_PROCESSED_INPUT|ENABLE_WINDOW_INPUT);
    if(!SetConsoleMode(stdin, stdin_mode))
    {
        ExitProcess(5);
    }

    /* Set console position to (0, 0) and hide it, we have our own cursor */
    coord.X = 0;
    coord.Y = 0;
    SetConsoleCursorPosition(stdout, coord);
    CONSOLE_CURSOR_INFO cursinfo = {0};
    cursinfo.dwSize = 100;
    cursinfo.bVisible = 0;
    SetConsoleCursorInfo(stdout, &cursinfo);

    /* Open file */
    ed.file_name = 0;
    ed.file_name_len = 0;
    size_t file_size;

    /* Parse arguments */
    int argc;
    char **argv = 0;
    argv = parse_args(&argc);
    if(argc > 1)
    {
        ed.file_name = argv[1];
    }

    /* Create gap buffer */
    gapbuff buff = {0};
    if(ed.file_name)
    {
        char *file_content = ez_file_read_text(ed.file_name, &file_size);
        buff.size = file_size + 5;
        buff.content = ez_mem_alloc(buff.size*sizeof(char));
        size_t buff_index = 0;
        for(size_t i = 0;
            i < file_size;
            ++i)
        {
            if(file_content[i] == '\r')
            {
                continue;
            }
            buff.content[buff_index++] = file_content[i];
        }
        buff.gap_start = buff_index;
        buff.gap_end   = buff.size;
        ez_file_free(file_content);

        ed.file_name_len = ez_str_len(ed.file_name);
    }
    else
    {
        buff.size = 10;
        buff.content = ez_mem_alloc(buff.size*sizeof(char));
        buff.gap_start = 0;
        buff.gap_end   = buff.size;
    }

    /* Main loop */
    int running = 1;
    ed.cx = 0;
    ed.cy = 0;
    ed.coloffset = 0;
    ed.rowoffset = 0;
    ed.mode = MODE_NORMAL;
    while(running)
    {
        screen_refresh(stdout_buff, cols, rows, &ed, &buff);

        /* Get input */
        char c = 0;
        DWORD num_read = 0;
    
        INPUT_RECORD input_rec = {0};
        int stop_input_reading = 0;
        do
        {
            if(!ReadConsoleInput(stdin, &input_rec, 1, &num_read))
            {
                ExitProcess(99);
            }
            if( num_read == 1 &&
                input_rec.EventType == KEY_EVENT &&
                input_rec.Event.KeyEvent.bKeyDown)
            {
                if(input_rec.Event.KeyEvent.dwControlKeyState & ENHANCED_KEY)
                {
                    switch(input_rec.Event.KeyEvent.wVirtualKeyCode)
                    {
                        case VK_DELETE:
                        {
                            c = 0x7f;
                            stop_input_reading = 1;
                        } break;

                        default:
                        {
                            c = 0;
                        } break;
                    }
                }
                else
                {
                    switch(input_rec.Event.KeyEvent.wVirtualKeyCode)
                    {
                        case VK_ESCAPE:
                        {
                            c = 0x1b;
                            stop_input_reading = 1;
                        } break;

                        default:
                        {
                            c = (char)input_rec.Event.KeyEvent.uChar.AsciiChar;
                            stop_input_reading = 1;
                        } break;
                    }
                }
            }
            else if(num_read == 1 &&
                    input_rec.EventType == WINDOW_BUFFER_SIZE_EVENT)
            {
                cols = (int)input_rec.Event.WindowBufferSizeEvent.dwSize.X;
                rows = (int)input_rec.Event.WindowBufferSizeEvent.dwSize.Y;
                if(!SetConsoleScreenBufferSize(stdout, input_rec.Event.WindowBufferSizeEvent.dwSize))
                {
                    ExitProcess(3);
                }
                buffsize = cols*rows;
                stdout_buff = ez_mem_realloc(stdout_buff, buffsize*sizeof(CHAR_INFO));
                if(!stdout_buff)
                {
                    ExitProcess(88);
                }
                for(size_t i = 0;
                    i < buffsize;
                    ++i)
                {
                    stdout_buff[i].Char.AsciiChar = ' ';
                    stdout_buff[i].Attributes     = FG_BRIGHT_WHITE|BG_BLACK;
                }

                ed.cols = cols;
                ed.rows = rows - DEBUG_SECTION_HEIGHT - COMMAND_BAR_HEIGHT;

                screen_refresh(stdout_buff, cols, rows, &ed, &buff);
            }
        }
        while(!stop_input_reading);

        /* Process input */
        if(ed.mode == MODE_NORMAL)
        {
            switch(c)
            {
                case CTRL_KEY('q'):
                {
                    running = 0;
                } break;

                case CTRL_KEY('s'):
                {
                    /*
                     * TODO OPTIMIZE
                     * This should be optimized because allocating double the size of
                     * the buffer every time could be bad (maybe??)
                     */
                    char *new_content = ez_mem_alloc(buff.size*2*sizeof(char));
                    size_t new_content_index = 0;
                    for(size_t i = 0;
                        i < buff.size;
                        ++i)
                    {
                        if( i < buff.gap_start ||
                            i >= buff.gap_end)
                        {
                            if(buff.content[i] == '\n')
                            {
                                new_content[new_content_index++] = '\r';
                                new_content[new_content_index++] = '\n';
                            }
                            else
                            {
                                new_content[new_content_index++] = buff.content[i];
                            }
                        }
                    }
                    ez_file_write(ed.file_name, (void *)new_content, new_content_index);
                    ez_mem_free(new_content);
                } break;

                case 'h':
                {
                    ed_cursor_move_left(&ed);
                } break;

                case 'l':
                {
                    size_t pos;
                    if(buff_pos(&buff, ed.cx + ed.coloffset + 1, ed.cy + ed.rowoffset, &pos))
                    {
                        if(ez_char_is_print(buff.content[pos]))
                        {
                            ed_cursor_move_right(&ed);
                        }
                    }
                } break;

                case 'k':
                {
                    ed_cursor_move_up(&ed);
                } break;

                case 'j':
                {
                    size_t pos;
                    if(buff_pos(&buff, 0 + ed.coloffset, ed.cy + ed.rowoffset + 1, &pos))
                    {
                        ed_cursor_move_down(&ed);
                    }
                } break;

                case 'i':
                {
                    size_t pos;
                    if(buff_pos(&buff, ed.cx + ed.coloffset, ed.cy + ed.rowoffset, &pos))
                    {
                        buff_move_gap(&buff, pos);
                        ed.mode = MODE_INSERT;
                    }
                } break;

                case 'a':
                {
                    size_t pos;
                    if(buff_pos(&buff, ed.cx + ed.coloffset + 1, ed.cy + ed.rowoffset, &pos))
                    {
                        ed_cursor_move_right(&ed);
                        buff_move_gap(&buff, pos);
                        ed.mode = MODE_INSERT;
                    }
                } break;

                case 's':
                {
                    size_t pos;
                    if(buff_pos(&buff, ed.cx + ed.coloffset, ed.cy + ed.rowoffset, &pos))
                    {
                        buff_move_gap(&buff, pos);
                        if(buff.gap_end < buff.size)
                        {
                            ++buff.gap_end;
                        }
                        ed.mode = MODE_INSERT;
                    }
                } break;

                case 'x':
                {
                    size_t pos;
                    if(buff_pos(&buff, ed.cx + ed.coloffset, ed.cy + ed.rowoffset, &pos))
                    {
                        buff_move_gap(&buff, pos);
                        if(buff.gap_end < buff.size)
                        {
                            ++buff.gap_end;
                        }
                    }
                } break;

                case 'I':
                {
                    size_t pos;
                    if(buff_pos(&buff, 0, ed.cy + ed.rowoffset, &pos))
                    {
                        ed.coloffset = 0;
                        ed.cx = 0;
                        while(pos < buff.size &&
                              buff.content[pos] != '\n' &&
                              !ez_char_is_print(buff.content[pos]))
                        {
                            do
                            {
                                ++pos;
                            }
                            while(buff_is_gap(&buff, pos));

                            if(!buff_is_gap(&buff, pos))
                            {
                                ed_cursor_move_right(&ed);
                            }
                        }
                        buff_move_gap(&buff, pos);
                        ed.mode = MODE_INSERT;
                    }
                } break;

                case 'A':
                {
                    size_t pos;
                    if(buff_pos(&buff, 0, ed.cy + ed.rowoffset, &pos))
                    {
                        ed.coloffset = 0;
                        ed.cx = 0;
                        while(pos < buff.size && buff.content[pos] != '\n')
                        {
                            do
                            {
                                ++pos;
                            }
                            while(buff_is_gap(&buff, pos));

                            if(!buff_is_gap(&buff, pos))
                            {
                                ed_cursor_move_right(&ed);
                            }
                        }
                        buff_move_gap(&buff, pos);
                        ed.mode = MODE_INSERT;
                    }
                } break;

                case 'S':
                {
                    size_t pos;
                    int indent;
                    int i;

                    indent = get_indent_level(&buff, ed.cy + ed.rowoffset);

                    if(buff_pos(&buff, 0, ed.cy + ed.rowoffset, &pos))
                    {
                        ed.coloffset = 0;
                        ed.cx = 0;
                        while(pos < buff.size && buff.content[pos] != '\n')
                        {
                            do
                            {
                                ++pos;
                            }
                            while(buff_is_gap(&buff, pos));

                            if(!buff_is_gap(&buff, pos))
                            {
                                ed_cursor_move_right(&ed);
                            }
                        }
                        buff_move_gap(&buff, pos);

                        while(buff.gap_start > 0 &&
                              buff.content[buff.gap_start-1] != '\n')
                        {
                            if(buff.gap_start > 0)
                            {
                                --buff.gap_start;
                                ed_cursor_move_left(&ed);
                            }
                        }
                        for(i = 0;
                            i < indent;
                            ++i)
                        {
                            buff_insert(&buff, ' ');
                            ed_cursor_move_right(&ed);
                        }

                        ed.mode = MODE_INSERT;
                    }
                } break;

                case 'X':
                {
                    size_t pos;
                    if(ed.cx + ed.coloffset > 0)
                    {
                        if(buff_pos(&buff, ed.cx + ed.coloffset, ed.cy + ed.rowoffset, &pos))
                        {
                            buff_move_gap(&buff, pos);
                            if(buff.gap_start > 0)
                            {
                                --buff.gap_start;
                                ed_cursor_move_left(&ed);
                            }
                        }
                    }
                } break;

                case 'o':
                {
                    size_t pos;
                    if(buff_pos(&buff, 0, ed.cy + ed.rowoffset, &pos))
                    {
                        ed.coloffset = 0;
                        ed.cx = 0;
                        while(pos < buff.size && buff.content[pos] != '\n')
                        {
                            do
                            {
                                ++pos;
                            }
                            while(buff_is_gap(&buff, pos));

                            if(!buff_is_gap(&buff, pos))
                            {
                                ed_cursor_move_right(&ed);
                            }
                        }
                        buff_move_gap(&buff, pos);
                        insert_new_line(&ed, &buff);
                        ed.mode = MODE_INSERT;
                    }
                } break;

                case 'O':
                {
                    size_t pos;
                    if(ed.cy + ed.rowoffset > 0)
                    {
                        if(buff_pos(&buff, 0, ed.cy + ed.rowoffset - 1, &pos))
                        {
                            ed.coloffset = 0;
                            ed.cx = 0;
                            while(pos < buff.size && buff.content[pos] != '\n')
                            {
                                do
                                {
                                    ++pos;
                                }
                                while(buff_is_gap(&buff, pos));

                                if(!buff_is_gap(&buff, pos))
                                {
                                    ed_cursor_move_right(&ed);
                                }
                            }
                            buff_move_gap(&buff, pos);
                            insert_new_line(&ed, &buff);
                            ed_cursor_move_up(&ed);
                            ed.mode = MODE_INSERT;
                        }
                    }
                    else
                    {
                        int indent = get_indent_level(&buff, 0);
                        int i;

                        if(buff_pos(&buff, 0, 0, &pos))
                        {
                            ed.coloffset = 0;
                            buff_move_gap(&buff, pos);
                            buff_insert(&buff, '\n');
                            buff_move_gap(&buff, pos);
                            ed.cx = 0;
                            for(i = 0;
                                i < indent;
                                ++i)
                            {
                                buff_insert(&buff, ' ');
                                ed_cursor_move_right(&ed);
                            }
                            ed.mode = MODE_INSERT;
                        }
                    }
                } break;

                case 'C':
                {
                    size_t pos_start, pos_end;
                    if(buff_pos(&buff, ed.cx + ed.coloffset, ed.cy + ed.rowoffset, &pos_start))
                    {
                        pos_end = pos_start;
                        while(pos_end < buff.size && buff.content[pos_end] != '\n')
                        {
                            do
                            {
                                ++pos_end;
                            }
                            while(buff_is_gap(&buff, pos_end));
                        }
                        buff_move_gap(&buff, pos_end);
                        while(buff.gap_start > pos_start)
                        {
                            --buff.gap_start;
                        }
                        ed.mode = MODE_INSERT;
                    }
                } break;

                case '^':
                {
                    size_t pos;
                    if(buff_pos(&buff, 0, ed.cy + ed.rowoffset, &pos))
                    {
                        ed.coloffset = 0;
                        ed.cx = 0;
                        while(pos < buff.size &&
                              buff.content[pos] != '\n' &&
                              !ez_char_is_print(buff.content[pos]))
                        {
                            do
                            {
                                ++pos;
                            }
                            while(buff_is_gap(&buff, pos));

                            if(!buff_is_gap(&buff, pos))
                            {
                                ed_cursor_move_right(&ed);
                            }
                        }
                    }
                } break;

                case '$':
                {
                    size_t pos;
                    if(buff_pos(&buff, 0, ed.cy + ed.rowoffset, &pos))
                    {
                        ed.coloffset = 0;
                        ed.cx = 0;
                        while(pos < buff.size && buff.content[pos] != '\n')
                        {
                            do
                            {
                                ++pos;
                            }
                            while(buff_is_gap(&buff, pos));

                            if(!buff_is_gap(&buff, pos))
                            {
                                ed_cursor_move_right(&ed);
                            }
                        }
                    }
                } break;

                case '{':
                {
                    if(ed.cy + ed.rowoffset > 0)
                    {
                        ed.cx = 0;
                        ed_cursor_move_up(&ed);
                        size_t pos, pos_start;
                        if(buff_pos(&buff, ed.cx, ed.cy + ed.rowoffset, &pos_start))
                        {
                            pos = pos_start;
                            while(pos > 0)
                            {
                                if(!buff_is_gap(&buff, pos))
                                {
                                    if(buff.content[pos] == '\n')
                                    {
                                        /* TODO: What if pos-1 is inside gap? I must check pos-gap_len */
                                        ed_cursor_move_up(&ed);
                                        if( buff.content[pos] == '\n' &&
                                            buff.content[pos-1] == '\n' &&
                                            !buff_is_gap(&buff, pos-1))
                                        {
                                            if(pos == pos_start)
                                            {
                                                ed_cursor_move_down(&ed);
                                            }
                                            break;
                                        }
                                    }
                                }
                                --pos;
                            }
                        }
                    }
                    else
                    {
                        ed.cx = 0;
                    }
                } break;

                case '}':
                {
                    size_t pos;
                    size_t gap_len = buff.gap_end - buff.gap_start;
                    size_t inc = 1;
                    if(buff_pos(&buff, 0, ed.cy + ed.rowoffset, &pos))
                    {
                        ed.cx = 0;
                        while(pos < buff.size)
                        {
                            if(!buff_is_gap(&buff, pos))
                            {
                                if(buff.content[pos] == '\n')
                                {
                                    ed_cursor_move_down(&ed);
                                }
                                if(buff_is_gap(&buff, pos+inc))
                                {
                                    inc = gap_len;
                                }
                                if(buff.content[pos] == '\n' &&
                                   buff.content[pos+inc] == '\n' &&
                                   !buff_is_gap(&buff, pos+inc))
                                {
                                    break;
                                }
                            }
                            ++pos;
                        }
                    }
                } break;

                default:
                {
                } break;
            }
        }
        else if(ed.mode == MODE_INSERT)
        {
            switch(c)
            {
                /* Escape (ESC)  & Ctrl+Q */
                case 0x1b:
                case CTRL_KEY('q'):
                {
                    ed_cursor_move_left(&ed);
                    ed.mode = MODE_NORMAL;
                } break;

                case '\t':
                {
                    /* TODO: Variable-sized tab */
                    buff_insert(&buff, ' ');
                    ed_cursor_move_right(&ed);
                    buff_insert(&buff, ' ');
                    ed_cursor_move_right(&ed);
                    buff_insert(&buff, ' ');
                    ed_cursor_move_right(&ed);
                    buff_insert(&buff, ' ');
                    ed_cursor_move_right(&ed);
                } break;

                /* Space */
                case ' ':
                {
                    buff_insert(&buff, c);
                    ed_cursor_move_right(&ed);
                } break;

                /* Printable characters (non-whitespace) */
                case '!':case '"':case '#':case '$':case '%':case '&':
                case '\'':case '(':case ')':case '*':case '+':case ',':case '-':
                case '.':case '/':case '0':case '1':case '2':case '3':case '4':
                case '5':case '6':case '7':case '8':case '9':case ':':case ';':
                case '<':case '=':case '>':case '?':case '@':case 'A':case 'B':
                case 'C':case 'D':case 'E':case 'F':case 'G':case 'H':case 'I':
                case 'J':case 'K':case 'L':case 'M':case 'N':case 'O':case 'P':
                case 'Q':case 'R':case 'S':case 'T':case 'U':case 'V':case 'W':
                case 'X':case 'Y':case 'Z':case '[':case '\\':case ']':case '^':
                case '_':case '´':case 'a':case 'b':case 'c':case 'd':case 'e':
                case 'f':case 'g':case 'h':case 'i':case 'j':case 'k':case 'l':
                case 'm':case 'n':case 'o':case 'p':case 'q':case 'r':case 's':
                case 't':case 'u':case 'v':case 'w':case 'x':case 'y':case 'z':
                case '{':case '|':case '}':case '~':
                {
                    buff_insert(&buff, c);
                    ed_cursor_move_right(&ed);
                } break;

                /* Backspace */
                case 0x08:
                {
                    if(buff.gap_start > 0)
                    {
                        --buff.gap_start;
                        if(ed.cx > 0)
                        {
                            ed_cursor_move_left(&ed);
                        }
                        else
                        {
                            ed_cursor_move_up(&ed);

                            size_t pos;
                            if(buff_pos(&buff, 0, ed.cy + ed.rowoffset, &pos))
                            {
                                ed.coloffset = 0;
                                ed.cx = 0;
                                while(buff.content[pos] && buff.content[pos] != '\n')
                                {
                                    ++pos;
                                    ed_cursor_move_right(&ed);
                                }
                                buff_move_gap(&buff, pos);
                            }
                        }
                    }
                } break;

                /* Delete */
                case 0x7f:
                {
                    if(buff.gap_end + 1 <= buff.size)
                    {
                        ++buff.gap_end;
                    }
                } break;

                /* New line */
                case '\r':
                {
                    insert_new_line(&ed, &buff);
                } break;

                default:
                {
                } break;
            }
        }
    }

    /* Quit */
    ExitProcess(0);
}
