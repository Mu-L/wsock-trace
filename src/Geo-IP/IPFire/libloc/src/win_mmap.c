#include <loc/libloc.h>
#include <loc/private.h>
#include <sys/mman.h>
#include <stdint.h>
#include <io.h>
#include <windows.h>

#define DWORD_HI(x)  ((uint64_t)(x) >> 32)
#define DWORD_LO(x)  ((x) & 0xffffffff)
#define DIM(arr)     (sizeof(arr) / sizeof(arr[0]))

static int debug = -1;
static SYSTEM_INFO si;

/**
 * A small array of 'mmap()' return values we need to remember
 * until we call `munmap()` on the pointer.
 */
struct mmap_info {
       void  *map;   /* the value from MapViewOfFile() */
       void  *rval;  /* the value we returned to caller of mmap() */
     };
static struct mmap_info mmap_storage[10];

static void *mmap_remember (void *map, off_t offset);
static int   mmap_forget (void *map, struct mmap_info *info);

#ifdef EXTRA_DEBUG_PARANOIA
  static void  hex_dump (const char *what, const void *data_p, size_t datalen);
#endif

void *_mmap (void *address, size_t length, int protection, int flags, int fd, off_t offset,
             const char *fname, unsigned line)
{
  void     *map = NULL;
  void     *rval = NULL;
  HANDLE    handle = INVALID_HANDLE_VALUE;
  DWORD     err1 = 0;
  DWORD     err2 = 0;
  intptr_t  h = _get_osfhandle (fd);
  DWORD     access = 0;
  uint64_t  pstart, psize, poffset;

  if (debug == -1)
  {
     const char *env = getenv ("LIBLOC_DEBUG");
     int   v;

     debug = 0;
     GetSystemInfo (&si);
     if (env)
     {
       v = *env - '0';
       debug = (v > 0 && v < 10);
     }
  }

  pstart  = (offset / si.dwAllocationGranularity) * si.dwAllocationGranularity;
  poffset = offset - pstart;
  psize   = poffset + length;

  switch (protection)
  {
    case PROT_READ:
         handle = CreateFileMapping ((HANDLE)h, 0, PAGE_READONLY, 0, 0, NULL);
         access = FILE_MAP_READ;
         break;
    case PROT_WRITE:
         handle = CreateFileMapping ((HANDLE)h, 0, PAGE_READWRITE, 0, 0, NULL);
         access = FILE_MAP_WRITE;  /* Or FILE_MAP_COPY? */
         break;
    case PROT_READWRITE:
         handle = CreateFileMapping ((HANDLE)h, 0, PAGE_READWRITE, 0, 0, NULL);
         access = FILE_MAP_ALL_ACCESS;
         break;
    default:
         break;
  }

  if (!handle)
  {
    map = MAP_FAILED;
    err1 = GetLastError();
  }
  else
  {
    map = MapViewOfFile (handle, access, DWORD_HI(pstart), DWORD_LO(pstart), (SIZE_T)psize);
    if (!map)
    {
      map = MAP_FAILED;
      err2 = GetLastError();
    }
  }

  if (handle && handle != INVALID_HANDLE_VALUE)
     CloseHandle (handle);

  rval = mmap_remember (map, poffset);

#ifdef EXTRA_DEBUG_PARANOIA
  if (!debug)
     return (rval);

  fprintf (stderr,
           "%s(%u):\n"
           "   pstart: %lld, poffset: %lld, psize: %lld, length: %u, fd: %d, offset: %ld,\n"
           "   err1: %lu, err2: %lu  -> map: 0x%p, 0x%p\n",
           fname, line, pstart, poffset, psize, length, fd, offset, err1, err2, map, rval);

  /* Now for the paranoia:
   *
   * Test the low and high end of the mmap'ed region to check
   * we get no exceptions. Only test 'PROT_READ' since that's the only
   * protection used in libloc.
   */
  if (map != MAP_FAILED && protection == PROT_READ)
  {
    const char  *p = (char*) rval;
    const char  *p_min = p;
    const char  *p_max = p + length - 2;
    size_t len;

    BOOL p_min_ok = !IsBadReadPtr (p_min, 1);
    BOOL p_max_ok = !IsBadReadPtr (p_max, 1);

    if (!p_min_ok || !p_max_ok)
    {
      fprintf (stderr, "   p_min_ok: %d, p_min: 0x%p, p_max_ok: %d, p_max: 0x%p\n\n",
               p_min_ok, p_min, p_max_ok, p_max);
      return (MAP_FAILED);
    }

    len = min (length, 100);
    hex_dump ("Dumping first %zu bytes:\n", p, len);
    if (length > 100)
      hex_dump ("Dumping last %zu bytes:\n", p + length - 100, len);
    else
      fprintf (stderr, "Last chunk of data covered by the first chunk.\n\n");
  }
#endif
  return (rval);
}

int _munmap (void *map, size_t length, const char *fname, unsigned line)
{
  struct mmap_info info;

  if (mmap_forget(map, &info))
  {
    fprintf (stderr, "%s(%u):\n   munmap (0x%p, %zu), EINVAL.\n", fname, line, map, length);
    return (-1);
  }

  if (!UnmapViewOfFile(info.map) && debug)
  {
    fprintf (stderr, "%s(%u):\n   munmap (0x%p, %zu) failed: %lu\n", fname, line, map, length, GetLastError());
    errno = EFAULT;
    return (-1);
  }
  return (0);
}

static void *mmap_remember (void *map, off_t offset)
{
  int i;

  if (map == MAP_FAILED)  /* never rememember this */
  {
    errno = EFAULT;
    return (MAP_FAILED);
  }

  for (i = 0; i < DIM(mmap_storage); i++)
  {
    if (!mmap_storage[i].map)
    {
      mmap_storage[i].map = map;
      mmap_storage[i].rval = (char*)map + offset;
      return (mmap_storage[i].rval);
    }
  }
  errno = EAGAIN;
  return (MAP_FAILED); /* all buckets full */
}

static int mmap_forget (void *map, struct mmap_info *info)
{
  int i;

  if (info)
     memset (info, '\0', sizeof(*info));
  for (i = 0; i < DIM(mmap_storage); i++)
  {
    if (map == mmap_storage[i].rval)
    {
      if (info)
         *info = mmap_storage[i];
      mmap_storage[i].map = NULL;   /* reuse this */
      return (0);
    }
  }
  errno = EINVAL;
  return (-1);  /* not found! */
}

#ifdef EXTRA_DEBUG_PARANOIA
/**
 * Do not use 'hexdump()' in 'loc/private.h'.
 */
static void hex_dump (const char *what, const void *data_p, size_t datalen)
{
  const BYTE *data = (const BYTE*) data_p;
  UINT  ofs;

  fprintf (stderr, what, datalen);
  for (ofs = 0; ofs < datalen; ofs += 16)
  {
    UINT j;

    fprintf (stderr, "  %p: ", data+ofs);
    for (j = 0; j < 16 && j+ofs < datalen; j++)
        fprintf (stderr, "%02X%c", (unsigned)data[j+ofs],
                 j == 7 && j+ofs < datalen-1 ? '-' : ' ');

    for ( ; j < 16; j++)       /* pad line to 16 positions */
        fputs ("   ", stderr);

    for (j = 0; j < 16 && j+ofs < datalen; j++)
    {
      int ch = data[j+ofs];

      if (ch < ' ')            /* non-printable */
           putc ('.', stderr);
      else putc (ch, stderr);
    }
    putc ('\n', stderr);
  }
  putc ('\n', stderr);
}
#endif /* EXTRA_DEBUG_PARANOIA */
