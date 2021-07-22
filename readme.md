# ezedit

Small vi-like editor for Windows written in ANSI C99.

This editor utilizes [ez.h library](https://github.com/driverfury/ez).

## Usage

´´´
./ezedit file.txt
´´´

## Commands

### Normal mode

- Ctrl+q quit
- hjkl navigate (move the cursor)
- i/I enter insert mode (capital I to insert at the end of the line)
- a/A append/append to the end of the line
- c delete line from the cursor and enter insert mode
- {/} go to previous/next block

### Insert mode

- Ctrl+q exit insert mode (go to normal mode)
