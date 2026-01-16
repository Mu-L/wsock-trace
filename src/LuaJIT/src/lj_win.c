
#include "lj_win.h"

#if defined(_WIN32)
#include <windows.h>
#include <stdlib.h>

static HANDLE stdout_hnd = INVALID_HANDLE_VALUE;
static CONSOLE_SCREEN_BUFFER_INFO console_info;
static int trace_level = -1;

void ljit_set_color (int color)
{
  WORD attr;

  if (stdout_hnd == INVALID_HANDLE_VALUE)
     return;

  switch (color)
  {
    case 0:
         attr = console_info.wAttributes;  /* restore original colors */
         break;
    case 1:     /* bright green */
         attr = (console_info.wAttributes & ~7) | (FOREGROUND_INTENSITY | 2);
         break;
    case 2:     /* bright white */
         attr = (console_info.wAttributes & ~7) | (FOREGROUND_INTENSITY | 7);
         break;
  }
  fflush (stdout);
  SetConsoleTextAttribute (stdout_hnd, attr);
}

int *ljit_trace_level (void)
{
  return (&trace_level);
}

int ljit_trace_init (void)
{
  const char *env;

  if (trace_level == -1)
  {
    trace_level = 0;
    env = getenv ("LUA_TRACE");
    if (env)
    {
      trace_level = *env - '0';
      if (trace_level > 0 && trace_level <= 9)
      {
        stdout_hnd = GetStdHandle (STD_OUTPUT_HANDLE);
        GetConsoleScreenBufferInfo (stdout_hnd, &console_info);
      }
    }
  }
  return (trace_level);
}

/**
 * Return the filename without any path or drive specifiers.
 */
const char *ljit_basename (const char *fname)
{
  const char *base = fname;

  if (fname && *fname)
  {
    if (fname[1] == ':')
    {
      fname += 2;
      base = fname;
    }

    while (*fname)
    {
      if (*fname == '\\' || *fname == '/')
         base = fname + 1;
      fname++;
    }
  }
  return (base);
}
#endif /* _WIN32 */

