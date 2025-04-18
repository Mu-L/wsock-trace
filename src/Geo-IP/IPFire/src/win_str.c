/*
 * `strsep()`, `strcasestr()`, `timegm()`, `strptime()`, `asprintf()` for Windows.
 */
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <errno.h>
#include <libloc/libloc.h>
#include <libloc/private.h>

/**
 * Get next token from string `*stringp`, where tokens are possibly empty
 * strings separated by characters from `delim`.
 *
 * Writes NULs into the string at `*stringp` to end tokens.
 *
 * `delim` need not remain constant from call to call.
 *
 * On return, `*stringp` points past the last `NUL` written (if there might
 * be further tokens), or is `NULL` (if there are definitely no more tokens).
 *
 * If `*stringp` is NULL, `strsep()` returns `NULL`.
 */
char *strsep (char **stringp, const char *delim)
{
  int         c, sc;
  char       *tok, *s = *stringp;
  const char *spanp;

  if (!s)
     return (NULL);

  for (tok = s;;)
  {
    c = *s++;
    spanp = delim;
    do
    {
      sc = *spanp++;
      if (sc == c)
      {
        if (c == '\0')
             s = NULL;
        else s[-1] = '\0';
        *stringp = s;
        return (tok);
      }
    }
    while (sc != 0);
  }
  return (NULL);
}

/**
 * Case-insensitive search for substring (`needle`) in
 * string `haystack`.
 */
char *strcasestr (const char *hay_stack, const char *needle)
{
  int i, lhay_stack, lneedle;

  lhay_stack = strlen (hay_stack);
  lneedle    = strlen (needle);
  for (i = 0; i < lhay_stack; i++)
  {
    if (!_strnicmp(hay_stack + i, needle, lneedle))
       return (char*) (hay_stack + i);
  }
  return (NULL);
}

/**
 * \def DELTA_EPOCH_SEC
 * Number of seconds from start of the Windows epoch
 * (Jan. 1, 1601) and to the Unix epoch (Jan. 1, 1970).
 */
#define DELTA_EPOCH_SEC  11644473600

/**
 * Return a Unix timestamp from a `struct tm`;
 * the inverse of function `gmtime()`.
 */
time_t timegm (struct tm *tm)
{
  time_t         ret;
  SYSTEMTIME     st;
  FILETIME       ft;
  ULARGE_INTEGER uli;

  memset (&st, '\0', sizeof(st));
  st.wYear   = (WORD) (tm->tm_year + 1900);
  st.wMonth  = (WORD) (tm->tm_mon + 1);
  st.wDay    = (WORD) tm->tm_mday;
  st.wHour   = (WORD) tm->tm_hour;
  st.wMinute = (WORD) tm->tm_min;
  st.wSecond = (WORD) tm->tm_sec;

  SystemTimeToFileTime (&st, &ft);
  uli.LowPart  = ft.dwLowDateTime;
  uli.HighPart = ft.dwHighDateTime;

  ret = (time_t) (uli.QuadPart/10000000);
  ret -= DELTA_EPOCH_SEC;   /* from Win epoch to Unix epoch */
  ret += _timezone;         /* Is this correct? (since strptime() ignores time-zone) */
  return (ret);
}

/**
 * Return last error from Winsock as "WSAE: xxx"
 */
char *get_neterr (void)
{
  static char err_buf [30];
  strcpy (err_buf, "WSAE: ");
  return _itoa (WSAGetLastError(), err_buf + sizeof("WSAE: ")-1, 10);
}

/*-
 * Copyright (c) 1997, 1998 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code was contributed to The NetBSD Foundation by Klaus Klein.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *        This product includes software developed by the NetBSD
 *        Foundation, Inc. and its contributors.
 * 4. Neither the name of The NetBSD Foundation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * We do not implement alternate representations. However, we always
 * check whether a given modifier is allowed for a certain conversion.
 */
#define ALT_E          0x01
#define ALT_O          0x02
#define TM_YEAR_BASE   1900

#define LEGAL_ALT(x)   do {                     \
                         if (alt_format & ~(x)) \
                            return (0);         \
                       } while (0)

static int conv_num (const char **buf, int *dest, int llim, int ulim);

static const char *day[7] = { "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday",
                               "Friday", "Saturday"
                            };
static const char *abday[7] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
static const char *mon[12] = { "January", "February", "March", "April", "May", "June", "July",
                               "August", "September", "October", "November", "December"
                             };
static const char *abmon[12] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                 "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
                               };
static const char *am_pm[2] = { "AM", "PM" };

char *strptime (const char *buf, const char *fmt, struct tm *tm)
{
  char        c;
  const char *bp = buf;
  size_t      len = 0;
  int         alt_format, i, split_year = 0;

  while ((c = *fmt) != '\0')
  {
    /* Clear `alternate' modifier prior to new conversion. */
    alt_format = 0;

    /* Eat up white-space. */
    if (isspace(c))
    {
      while (isspace(*bp))
          bp++;
      fmt++;
      continue;
    }

    if ((c = *fmt++) != '%')
       goto literal;

again:
      switch (c = *fmt++)
      {
        case '%':   /* "%%" is converted to "%". */
literal:
             if (c != *bp++)
                return (0);
             break;

        /* "Alternative" modifiers. Just set the appropriate flag
         * and start over again.
         */
        case 'E':   /* "%E?" alternative conversion modifier. */
             LEGAL_ALT (0);
             alt_format |= ALT_E;
             goto again;

        case 'O':   /* "%O?" alternative conversion modifier. */
             LEGAL_ALT (0);
             alt_format |= ALT_O;
             goto again;

        /* "Complex" conversion rules, implemented through recursion.
         */
        case 'c':   /* Date and time, using the locale's format. */
             LEGAL_ALT (ALT_E);
             bp = strptime (bp, "%x %X", tm);
             if (!bp)
                return (0);
             break;

        case 'D':   /* The date as "%m/%d/%y". */
             LEGAL_ALT (0);
             bp = strptime (bp, "%m/%d/%y", tm);
             if (!bp)
                return (0);
             break;

        case 'R':   /* The time as "%H:%M". */
             LEGAL_ALT (0);
             bp = strptime (bp, "%H:%M", tm);
             if (!bp)
                return (0);
             break;

        case 'r':   /* The time in 12-hour clock representation. */
             LEGAL_ALT (0);
             bp = strptime (bp, "%I:%M:%S %p", tm);
             if (!bp)
                return (0);
             break;

        case 'T':   /* The time as "%H:%M:%S". */
             LEGAL_ALT (0);
             bp = strptime (bp, "%H:%M:%S", tm);
             if (!bp)
                return (0);
             break;

        case 'X':   /* The time, using the locale's format. */
             LEGAL_ALT (ALT_E);
             bp = strptime (bp, "%H:%M:%S", tm);
             if (!bp)
                return (0);
             break;

        case 'x':   /* The date, using the locale's format. */
             LEGAL_ALT (ALT_E);
             bp = strptime (bp, "%m/%d/%y", tm);
             if (!bp)
                return (0);
             break;

             /* "Elementary" conversion rules.
              */
        case 'A':   /* The day of week, using the locale's form. */
        case 'a':
             LEGAL_ALT (0);
             for (i = 0; i < 7; i++)
             {
               /* Full name. */
               len = strlen (day[i]);
               if (!_strnicmp(day[i], bp, len))
                  break;

               /* Abbreviated name. */
               len = strlen (abday[i]);
               if (!_strnicmp(abday[i], bp, len))
                  break;
             }
             /* Nothing matched. */
             if (i == 7)
                return (0);

             tm->tm_wday = i;
             bp += len;
             break;

        case 'B':   /* The month, using the locale's form. */
        case 'b':
        case 'h':
             LEGAL_ALT (0);
             for (i = 0; i < 12; i++)
             {
               /* Full name. */
               len = strlen (mon[i]);
               if (!_strnicmp(mon[i], bp, len))
                  break;

               /* Abbreviated name. */
               len = strlen (abmon[i]);
               if (!_strnicmp(abmon[i], bp, len))
                  break;
             }
             /* Nothing matched. */
             if (i == 12)
                return (0);
             tm->tm_mon = i;
             bp += len;
             break;

        case 'C':   /* The century number. */
             LEGAL_ALT (ALT_E);
             if (!conv_num(&bp, &i, 0, 99))
                return (0);

             if (split_year)
                tm->tm_year = (tm->tm_year % 100) + (i * 100);
             else
             {
               tm->tm_year = i * 100;
               split_year = 1;
             }
             break;

        case 'd':   /* The day of month. */
        case 'e':
             LEGAL_ALT (ALT_O);
             if (!conv_num(&bp, &tm->tm_mday, 1, 31))
                return (0);
             break;

        case 'k':   /* The hour (24-hour clock representation). */
             LEGAL_ALT (0);
             __attribute__((fallthrough));

        case 'H':
             LEGAL_ALT (ALT_O);
             if (!conv_num(&bp, &tm->tm_hour, 0, 23))
                return (0);
             break;

        case 'l':   /* The hour (12-hour clock representation). */
             LEGAL_ALT (0);
             __attribute__((fallthrough));

        case 'I':
             LEGAL_ALT (ALT_O);
             if (!conv_num(&bp, &tm->tm_hour, 1, 12))
                return (0);
             if (tm->tm_hour == 12)
                tm->tm_hour = 0;
             break;

        case 'j':   /* The day of year. */
             LEGAL_ALT (0);
             if (!conv_num(&bp, &i, 1, 366))
                return (0);
             tm->tm_yday = i - 1;
             break;

        case 'M':   /* The minute. */
             LEGAL_ALT (ALT_O);
             if (!conv_num(&bp, &tm->tm_min, 0, 59))
                return (0);
             break;

        case 'm':   /* The month. */
             LEGAL_ALT (ALT_O);
             if (!conv_num(&bp, &i, 1, 12))
                return (0);
             tm->tm_mon = i - 1;
             break;

        case 'p':   /* The locale's equivalent of AM/PM. */
             LEGAL_ALT (0);
             if (!_stricmp(am_pm[0], bp)) /* AM? */
             {
               if (tm->tm_hour > 11)
                  return (0);
               bp += strlen(am_pm[0]);
               break;
             }
             if (!_stricmp(am_pm[1], bp)) /* PM? */
             {
               if (tm->tm_hour > 11)
                   return (0);
               tm->tm_hour += 12;
               bp += strlen(am_pm[1]);
               break;
             }
             /* Nothing matched. */
             return (0);

        case 'S':   /* The seconds. */
             LEGAL_ALT (ALT_O);
             if (!conv_num(&bp, &tm->tm_sec, 0, 61))
                return (0);
             break;

        case 'U':   /* The week of year, beginning on sunday. */
        case 'W':   /* The week of year, beginning on monday. */
             LEGAL_ALT (ALT_O);

            /* XXX This is bogus, as we can not assume any valid
             * information present in the tm structure at this
             * point to calculate a real value, so just check the
             * range for now.
             */
             if (!conv_num(&bp, &i, 0, 53))
                return (0);
             break;

        case 'w':   /* The day of week, beginning on sunday. */
             LEGAL_ALT (ALT_O);
             if (!conv_num(&bp, &tm->tm_wday, 0, 6))
                return (0);
             break;

        case 'Y':   /* The year. */
             LEGAL_ALT (ALT_E);
             if (!conv_num(&bp, &i, 0, 9999))
                return (0);
             tm->tm_year = i - TM_YEAR_BASE;
             break;

        case 'y':   /* The year within 100 years of the epoch. */
             LEGAL_ALT (ALT_E | ALT_O);
             if (!conv_num(&bp, &i, 0, 99))
                return (0);

             if (split_year)
             {
               tm->tm_year = ((tm->tm_year / 100) * 100) + i;
               break;
             }
             split_year = 1;
             if (i <= 68)
                  tm->tm_year = i + 2000 - TM_YEAR_BASE;
             else tm->tm_year = i + 1900 - TM_YEAR_BASE;
             break;

        case 'n':   /* Any kind of white-space. */
        case 't':
             LEGAL_ALT (0);
             while (isspace(*bp))
                 bp++;
             break;

        default:    /* Unknown/unsupported conversion. */
             return (0);
    }
  }
  return ((char*)bp);
}

static int conv_num (const char **buf, int *dest, int llim, int ulim)
{
  int result = 0;
  int rulim = ulim;  /* The limit also determines the number of valid digits. */

  if (**buf < '0' || **buf > '9')
     return (0);

  do
  {
    result *= 10;
    result += *(*buf)++ - '0';
    rulim /= 10;
  }
  while ((result * 10 <= ulim) && rulim && **buf >= '0' && **buf <= '9');

  if (result < llim || result > ulim)
     return (0);

  *dest = result;
  return (1);
}

/*
 * Ripped from libpcap's 'missing/asprintf.c' and modified:
 *
 * vasprintf() and asprintf() for platforms with a C99-compliant
 * snprintf() - so that, if you format into a 1-byte buffer, it
 * will return how many characters it would have produced had
 * it been given an infinite-sized buffer.
 */
int vasprintf (char **strp, const char *format, va_list args)
{
  char   buf, *str;
  int    ret, len;
  size_t str_size;

  len = vsnprintf (&buf, sizeof(buf), format, args);
  if (len == -1)
  {
    *strp = NULL;
    return (-1);
  }

  str_size = len + 1;
  str = malloc (str_size);
  if (!str)
  {
    *strp = NULL;
    return (-1);
  }

  ret = vsnprintf (str, str_size, format, args);
  if (ret == -1)
  {
    free (str);
    *strp = NULL;
    return (-1);
  }

  *strp = str;
  return (ret);
}

int asprintf (char **strp, const char *format, ...)
{
  va_list args;
  int     ret;

  va_start (args, format);
  ret = vasprintf (strp, format, args);
  va_end (args);
  return (ret);
}
