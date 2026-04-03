/*
 * ansi.h
 *
 *  Created on: Oct 2, 2025
 *      Author: Daruin Solano
 */

#ifndef ANSI_H_
#define ANSI_H_

/*
 * ANSI Escape Sequences for Terminal Formatting
 * Works in most modern terminals, UART consoles, and debuggers
 * Author: Daruin Solano
 */

//
// Reset
//
#define ANSI_RESET          "\033[0m"

//
// Standard Foreground Colors
//
// ANSI color codes (non-bold)

#define ANSI_BLACK          "\033[30m"
#define ANSI_RED            "\033[31m"
#define ANSI_GREEN          "\033[32m"
#define ANSI_YELLOW         "\033[33m"
#define ANSI_BLUE           "\033[34m"
#define ANSI_MAGENTA        "\033[35m"
#define ANSI_CYAN           "\033[36m"
#define ANSI_WHITE          "\033[37m"

//
// Bright Foreground Colors
//
#define ANSI_BRIGHT_BLACK   "\033[90m"
#define ANSI_BRIGHT_RED     "\033[91m"
#define ANSI_BRIGHT_GREEN   "\033[92m"
#define ANSI_BRIGHT_YELLOW  "\033[93m"
#define ANSI_BRIGHT_BLUE    "\033[94m"
#define ANSI_BRIGHT_MAGENTA "\033[95m"
#define ANSI_BRIGHT_CYAN    "\033[96m"
#define ANSI_BRIGHT_WHITE   "\033[97m"

//
// Background Colors
//
#define ANSI_BG_BLACK       "\033[40m"
#define ANSI_BG_RED         "\033[41m"
#define ANSI_BG_GREEN       "\033[42m"
#define ANSI_BG_YELLOW      "\033[43m"
#define ANSI_BG_BLUE        "\033[44m"
#define ANSI_BG_MAGENTA     "\033[45m"
#define ANSI_BG_CYAN        "\033[46m"
#define ANSI_BG_WHITE       "\033[47m"

//
// Bright Background Colors
//
#define ANSI_BG_BRIGHT_BLACK   "\033[100m"
#define ANSI_BG_BRIGHT_RED     "\033[101m"
#define ANSI_BG_BRIGHT_GREEN   "\033[102m"
#define ANSI_BG_BRIGHT_YELLOW  "\033[103m"
#define ANSI_BG_BRIGHT_BLUE    "\033[104m"
#define ANSI_BG_BRIGHT_MAGENTA "\033[105m"
#define ANSI_BG_BRIGHT_CYAN    "\033[106m"
#define ANSI_BG_BRIGHT_WHITE   "\033[107m"

//
// Text Attributes
//
#define ANSI_BOLD           "\033[1m"
#define ANSI_DIM            "\033[2m"
#define ANSI_ITALIC         "\033[3m"
#define ANSI_UNDERLINE      "\033[4m"
#define ANSI_BLINK          "\033[5m"
#define ANSI_REVERSE        "\033[7m"
#define ANSI_HIDDEN         "\033[8m"
#define ANSI_STRIKETHROUGH  "\033[9m"

//
// Cursor Control
//
#define ANSI_CLEAR_SCREEN   "\033[2J"
#define ANSI_CLEAR_LINE     "\033[2K"
#define ANSI_CURSOR_HOME    "\033[H"
#define ANSI_SAVE_CURSOR    "\033[s"
#define ANSI_RESTORE_CURSOR "\033[u"
#define ANSI_BACKSPACE		"\x1b[D\x1b[K"

// Positioning Macros: Use like printf(ANSI_CURSOR_POS(10,20));
#define ANSI_CURSOR_POS(row,col)  "\033[" #row ";" #col "H"

// Movement
#define ANSI_CURSOR_UP(n)    "\033[" #n "A"
#define ANSI_CURSOR_DOWN(n)  "\033[" #n "B"
#define ANSI_CURSOR_RIGHT(n) "\033[" #n "C"
#define ANSI_CURSOR_LEFT(n)  "\033[" #n "D"


#endif /* ANSI_H_ */
