/*
	Copyright 2001, 2002 Georges Menie (www.menie.org)
	stdarg version contributed by Christian Ettinger
    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU Lesser General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU Lesser General Public License for more details.
    You should have received a copy of the GNU Lesser General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

/*
	putchar is the only external dependency for this file,
	if you have a working putchar, leave it commented out.
	If not, uncomment the define below and
	replace outbyte(c) by your own function call.
#define putchar(c) outbyte(c)
*/

#include <stdarg.h>
#include <stdint.h>

#ifndef PRINT_BUF_LEN
#define PRINT_BUF_LEN 32
#endif

extern int _write(int file, char *data, int len);
extern int _read(int file, char *ptr, int len);
/* Variables */
extern int __io_putchar(int ch) __attribute__((weak));
extern int __io_getchar(void) __attribute__((weak));

/* Put a string without format */
int	puts (const char *p){
	int count = 0;
	while(*p){
		(void)_write(0, (char*)p, 1);
		count++;
		p++;
	}
	return count;
}

/* put a single char to the console*/
int	putchar (int c){
	(void)_write(0, (char*)&c, 1);
	return 1;
}

/* Get a single char from the retarget console*/
int	getchar (void){
	int c=0;
	_read(0, (char*)&c, 1);
	return c;
}

/* Get a string of chars ended by \r
 * Max size is 32 bytes
 */
char* gets(char *p) {
#define GETS_MAX_SIZE 32
    char *base = p;
    char c = 0;
    int count = 0; // Contador interno para no exceder el límite

    while (1) {
        _read(0, &c, 1);

        // 1. Terminación
        if (c == '\r' || c == '\n') {
            *p = '\0';
            __io_putchar('\r');
            __io_putchar('\n');
            break;
        }

        // 2. Backspace
        else if (c == '\b' || c == 127) {
            if (count > 0) {
                p--;
                count--;
                __io_putchar('\b'); // 1. Mueve el cursor a la izquierda
                __io_putchar(' ');  // 2. Sobrescribe el carácter con un espacio (lo borra)
                __io_putchar('\b'); // 3. Vuelve a mover el cursor a la izquierda para escribir ahí
            }
        }

        // 3. Captura con límite interno
        else {
            // Solo guarda si no ha llegado al máximo (dejando 1 espacio para \0)
            if (count < (GETS_MAX_SIZE - 1)) {
                *p++ = c;
                count++;
                __io_putchar(c);
            }
        }
    }
    return base;
}


static void printchar(char **str, int c)
{

	if (str) {
		**str = c;
		++(*str);
	}
	else (void)_write(0,(char*)&c,1);
}

#define PAD_RIGHT 1
#define PAD_ZERO 2

static int prints(char **out, const char *string, int width, int pad)
{
	register int pc = 0, padchar = ' ';

	if (width > 0) {
		register int len = 0;
		register const char *ptr;
		for (ptr = string; *ptr; ++ptr) ++len;
		if (len >= width) width = 0;
		else width -= len;
		if (pad & PAD_ZERO) padchar = '0';
	}
	if (!(pad & PAD_RIGHT)) {
		for ( ; width > 0; --width) {
			printchar (out, padchar);
			++pc;
		}
	}
	for ( ; *string ; ++string) {
		printchar (out, *string);
		++pc;
	}
	for ( ; width > 0; --width) {
		printchar (out, padchar);
		++pc;
	}

	return pc;
}

#include <stdint.h>  // uintptr_t

static int printu(char **out, uintptr_t u, int b, int width, int pad, int letbase)
{
    char print_buf[PRINT_BUF_LEN];
    register char *s;
    register int t;

    if (u == 0) {
        print_buf[0] = '0';
        print_buf[1] = '\0';
        return prints(out, print_buf, width, pad);
    }

    s = print_buf + PRINT_BUF_LEN - 1;
    *s = '\0';

    while (u) {
        t = (int)(u % (uintptr_t)b);
        if (t >= 10)
            t += letbase - '0' - 10;
        *--s = (char)(t + '0');
        u /= (uintptr_t)b;
    }

    return prints(out, s, width, pad);
}



static int printp_fixed(char **out, void *p, int width, int pad, int letbase)
{
    uintptr_t v = (uintptr_t)p;
    int pc = 0;
    int digits = (int)(sizeof(uintptr_t) * 2); // 8 on STM32F4, 16 on 64-bit hosts

    // If user asks for larger total field, apply extra padding BEFORE 0x... unless PAD_RIGHT
    int total_len = 2 + digits; // "0x" + digits
    int extra = (width > total_len) ? (width - total_len) : 0;

    if (!(pad & PAD_RIGHT) && !(pad & PAD_ZERO)) {
        while (extra--) { printchar(out, ' '); pc++; }
    }

    pc += prints(out, "0x", 0, 0);

    // Always print fixed digits with zero pad
    pc += printu(out, v, 16, digits, (pad | PAD_ZERO) & ~PAD_RIGHT, letbase);

    if (pad & PAD_RIGHT) {
        while (extra--) { printchar(out, ' '); pc++; }
    }

    return pc;
}


/* the following should be enough for 32 bit int */

static int printi(char **out, int i, int b, int sg, int width, int pad, int letbase)
{
	char print_buf[PRINT_BUF_LEN];
	register char *s;
	register int t, neg = 0, pc = 0;
	register unsigned int u = i;

	if (i == 0) {
		print_buf[0] = '0';
		print_buf[1] = '\0';
		return prints (out, print_buf, width, pad);
	}

	if (sg && b == 10 && i < 0) {
		neg = 1;
		u = -i;
	}

	s = print_buf + PRINT_BUF_LEN-1;
	*s = '\0';

	while (u) {
		t = u % b;
		if( t >= 10 )
			t += letbase - '0' - 10;
		*--s = t + '0';
		u /= b;
	}

	if (neg) {
		if( width && (pad & PAD_ZERO) ) {
			printchar (out, '-');
			++pc;
			--width;
		}
		else {
			*--s = '-';
		}
	}

	return pc + prints (out, s, width, pad);
}

static int prints_len(char **out, const char *s, int maxlen, int width, int pad)
{
    int pc = 0;
    int len = 0;

    while (s[len] && (maxlen < 0 || len < maxlen))
        len++;

    if (!(pad & PAD_RIGHT)) {
        while (len < width) {
            printchar(out, pad & PAD_ZERO ? '0' : ' ');
            ++pc;
            ++len;
        }
    }

    for (int i = 0; i < len; i++) {
        printchar(out, s[i]);
        ++pc;
    }

    while (len < width) {
        printchar(out, ' ');
        ++pc;
        ++len;
    }

    return pc;
}


static int print(char **out, const char *format, va_list args)
{
    register int width, pad;
    register int pc = 0;
    int precision;
    int altform = 0;          // NEW: '#' flag
    char scr[2];

    for (; *format != 0; ++format) {
        if (*format == '%') {

            int is_long = 0;     // 'l' prefix
            precision = -1;      // string precision
            altform = 0;         // '#'

            ++format;
            width = pad = 0;

            if (*format == '\0') break;
            if (*format == '%') goto out;

            // FLAGS
            if (*format == '-') {
                ++format;
                pad = PAD_RIGHT;
            }

            while (*format == '0') {
                ++format;
                pad |= PAD_ZERO;
            }

            // NEW: '#' alternate form (useful for %p / %x)
            if (*format == '#') {
                altform = 1;
                ++format;
            }

            // WIDTH
            for (; *format >= '0' && *format <= '9'; ++format) {
                width *= 10;
                width += *format - '0';
            }

            // PRECISION (only used for %s in this tiny printf)
            if (*format == '.') {
                ++format;
                precision = 0;
                for (; *format >= '0' && *format <= '9'; ++format) {
                    precision *= 10;
                    precision += *format - '0';
                }
            }

            // 'l' prefix (for numbers; if used with %p we just ignore it)
            if (*format == 'l') {
                is_long = 1;
                ++format;
            }

            // STRING
            if (*format == 's') {
                if (is_long) {
                    pc += prints(out, "(wstr)", width, pad);
                } else {
                    char *s = va_arg(args, char*);
                    if (!s) s = "(null)";
                    if (precision >= 0)
                        pc += prints_len(out, s, precision, width, pad);
                    else
                        pc += prints(out, s, width, pad);
                }
                continue;
            }

            // POINTER
            /*
             * printf("%p", ptr);        // 0x1234abcd
			   printf("%016p", ptr);     // 0x000000001234abcd  (on 64-bit) or 0x000000001234abcd-ish depending on uintptr_t size
			   printf("%-18p", ptr);     // left-aligned within width
               printf("%#p", ptr);       // same as %p (prefix forced)
             * */
            if (*format == 'p') {
                void *pv = va_arg(args, void *);   // read ONCE
                pc += printp_fixed(out, pv, width, pad, 'a');
                continue;
            }

            // SIGNED DECIMAL
            if (*format == 'd') {
                if (is_long)
                    pc += printi(out, va_arg(args, long), 10, 1, width, pad, 'a');
                else
                    pc += printi(out, va_arg(args, int), 10, 1, width, pad, 'a');
                continue;
            }

            // UNSIGNED DECIMAL
            if (*format == 'u') {
                if (is_long)
                    pc += printi(out, va_arg(args, unsigned long), 10, 0, width, pad, 'a');
                else
                    pc += printi(out, va_arg(args, unsigned int), 10, 0, width, pad, 'a');
                continue;
            }

            // HEX LOWERCASE
            if (*format == 'x') {
                if (altform) pc += prints(out, "0x", 0, 0);
                if (is_long)
                    pc += printi(out, va_arg(args, unsigned long), 16, 0, width, pad, 'a');
                else
                    pc += printi(out, va_arg(args, unsigned int), 16, 0, width, pad, 'a');
                continue;
            }

            // HEX UPPERCASE
            if (*format == 'X') {
                if (altform) pc += prints(out, "0X", 0, 0);
                if (is_long)
                    pc += printi(out, va_arg(args, unsigned long), 16, 0, width, pad, 'A');
                else
                    pc += printi(out, va_arg(args, unsigned int), 16, 0, width, pad, 'A');
                continue;
            }

            // CHAR
            if (*format == 'c') {
                scr[0] = (char)va_arg(args, int);
                scr[1] = '\0';
                pc += prints(out, scr, width, pad);
                continue;
            }
        }
        else {
        out:
            printchar(out, *format);
            ++pc;
        }
    }

    if (out) **out = '\0';
    va_end(args);
    return pc;
}


int printf(const char *format, ...)
{
        va_list args;

        va_start( args, format );
        return print( 0, format, args );
}

int snprintf( char *buf, unsigned int count, const char *format, ... )
{
        va_list args;

        ( void ) count;

        va_start( args, format );
        return print( &buf, format, args );
}

int sprintf( char *buf, const char *format, ... )
{
        va_list args;

        va_start( args, format );
        return print( &buf, format, args );
}
