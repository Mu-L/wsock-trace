/**\file    ip2loc.c
 * \ingroup Geoip
 *
 * \brief
 *   This file is an interface for the IP2Location library. \n
 *     Ref: https://github.com/chrislim2888/IP2Location-C-Library <br>
 *          http://lite.ip2location.com
 *
 * ip2loc.c - Part of Wsock-Trace.
 *
 * Together with the `geoip*.c` files, this module will return location
 * information (country, city and region) for an IPv4/IPv6-address.
 *
 * This module uses a binary file specified in the `[geoip]` section and
 * the keyword `ip2location_bin_file` of `wsock_trace`.
 *
 * \note There is no separate keyword for IPv4 and IPv6 addresses as this
 *       binary file should contain both address-families
 *       (and additional information).
 */

#include <stdint.h>
#include <errno.h>

#include "common.h"
#include "init.h"
#include "in_addr.h"
#include "inet_util.h"
#include "geoip.h"

#if defined(__clang__)
  #pragma clang diagnostic ignored "-Wstrict-prototypes"

#elif defined(__GNUC__)
  GCC_PRAGMA (GCC diagnostic ignored "-Wpointer-to-int-cast")
#endif

#if !defined(IN6_IS_ADDR_V4MAPPED)
  #define IN6_IS_ADDR_V4MAPPED(a) \
          (BOOLEAN) ( ((a)->s6_words[0] == 0) && ((a)->s6_words[1] == 0) && \
                      ((a)->s6_words[2] == 0) && ((a)->s6_words[3] == 0) && \
                      ((a)->s6_words[4] == 0) && ((a)->s6_words[5] == 0xFFFF) )
#endif

#if defined(__CYGWIN__)
  #define _byteswap_ulong(x)  (unsigned long) swap32 (x)
#endif

/*
 * Previously ip2loc.c used the IP2Location-C-Library as a `git submodule`.
 * But now, I've simply pasted in the code from:
 *   IP2Location.c
 *   IP2Location.h
 *
 * below, patched heavily and removed stuff not needed.
 *
 * Most importantly, it uses only shared-memory access without calling `calloc()`
 * and `strdup()` to return any results. Just copy to a `ip2loc_entry` entry as
 * needed.
 *
 * IP2Location C library is distributed under MIT license
 * Copyright (c) 2013-2015 IP2Location.com. support at ip2location dot com
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the MIT license
 */

/**
 * \def API_VERSION_MAJOR
 *  The major version of this API.
 *
 * \def API_VERSION_MINOR
 *  The minor version of this API.
 *
 * \def API_VERSION_MICRO
 *  The micro version of this API.
 */
#define API_VERSION_MAJOR   8
#define API_VERSION_MINOR   0
#define API_VERSION_MICRO   8

/**
 * Flags used to determine what to look for in `IP2Location_read_record()`.
 *
 * \def FLG_COUNTRY_SHORT
 *  Look up the 2 letter ISO-3166 country code.
 * \ref geoip_get_long_name_by_A2() and c_list[].
 *
 * \def FLG_COUNTRY_LONG
 *  Look up the complete country name.
 *
 * \def FLG_REGION
 *  Look up the region in the country.
 *
 * \def FLG_CITY
 *  Look up the city in the country.
 */
#define FLG_COUNTRY_SHORT   0x00001
#define FLG_COUNTRY_LONG    0x00002
#define FLG_REGION          0x00004
#define FLG_CITY            0x00008
#define FLG_ISP             0x00010   /* never used */
#define FLG_LATITUDE        0x00020
#define FLG_LONGITUDE       0x00040

/**
 * \def IP2LOC_FLAGS
 *
 * The default flags used to look up an database-entry for an IPv4/IPv6-address
 * with these members:
 *
 *  \li `struct ip2loc_entry::country_short`,
 *  \li `struct ip2loc_entry::country_long`,
 *  \li `struct ip2loc_entry::city`,
 *  \li `struct ip2loc_entry::region`.
 *
 * And optionally:
 *  \li `struct ip2loc_entry::latitude`.
 *  \li `struct ip2loc_entry::longitude`.
 *
 * if `g_cfg.GEOIP.show_position` or `g_cfg.GEOIP.show_map_url` is TRUE.
 */

#if 0
  #define IP2LOC_FLAGS  (FLG_COUNTRY_SHORT | FLG_COUNTRY_LONG | FLG_REGION | FLG_CITY)
#else
  /**
   * A bit faster to skip looking for the full country-name.
   * The returned `struct ip2loc_entry::country_short` is instead passed to
   * `geoip_get_long_name_by_A2()` to get the full country-name.
   */
  #define IP2LOC_FLAGS  (FLG_COUNTRY_SHORT | FLG_REGION | FLG_CITY)
#endif

static uint32_t lookup_flags = IP2LOC_FLAGS;

#define MAX_IPV4_RANGE      4294967295U /* ULONG_MAX */
#define IPV4                0
#define IPV6                1

/**\typedef IP2Location
 *
 * The structure that holds all vital information in a IP2Location .bin data-file.
 */
typedef struct IP2Location {
        FILE       *file;         /**< The `fopen_excl()` file structure */
        uint8_t    *sh_mem_ptr;
        uint64      sh_mem_max;
        uint64      sh_mem_index_errors;
        HANDLE      sh_mem_fd;
        BOOL        sh_mem_already;
        struct stat stat_buf;
        uint8_t     db_type;
        uint8_t     db_column;
        uint8_t     db_day;
        uint8_t     db_month;
        uint8_t     db_year;
        uint32_t    ip_version;
        uint32_t    ipv4_db_count;
        uint32_t    ipv4_db_addr;
        uint32_t    ipv6_db_count;
        uint32_t    ipv6_db_addr;
        uint32_t    ipv4_index_db_addr;
        uint32_t    ipv6_index_db_addr;
      } IP2Location;

typedef struct ipv_t {
        uint32_t        ip_ver;
        uint32_t        ipv4;
        struct in6_addr ipv6;
      } ipv_t;

/**
 * The global handle for all IP2Location access.
 * Returned from `open_file()`.
 */
static struct IP2Location *ip2loc_handle;

/**
 * Set to TRUE in case `open_file()` found the .bin-file to be junk.
 */
static BOOL ip2loc_bad;

/** Number of loops in `IP2Location_get_ipv4_record()` to find an IPv4 entry.
 */
static DWORD num_4_loops;

/** Number of loops in `IP2Location_get_ipv6_record()` to find an IPv4 entry.
 */
static DWORD num_6_loops;

static uint8_t COUNTRY_POSITION[25]   = { 0, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2 };
static uint8_t REGION_POSITION[25]    = { 0, 0, 0, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3 };
static uint8_t CITY_POSITION[25]      = { 0, 0, 0, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4 };
static uint8_t LATITUDE_POSITION[25]  = { 0, 0, 0, 0, 0, 5, 5, 0, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5 };
static uint8_t LONGITUDE_POSITION[25] = { 0, 0, 0, 0, 0, 6, 6, 0, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6 };

static void        IP2Location_initialize (IP2Location *loc);
static void        IP2Location_read_ipv6_addr (IP2Location *loc, uint32_t position, struct in6_addr *addr);
static uint32_t    IP2Location_read32 (IP2Location *loc, uint32_t position);
static uint8_t     IP2Location_read8 (IP2Location *loc, uint32_t position);
static float       IP2Location_read_float (IP2Location *loc, uint32_t position);
static int32_t     IP2Location_DB_set_shared_memory (IP2Location *loc);
static void        IP2Location_close (IP2Location *loc);
static const char *IP2Location_api_version_str (void);

/**
 * Open and initialise access to the IP2Location library and binary data-file.
 *
 * \param[in] fname  the IP2Location binary file.
 */
static IP2Location *open_file (const char *fname)
{
  struct IP2Location *loc = calloc (1, sizeof(*loc));
  UINT   IPvX;
  BOOL   is_IPv4_only, is_IPv6_only;
  SYSTEMTIME st;

  if (!loc)
  {
    TRACE (1, "Failed to allocate memory.\n");
    return (NULL);
  }

  loc->file = fopen_excl (fname, "rb");
  if (!loc->file)
  {
    TRACE (1, "Failed to fopen_excl (\"%s\"): errno=%d.\n", fname, errno);
    IP2Location_close (loc);
    return (NULL);
  }

  if (IP2Location_DB_set_shared_memory(loc) == -1)
  {
    IP2Location_close (loc);
    return (NULL);
  }

  IP2Location_initialize (loc);

  if (!loc->sh_mem_already)
  {
    /* No need to keep the file open
     */
    fclose (loc->file);
    loc->file = NULL;
  }

  /* The IP2Loc database scheme is really strange.
   * This used to be true previously.
   */
  IPvX = loc->ip_version;
  if (IPvX == IPV4)
     IPvX = 4;
  else if (IPvX == IPV6)
     IPvX = 6;

  /* The IPvX count could now mean the count of IPv6 addresses
   * in a database with both IPv4 and IPv6 addresses.
   */
  is_IPv4_only = is_IPv6_only = FALSE;
  if (IPvX == loc->ipv6_db_count && loc->ipv4_db_count == 0)
     is_IPv6_only = TRUE;

  else if (IPvX == loc->ipv4_db_count && loc->ipv6_db_count == 0)
     is_IPv4_only = TRUE;

  memset (&st, '\0', sizeof(st));
  GetLocalTime (&st);

  if (loc->db_day == 0 || loc->db_day > 31 ||
      loc->db_month == 0 || loc->db_month > 12 ||
      loc->db_year + 2000 > st.wYear)
  {
    ip2loc_bad = TRUE;   /* Do not attempt this again */
    ip2loc_exit();

    memset (&st, '\0', sizeof(st));
    st.wDay   = loc->db_day;
    st.wMonth = loc->db_month;
    st.wYear  = loc->db_year + 2000;

    if (g_cfg.trace_level > 0 && !loc->sh_mem_already)
       WARNING ("IP2Loc file '%s' seems to contain junk. Date: %s\n",
                fname, get_date_str(&st));
  }
  else
  {
    memset (&st, '\0', sizeof(st));
    st.wDay   = loc->db_day;
    st.wMonth = loc->db_month;
    st.wYear  = loc->db_year + 2000;

    TRACE (2, "Success: %s\n"
              "               Database has %s entries. API-version: %s, size: %s bytes\n"
              "               Date: %s, IPv4-count: %s, IPv6-count: %s\n"
              "               (is_IPv4_only: %d, is_IPv6_only: %d, ipv4_index_db_addr: %u, ipv6_index_db_addr: %u).\n",
           fname, dword_str(loc->ipv4_db_count + loc->ipv6_db_count), IP2Location_api_version_str(),
           dword_str(loc->stat_buf.st_size), get_date_str(&st),
           dword_str(loc->ipv4_db_count), dword_str(loc->ipv6_db_count),
           is_IPv4_only, is_IPv6_only, loc->ipv4_index_db_addr, loc->ipv6_index_db_addr);
  }
  return (loc);
}

/**
 * Our initialiser for IP2Location library and binary data-file. <br>
 * Called from geoip_init().
 */
BOOL ip2loc_init (void)
{
  if (!g_cfg.GEOIP.enable || !g_cfg.GEOIP.ip2location_bin_file || ip2loc_bad)
     return (FALSE);

  if (!ip2loc_handle)
     ip2loc_handle = open_file (g_cfg.GEOIP.ip2location_bin_file);

  if (g_cfg.GEOIP.show_position || g_cfg.GEOIP.show_map_url)
     lookup_flags |= (FLG_LATITUDE + FLG_LONGITUDE);

  return (ip2loc_handle != NULL);
}

/**
 * Close the IP2Location library. <br>
 * Called from geoip_exit().
 */
void ip2loc_exit (void)
{
  IP2Location_close (ip2loc_handle);
  ip2loc_handle = NULL;
}

/**
 * Return the number of IPv4-addresses in the data-file.
 */
DWORD ip2loc_num_ipv4_entries (void)
{
  if (ip2loc_handle)
     return (ip2loc_handle->ipv4_db_count);
  return (0);
}

/**
 * Return the number of IPv6-addresses in the data-file.
 */
DWORD ip2loc_num_ipv6_entries (void)
{
  if (ip2loc_handle)
     return (ip2loc_handle->ipv6_db_count);
  return (0);
}

/*
 * Include the IP2Location sources here to avoid the need to build the library.
 * The `Makefile.win` on Github is broken anyway.
 *
 * And turn off some warnings:
 */
#if defined(__GNUC__) || defined(__clang__)
  GCC_PRAGMA (GCC diagnostic ignored "-Wunused-function")
  GCC_PRAGMA (GCC diagnostic ignored "-Wunused-variable")
  GCC_PRAGMA (GCC diagnostic ignored "-Wunused-parameter")

  #if defined(__clang__)
    #pragma clang diagnostic ignored "-Wcast-qual"
    #pragma clang diagnostic ignored "-Wconditional-uninitialized"
  #else
    GCC_PRAGMA (GCC diagnostic ignored   "-Wunused-but-set-variable")
  #endif

#elif defined(_MSC_VER)
  #pragma warning (disable: 4101 4244)
#endif

/**
 * Close the IP2Location database access to shared-memory.
 * Free the global `loc`.
 */
static void IP2Location_close (IP2Location *loc)
{
  if (loc)
  {
    if (loc->sh_mem_ptr)
    {
      UnmapViewOfFile (loc->sh_mem_ptr);
      CloseHandle (loc->sh_mem_fd);
    }
    if (loc->file)
       fclose (loc->file);

    loc->file       = NULL;
    loc->sh_mem_ptr = NULL;
    loc->sh_mem_fd  = INVALID_HANDLE_VALUE;
    free (loc);
  }
}

/**
 * Startup
 */
static void IP2Location_initialize (IP2Location *loc)
{
  loc->db_type       = IP2Location_read8 (loc, 1);
  loc->db_column     = IP2Location_read8 (loc, 2);
  loc->db_year       = IP2Location_read8 (loc, 3);
  loc->db_month      = IP2Location_read8 (loc, 4);
  loc->db_day        = IP2Location_read8 (loc, 5);

  loc->ipv4_db_count = IP2Location_read32 (loc, 6);
  loc->ipv4_db_addr  = IP2Location_read32 (loc, 10);
  loc->ipv6_db_count = IP2Location_read32 (loc, 14);
  loc->ip_version    = IP2Location_read32 (loc, 14);
  loc->ipv6_db_addr  = IP2Location_read32 (loc, 18);

  loc->ipv4_index_db_addr = IP2Location_read32 (loc, 22);
  loc->ipv6_index_db_addr = IP2Location_read32 (loc, 26);
}

/**
 * Compare to IPv6 addresses
 */
static int ipv6_compare (const struct in6_addr *addr1, const struct in6_addr *addr2)
{
  int i, ret = 0;

  for (i = 0 ; i < 16 ; i++)
  {
    if (addr1->u.Byte[i] > addr2->u.Byte[i])
    {
      ret = 1;
      break;
    }
    if (addr1->u.Byte[i] < addr2->u.Byte[i])
    {
      ret = -1;
      break;
    }
  }
  return (ret);
}

/**
 * Read a Pascal-type string; `size + byte-characters` at `position`.
 */
static void IP2Location_read_str (IP2Location *loc, uint32_t position, char *ret, size_t max_sz)
{
  uint8_t size;

  if ((uint64)loc->sh_mem_ptr + position >= loc->sh_mem_max)
  {
    loc->sh_mem_index_errors++;
    *ret = '\0';
    return;
  }
  size = loc->sh_mem_ptr [position];
  size = min (size, (uint8_t)max_sz);
  memcpy (ret, &loc->sh_mem_ptr[position+1], size);
}

/**
 * Read the record data into `*out`.
 *
 * \param[in] loc      the current database structure; equals `ip2loc_handle`.
 * \param[in] rowaddr  the database row.
 * \param[in] mode     the flags used for lookup; equals `lookup_flags`.
 * \param[out] out     the entry to return.
 *
 * \note
 *  The `*out` record is empty on entry of this function.
 */
static void IP2Location_read_record (IP2Location *loc, uint32_t rowaddr, uint32_t mode, struct ip2loc_entry *out)
{
  uint32_t val;
  uint8_t  dbtype = loc->db_type;

  if ((mode & FLG_COUNTRY_SHORT) && COUNTRY_POSITION[dbtype])
  {
    val = IP2Location_read32 (loc, rowaddr + 4 * (COUNTRY_POSITION[dbtype]-1));
    IP2Location_read_str (loc, val, out->country_short, sizeof(out->country_short));
  }

  if ((mode & FLG_COUNTRY_LONG) && COUNTRY_POSITION[dbtype])  /* No need to call this. Remove? */
  {
    val = IP2Location_read32 (loc, rowaddr + 4 * (COUNTRY_POSITION[dbtype]-1));
    IP2Location_read_str (loc, val+3, out->country_long, sizeof(out->country_long));
  }

  if ((mode & FLG_REGION) && REGION_POSITION[dbtype])
  {
    val = IP2Location_read32 (loc, rowaddr + 4 * (REGION_POSITION[dbtype]-1));
    IP2Location_read_str (loc, val, out->region, sizeof(out->region));
  }

  if ((mode & FLG_CITY) && CITY_POSITION[dbtype])
  {
    val = IP2Location_read32 (loc, rowaddr + 4 * (CITY_POSITION[dbtype]-1));
    IP2Location_read_str (loc, val, out->city, sizeof(out->city));
  }

  if ((mode & FLG_LATITUDE) && (LATITUDE_POSITION[dbtype]))
     out->latitude = IP2Location_read_float (loc, rowaddr + 4 * (LATITUDE_POSITION[dbtype] - 1));

  if ((mode & FLG_LONGITUDE) && (LONGITUDE_POSITION[dbtype]))
     out->longitude = IP2Location_read_float (loc, rowaddr + 4 * (LONGITUDE_POSITION[dbtype]-1));
}

/**
 * Get record for a IPv4 from database
 */
static BOOL IP2Location_get_ipv4_record (IP2Location *loc, uint32_t mode, ipv_t parsed_ipv, struct ip2loc_entry *out)
{
  uint32_t baseaddr  = loc->ipv4_db_addr;
  uint32_t dbcolumn  = loc->db_column;
  uint32_t ipv4index = loc->ipv4_index_db_addr;
  uint32_t low       = 0;
  uint32_t mid       = 0;
  uint32_t high      = loc->ipv4_db_count;
  uint32_t ipno, ipfrom, ipto;

  ipno = parsed_ipv.ipv4;
  num_4_loops = 0;

  if (ipno == (uint32_t)MAX_IPV4_RANGE)
     ipno -= 1;

  if (ipv4index > 0)
  {
    /* use the index table
     */
    uint32_t ipnum1n2 = (uint32_t)ipno >> 16;
    uint32_t indexpos = ipv4index + (ipnum1n2 << 3);

    low  = IP2Location_read32 (loc, indexpos);
    high = IP2Location_read32 (loc, indexpos + 4);
  }

  while (low <= high)
  {
    uint32_t column = dbcolumn * 4;

    mid    = (uint32_t) ((low + high) >> 1);
    ipfrom = IP2Location_read32 (loc, baseaddr + mid * column);
    ipto   = IP2Location_read32 (loc, baseaddr + (mid + 1) * column);

    num_4_loops++;

    if (ipno >= ipfrom && ipno < ipto)
    {
      IP2Location_read_record (loc, baseaddr + (mid * column), mode, out);
      return (TRUE);
    }

    if (ipno < ipfrom)
         high = mid - 1;
    else low  = mid + 1;
  }
  return (FALSE);
}

/**
 * Get record for a IPv6 from database
 */
static BOOL IP2Location_get_ipv6_record (IP2Location *loc, uint32_t mode, ipv_t parsed_ipv, struct ip2loc_entry *out)
{
  uint32_t baseaddr  = loc->ipv6_db_addr;
  uint32_t dbcolumn  = loc->db_column;
  uint32_t ipv6index = loc->ipv6_index_db_addr;
  uint32_t low       = 0;
  uint32_t mid       = 0;
  uint32_t high      = loc->ipv6_db_count;
  struct in6_addr ipfrom, ipto, ipno;

  ipno = parsed_ipv.ipv6;
  num_6_loops = 0;

  if (!high)
      return (FALSE);

  if (ipv6index > 0)
  {
    /* use the index table
     */
    uint32_t ipnum1   = (ipno.u.Byte[0] * 256) + ipno.u.Byte[1];
    uint32_t indexpos = ipv6index + (ipnum1 << 3);

    low  = IP2Location_read32 (loc, indexpos);
    high = IP2Location_read32 (loc, indexpos + 4);
  }

  while (low <= high)
  {
    uint32_t column = dbcolumn * 4 + 12;

    mid = (uint32_t) ((low + high) >> 1);
    IP2Location_read_ipv6_addr (loc, baseaddr + mid * column, &ipfrom);
    IP2Location_read_ipv6_addr (loc, baseaddr + (mid + 1) * column, &ipto);

    num_6_loops++;

    if ((ipv6_compare(&ipno, &ipfrom) >= 0) && ipv6_compare(&ipno, &ipto) < 0)
    {
      IP2Location_read_record (loc, baseaddr + mid * column + 12, mode, out);
      return (TRUE);
    }

    if (ipv6_compare(&ipno, &ipfrom) < 0)
         high = mid - 1;
    else low = mid + 1;
  }
  return (FALSE);
}

/**
 * Return API version as string.
 */
static const char *IP2Location_api_version_str (void)
{
  #define _STR2(x) #x
  #define _STR(x)  _STR2(x)

  return (_STR(API_VERSION_MAJOR) "." _STR(API_VERSION_MINOR) "." _STR(API_VERSION_MICRO));
}

/**
 * Load the DB file into shared/cache memory.
 */
static int32_t IP2Location_DB_Load_to_mem (IP2Location *loc, HANDLE os_hnd)
{
  size_t read;

  if (os_hnd != INVALID_HANDLE_VALUE)
     return (0);

  if (fseek(loc->file, SEEK_SET, 0) < 0)
  {
    TRACE (2, "fseek() failed: errno=%d\n", errno);
    return (-1);
  }

  read = fread (loc->sh_mem_ptr, 1, loc->stat_buf.st_size, loc->file);
  if (read != loc->stat_buf.st_size)
  {
    TRACE (2, "fread() failed, read=%d, loc->stat_buf.st_size=%u, errno=%d\n",
           (int)read, (unsigned)loc->stat_buf.st_size, errno);
    return (-1);
  }
  return (0);
}

/**
 * Set the DB access method as shared memory.
 */
static int32_t IP2Location_DB_set_shared_memory (IP2Location *loc)
{
  FILE  *file = loc->file;
  int    fd = fileno (file);
#if 1
  DWORD  os_prot = PAGE_READWRITE;
  DWORD  os_map  = FILE_MAP_WRITE;
  HANDLE os_hnd  = INVALID_HANDLE_VALUE;
#else  /* \todo xxx */
  DWORD  os_prot = PAGE_READONLY | SEC_LARGE_PAGES;
  DWORD  os_map  = FILE_MAP_READ;
  HANDLE os_hnd  = (HANDLE) _get_osfhandle (fileno(file));
#endif

  if (fstat(fd, &loc->stat_buf) == -1)
  {
    TRACE (2, "fstat() failed: errno=%d\n", errno);
    return (-1);
  }

  if (loc->stat_buf.st_size == 0)
  {
    TRACE (1, "IP2Loc file is 0 bytes.\n");
    return (-1);
  }

  SetLastError (0);
  loc->sh_mem_fd = CreateFileMapping (os_hnd, NULL, os_prot, 0,
                                      loc->stat_buf.st_size+1, "IP2location_Shm");
  if (!loc->sh_mem_fd)
  {
    TRACE (2, "CreateFileMapping() failed: %s\n", win_strerror(GetLastError()));
    return (-1);
  }

  loc->sh_mem_already = (GetLastError() == ERROR_ALREADY_EXISTS);

  loc->sh_mem_ptr = MapViewOfFile (loc->sh_mem_fd, os_map, 0, 0, 0);

  if (!loc->sh_mem_ptr)
  {
    TRACE (2, "MapViewOfFile() failed: %s\n", win_strerror(GetLastError()));
    return (-1);
  }

  if (loc->sh_mem_already)
     TRACE (2, "CreateFileMapping() already exist. Sharing 0x%p file-mapping with another process.\n",
            loc->sh_mem_ptr);
  else
  {
    if (IP2Location_DB_Load_to_mem(loc, os_hnd) == -1)
       return (-1);
  }

  loc->sh_mem_max = (uint64)loc->sh_mem_ptr + loc->stat_buf.st_size;
  return (0);
}

static void IP2Location_read_ipv6_addr (IP2Location *loc, uint32_t position, struct in6_addr *addr)
{
  int i, j;

  for (i = 0, j = 15; i < 16; i++, j--)
      addr->u.Byte[i] = IP2Location_read8 (loc, position + j);
}

/**
 * Read a 32-bit value from shared-memory.
 */
static uint32_t IP2Location_read32 (IP2Location *loc, uint32_t position)
{
  uint8_t byte1, byte2, byte3, byte4;

  if ((uint64)loc->sh_mem_ptr + position >= loc->sh_mem_max)
  {
    loc->sh_mem_index_errors++;
    return (0UL);
  }
  byte1 = loc->sh_mem_ptr [position-1];
  byte2 = loc->sh_mem_ptr [position];
  byte3 = loc->sh_mem_ptr [position+1];
  byte4 = loc->sh_mem_ptr [position+2];
  return ((byte4 << 24) | (byte3 << 16) | (byte2 << 8) | byte1);
}

/**
 * Read a 8-bit value from shared-memory.
 */
static uint8_t IP2Location_read8 (IP2Location *loc, uint32_t position)
{
  uint8_t ret = 0;

  if ((uint64)loc->sh_mem_ptr + position - 1 <= loc->sh_mem_max)
       ret = loc->sh_mem_ptr [position-1];
  else loc->sh_mem_index_errors++;
  return (ret);
}

/**
 * Read a float value from shared-memory.
 */
static float IP2Location_read_float (IP2Location *loc, uint32_t position)
{
  float ret = 0.0;

  if ((uint64)loc->sh_mem_ptr + position >= loc->sh_mem_max)
  {
    loc->sh_mem_index_errors++;
    return (0.0);
  }
  memcpy (&ret, &loc->sh_mem_ptr[position-1], 4);
  return (ret);
}


/**
 * This avoids the call to `inet_pton()` since the passed `addr`
 * should be a valid IPv4-address.
 */
BOOL ip2loc_get_ipv4_entry (const struct in_addr *addr, struct ip2loc_entry *out)
{
  ipv_t parsed_ipv;

  parsed_ipv.ip_ver = 4;
  parsed_ipv.ipv4   = _byteswap_ulong (addr->s_addr);

  memset (out, '\0', sizeof(*out));
  if (!IP2Location_get_ipv4_record(ip2loc_handle, lookup_flags, parsed_ipv, out))
     return (FALSE);

  TRACE (3, "Record for IPv4-number %s; country_short: \"%.2s\", num_4_loops: %lu.\n",
         INET_util_get_ip_num(addr, NULL), out->country_short, DWORD_CAST(num_4_loops));
  return (out->country_short[0] != '\0' && out->country_short[1] != '\0');
}

/**
 * This avoids the call to `inet_pton()` since the passed
 * `addr` should be a valid IPv6-address.
 */
BOOL ip2loc_get_ipv6_entry (const struct in6_addr *addr, struct ip2loc_entry *out)
{
  ipv_t parsed_ipv;

  if (IN6_IS_ADDR_V4MAPPED(addr))
  {
    parsed_ipv.ip_ver = 4;
    parsed_ipv.ipv4   = *(const uint32_t*) addr;
  }
  else
  {
    parsed_ipv.ip_ver = 6;
    memcpy (&parsed_ipv.ipv6, addr, sizeof(parsed_ipv.ipv6));
  }

  memset (out, '\0', sizeof(*out));
  if (!IP2Location_get_ipv6_record(ip2loc_handle, lookup_flags, parsed_ipv, out))
     return (FALSE);

  TRACE (3, "Record for IPv6-number %s; country_short: \"%.2s\", num_6_loops: %lu.\n",
         INET_util_get_ip_num(NULL, (const struct in6_addr*)&parsed_ipv.ipv6),
         out->country_short, DWORD_CAST(num_6_loops));
  return (out->country_short[0] != '\0' && out->country_short[1] != '\0');
}

/**
 * Return number of index-errors to the shared-memory area.
 *
 * This seems to be an issue when >= 2 processes are accessing the
 * `g_cfg.GEOIP.ip2location_bin_file` at the same time. The latter process
 * (using wsock_trace.dll) will get junk returned for locations.
 * This is just an attempt to detect it.
 */
DWORD ip2loc_index_errors (void)
{
  if (ip2loc_handle)
     return (DWORD) ip2loc_handle->sh_mem_index_errors;
  return (0);
}
