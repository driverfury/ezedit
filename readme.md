# ezedit

Small vi-like editor for Windows written in ANSI C99.

This editor utilizes [ez.h](https://github.com/driverfury/ez) library.

## Usage

```
./ezedit file.txt
```

## Commands

### Normal mode

- Ctrl+q quit
- Ctrl+s save
- hjkl navigate (move the cursor)
- ^ go to first non-whitespace character of the current line
- $ go to the end of the current line
- i/I enter insert mode (capital I to insert at the end of the line)
- a/A append/append to the end of the line
- c delete line from the cursor and enter insert mode
- {/} go to previous/next block
- o/O new line next/back
- s/S delete current char/line and enter insert mode
- x/X delete current/previous char

### Insert mode

- Ctrl+Q/ESC exit insert mode (go to normal mode)
