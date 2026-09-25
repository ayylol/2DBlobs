# 2D Blending Blobs

Blending blobs in 2D written all in C, and accelerated using AVX512.
The renderer is inspired by [Mike Acton's braille ncurses game engine](https://www.altdevarts.com/p/a-simple-main-game-loop-with-ncurses). 

## To run
this will make and launch the project, if you are inside the directory for the project:
`./go.sh`

## Requirements:
- Linux, GCC and Make.
- CPU that supports AVX512.
- unicode font with braille characters in the terminal.
- at least 16:9, 40 character tall terminal.
