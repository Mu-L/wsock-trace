/**\file    dnsbl.c
 * \ingroup DNSBL
 *
 * \brief
 *   A simple DNSBL (Domain Name System Blacklist) implementation.
 *   Parses and uses the the Spamhaus DROP / DROPv6 files to
 *   check an IPv4/IPv6-address for membership of a "spam network".
 *   Used in dump.c to print the SBL (Spamhaus Block Reference)
 *   if found in the `DNSBL_list` smartlist.
 *
 * Ref:
 *   http://www.spamhaus.org/drop/
 *
 * Some other black-list providers and links:
 * ```
 *  b.barracudacentral.org
 *  bl.blocklist.de
 *  bl.spamcop.net
 *  cbl.abuseat.org
 *  dnsbl-1.uceprotect.net
 *  dnsbl-2.uceprotect.net
 *  dnsbl-3.uceprotect.net
 *  dnsbl.abuse.ch
 *  ix.dnsbl.manitu.net
 *  pbl.spamhaus.org
 *  sbl.spamhaus.org
 *  xbl.spamhaus.org
 *  zen.spamhaus.org
 * ```
 *
 * By Gisle Vanem <gvanem@yahoo.no> August 2017.
 */

#include "common.h"
#include "init.h"
#include "smartlist.h"
#include "geoip.h"
#include "getopt.h"
#include "inet_addr.h"
#include "inet_util.h"
#include "dnsbl.h"

typedef enum {
        DNSBL_DROP,
        DNSBL_DROPv6,
        DNSBL_MAX
      } DNSBL_type;

struct DNSBL_info {
       union {
         struct {
           struct in_addr network;
           struct in_addr mask;
         } ip4;
         struct {
           struct in6_addr network;
           struct in6_addr mask;
         } ip6;
       } u;
       unsigned   bits;
       int        family;  /* AF_INET or AF_INET6 */
       DNSBL_type type;
       char       SBL_ref[10];
     };

static smartlist_t *DNSBL_list = NULL;

static void DNSBL_parse_DROP   (smartlist_t *sl, const char *line);
static void DNSBL_parse_DROPv6 (smartlist_t *sl, const char *line);

static const char *DNSBL_type_name (DNSBL_type type)
{
  return (type == DNSBL_DROP   ? "DROP"   :
          type == DNSBL_DROPv6 ? "DROPv6" : "?");
}

/**
 * `smartlist_sort()` helper; compare on network.
 *
 * This compares both `DNSBL_info*` nodes with `family == AF_INET`
 * and `family == AF_INET6`.
 */
static int DNSBL_compare_net (const void **_a, const void **_b)
{
  const struct DNSBL_info *a = *_a;
  const struct DNSBL_info *b = *_b;

  if (a->family != b->family)
  {
    /* This will force all AF_INET6 addresses after
     * AF_INET addresses; 2 - 23 < 0.
     */
     return (a->family - b->family);
   }

  if (a->family == AF_INET)
  {
    DWORD a_net = swap32 (a->u.ip4.network.s_addr);
    DWORD b_net = swap32 (b->u.ip4.network.s_addr);

    if (a_net < b_net)
       return (-1);
    if (a_net > b_net)
       return (1);
  }
  else
  {
    const struct in6_addr *a_net = &a->u.ip6.network;
    const struct in6_addr *b_net = &b->u.ip6.network;

    return memcmp (a_net, b_net, sizeof(*a_net));
  }
  return (0);
}

/**
 * `smartlist_bsearch()` helper; compare on IPv4 network range.
 *
 * \note the `mask`, `start_ip` and `end_ip` are all on network order.
 */
static int DNSBL_compare_is_on_net4 (const void *key, const void **member)
{
  const struct DNSBL_info *dnsbl = *member;
  const struct in_addr    *ia    = key;
  int   rc;

  if (dnsbl->family != AF_INET)
  {
    /* Since AF_INET6 networks are sorted last in 'DNSBL_list', force
     * 'smartlist_bsearch_idx()' to look closer to index 0.
     */
    TRACE (3, "Wrong family\n");
    return (-1);
  }

  rc = INET_util_range4cmp (ia, &dnsbl->u.ip4.network, dnsbl->bits);

  if (g_cfg.trace_level >= 3)
  {
    char  net_str     [MAX_IP4_SZ+1];
    char  mask_str    [MAX_IP4_SZ+1];
    char  ip_str      [MAX_IP4_SZ+1];
    char  start_ip_str[MAX_IP4_SZ+1];
    char  end_ip_str  [MAX_IP4_SZ+1];
    DWORD mask     = dnsbl->u.ip4.mask.s_addr;
    DWORD netw     = dnsbl->u.ip4.network.s_addr;
    DWORD start_ip = netw & mask;
    DWORD end_ip   = start_ip | ~mask;

    INET_addr_ntop (AF_INET, &dnsbl->u.ip4.network, net_str, sizeof(net_str), NULL);
    INET_addr_ntop (AF_INET, &dnsbl->u.ip4.mask, mask_str, sizeof(mask_str), NULL);
    INET_addr_ntop (AF_INET, &ia->s_addr, ip_str, sizeof(ip_str), NULL);
    INET_addr_ntop (AF_INET, &start_ip, start_ip_str, sizeof(start_ip_str), NULL);
    INET_addr_ntop (AF_INET, &end_ip, end_ip_str, sizeof(end_ip_str), NULL);

    TRACE (3, "ip: %-15s net: %-15s (%-12s - %-15s) mask: %-15s rc: %d\n",
           ip_str, net_str, start_ip_str, end_ip_str, mask_str, rc);
  }
  return (rc);
}

/**
 * `smartlist_bsearch()` helper; compare on IPv6 network range.
 *
 * \note the `mask`, `start_ip` and `end_ip` are all on network order.
 */
static int DNSBL_compare_is_on_net6 (const void *key, const void **member)
{
  const struct DNSBL_info *dnsbl = *member;
  const struct in6_addr   *ia    = key;
  int    rc = -1;

  if (dnsbl->family != AF_INET6)
  {
    /* Since AF_INET6 networks are sorted last in 'DNSBL_list', force
     * 'smartlist_bsearch_idx()' to look closer to the end-index.
     */
    TRACE (3, "Wrong family\n");
    return (1);
  }

  rc = INET_util_range6cmp (ia, &dnsbl->u.ip6.network, dnsbl->bits);

  if (g_cfg.trace_level >= 3)
  {
    struct in6_addr start_ip, end_ip;
    char   net_str     [MAX_IP6_SZ+1];
    char   mask_str    [MAX_IP6_SZ+1];
    char   ip_str      [MAX_IP6_SZ+1];
    char   start_ip_str[MAX_IP6_SZ+1];
    char   end_ip_str  [MAX_IP6_SZ+1];

    INET_addr_ntop (AF_INET6, &dnsbl->u.ip6.network, net_str, sizeof(net_str), NULL);
    INET_addr_ntop (AF_INET6, &dnsbl->u.ip6.mask, mask_str, sizeof(mask_str), NULL);
    INET_addr_ntop (AF_INET6, &ia->s6_addr, ip_str, sizeof(ip_str), NULL);
    INET_addr_ntop (AF_INET6, &start_ip, start_ip_str, sizeof(start_ip_str), NULL);
    INET_addr_ntop (AF_INET6, &end_ip, end_ip_str, sizeof(end_ip_str), NULL);

    TRACE (3, "ip: %-20s net: %-20s (%-20s - %-30s)\n"
               "                mask: 0x%s, rc: %d\n",
           ip_str, net_str, start_ip_str, end_ip_str, mask_str, rc);
  }
  return (rc);
}

/**
 * Do a binary search in the `DNSBL_list` to figure out if
 * `ip4` or `ip6` address is a member of a **spam group**.
 *
 * \note An IPv4/IPv6 address can have more than 1 SBL reference.
 *       This is currently unsupported.
 *       \eg{.}:
 *       Currently (as of January 2025), the IPv4 block `24.233.0.0/19`
 *       is listed in `drop.txt` as:
 *       ```
 *         24.233.0.0/19 ; SBL210084
 *       ```
 */
static bool DNSBL_check_common (const struct in_addr *ip4, const struct in6_addr *ip6, const char **sbl_ref)
{
  const struct DNSBL_info *dnsbl;

  if (sbl_ref)
     *sbl_ref = NULL;

  if (!DNSBL_list)
     return (false);

  dnsbl = smartlist_bsearch (DNSBL_list,
                             ip4 ? (const void*)ip4         : (const void*)ip6,
                             ip4 ? DNSBL_compare_is_on_net4 : DNSBL_compare_is_on_net6);
  if (sbl_ref && dnsbl)
     *sbl_ref = dnsbl->SBL_ref;

  return (dnsbl ? true : false);
}

bool DNSBL_check_ipv4 (const struct in_addr *ip4, const char **sbl_ref)
{
  if (!INET_util_addr_is_global(ip4, NULL))
     return (false);
  return DNSBL_check_common (ip4, NULL, sbl_ref);
}

bool DNSBL_check_ipv6 (const struct in6_addr *ip6, const char **sbl_ref)
{
  if (!INET_util_addr_is_global(NULL, ip6))
     return (false);
  return DNSBL_check_common (NULL, ip6, sbl_ref);
}

/**
 * Simply prints all the members of the `DNSBL_list` smartlist.
 */
static void DNSBL_dump (void)
{
  int i, max = DNSBL_list ? smartlist_len(DNSBL_list) : 0;
  const char *head_fmt = "%4s  SBL%-6s  %-20s %-20s %s\n";
  const char *line_fmt = "%4d: SBL%-6s  %-20s %-20s %s\n";

  C_puts ("DNSBL_dump():\n");
  C_printf (head_fmt, "Num", "-ref", "Network", "Mask", "Type");

  for (i = 0; i < max; i++)
  {
    const struct DNSBL_info *dnsbl = smartlist_get (DNSBL_list, i);
    char  addr [MAX_IP6_SZ+1];
    char  mask [MAX_IP6_SZ+1];
    char  cidr [MAX_IP6_SZ+11];

    if (dnsbl->family == AF_INET)
    {
      INET_addr_ntop (AF_INET, &dnsbl->u.ip4.network, addr, sizeof(addr), NULL);
      INET_addr_ntop (AF_INET, &dnsbl->u.ip4.mask, mask, sizeof(mask), NULL);
    }
    else
    {
      INET_addr_ntop (AF_INET6, &dnsbl->u.ip6.network, addr, sizeof(addr), NULL);
      INET_addr_ntop (AF_INET6, &dnsbl->u.ip6.mask, mask, sizeof(mask), NULL);
    }
    snprintf (cidr, sizeof(cidr), "%s/%u", addr, dnsbl->bits);
    C_printf (line_fmt, i, dnsbl->SBL_ref[0] ? dnsbl->SBL_ref : "<none>",
              cidr, mask, DNSBL_type_name(dnsbl->type));
  }
}

/**
 * Test a single IPv4/6 address for DNSBL-membership.
 */
static int DNSBL_test_single (const char *addr_str)
{
  char addr_buf [MAX_IP6_SZ+3];
  union {
    struct in_addr  ip4;
    struct in6_addr ip6;
  } addr;
  const char *sbl_ref = NULL;
  const char *remark;
  int   special, rc;

  snprintf (addr_buf, sizeof(addr_buf), "\"%s\"", addr_str);
  if (INET_addr_pton2(AF_INET, addr_str, &addr.ip4) == 1)
  {
    special = INET_util_addr_is_special (&addr.ip4, NULL, &remark);
    rc = DNSBL_check_common (&addr.ip4, NULL, &sbl_ref);
  }
  else if (INET_addr_pton2(AF_INET6, addr_str, &addr.ip6) == 1)
  {
    special = INET_util_addr_is_special (NULL, &addr.ip6, &remark);
    rc = DNSBL_check_common (NULL, &addr.ip6, &sbl_ref);
  }
  else
  {
    C_printf ("Invalid address: %s.\n", addr_buf);
    return (0);
  }

  if (special)
     C_printf ("Address: %s is special; %s.\n", addr_buf, remark);
  else if (sbl_ref)
       C_printf ("Address: %s is listed as %s.\n", addr_buf, sbl_ref);
  else C_printf ("Address: %s is not listed in any block-list.\n", addr_buf);
  return (rc);
}

/*
 * Test some lines from drop.txt:
 *   108.166.224.0/19 ; SBL235333
 *   24.51.0.0/19     ; SBL293696
 *   193.25.48.0/20   ; SBL211796
 *
 * Verify that Google's NS is not in any DNSBL
 *   8.8.8.8/xx
 *
 * Test some lines from dropv6.txt:
 *   2a06:5280::/29 ; SBL334219
 *   2607:d100::/32 ; SBL347495
 *
 * Further details for the SBL-reference is found from:
 *   https://www.spamhaus.org/sbl/query/SBL<xxx>
 */
struct test_list {
       int         family;
       const char *addr;
       const char *sbl_ref;
     };

static int DNSBL_test (const char *addr_str)
{
  int    i, rc;
  WORD   save1, save4, save5;
  static const struct test_list tests[] = {
                    { AF_INET,  "192.219.208.2", "103496" },  /* in drop.txt */
                    { AF_INET,  "24.233.0.21",   "210084" },
                    { AF_INET,  "8.8.8.8",       "<none>" },  /* Google's NS */
                    { AF_INET,  "193.25.48.3",   "211796" },
                    { AF_INET6, "2a06:e480::1",  "301771" },  /* in dropv6.txt */
                    { AF_INET6, "2a06:e480::ff", "301771" },
                    { AF_INET6, "2607:d100::1",  "347495" }
                  };
  const struct test_list *test = tests;

  if (addr_str)
     return DNSBL_test_single (addr_str);

  /* Save some colors and temporary change them
   */
  save1 = g_cfg.color_trace;
  save4 = g_cfg.color_data;
  save5 = g_cfg.color_func;
  get_color ("bright white", &g_cfg.color_trace);
  get_color ("bright green", &g_cfg.color_data);
  get_color ("bright red", &g_cfg.color_func);

  if (g_cfg.DNSBL.drop_file && file_exists(g_cfg.DNSBL.drop_file))
     INET_util_test_mask4();

  if (g_cfg.DNSBL.dropv6_file && file_exists(g_cfg.DNSBL.dropv6_file))
     INET_util_test_mask6();

  C_puts ("Running ~2DNSBL_test():~0\n");
  for (i = rc = 0; i < DIM(tests); i++, test++)
  {
    union {
      struct in_addr  ip4;
      struct in6_addr ip6;
    } addr;
    const char *sbl_ref = NULL;
    const char *country_code, *location, *okay;
    bool        res;

    INET_addr_pton2 (test->family, test->addr, &addr);

    if (test->family == AF_INET)
    {
      country_code = geoip_get_country_by_ipv4 (&addr.ip4);
      location     = geoip_get_location_by_ipv4 (&addr.ip4);
      res = DNSBL_check_ipv4 (&addr.ip4, &sbl_ref);
    }
    else
    {
      country_code = geoip_get_country_by_ipv6 (&addr.ip6);
      location     = geoip_get_location_by_ipv6 (&addr.ip6);
      res = DNSBL_check_ipv6 (&addr.ip6, &sbl_ref);
    }

    if (!sbl_ref)
       sbl_ref = "<none>";

    if (!strcmp(sbl_ref, test->sbl_ref))
         okay = "~4success";
    else okay = "~5failed ";

    if (res)
       rc++;

    C_printf ("~1%-15s~0 -> %d, ~1SBL%-7s~0 %s~0  country: %s, location: %s~0\n",
              test->addr, res, sbl_ref, okay, country_code, location);
  }
  g_cfg.color_trace = save1;
  g_cfg.color_data  = save4;
  g_cfg.color_func  = save5;
  return (rc);
}

/**
 * Load and parse a DROP file.
 * On first call, the `*prev` list is empty.
 * Thus on the 2nd etc. call, this function appends the current smart-list to `*prev`
 * and frees the current smart-list.
 */
static void DNSBL_parse_and_add (smartlist_t **prev, const char *file, smartlist_parse_func parser)
{
  if (file)
  {
    smartlist_t *sl = smartlist_read_file (file, parser);

    if (!sl)
       return;

    if (*prev)
    {
      smartlist_append (*prev, sl);
      smartlist_free (sl);
    }
    else
      *prev = sl;
  }
}

/**
 * Called from init.c / `wsock_trace_init()`.
 *
 * This function is called before `load_ws2_funcs()` that dynamically loads
 * all Winsock functions. That means we must NOT call any true Winsock functions.
 * But instead use private functions like `INET_addr_pton()`.
 *
 * Since the `DNSBL_test()` test function will call `inet_addr()` (at `trace_level >= 3`)
 * via `IP2Location_get_record()`, the `DNSBL_test()` must be postponed to a later
 * stage in `wsock_trace_init()`.
 */
void DNSBL_init (void)
{
  if (!g_cfg.DNSBL.enable)
  {
    TRACE (2, "g_cfg.DNSBL.enable = 0\n");
    return;
  }

  DNSBL_parse_and_add (&DNSBL_list, g_cfg.DNSBL.drop_file, DNSBL_parse_DROP);
  DNSBL_parse_and_add (&DNSBL_list, g_cfg.DNSBL.dropv6_file, DNSBL_parse_DROPv6);

  /* Each of the 'drop.txt', 'dropv6.txt' are already sorted.
   * But after merging them into one list, we must sort them ourself.
   */
  if (DNSBL_list)
     smartlist_sort (DNSBL_list, DNSBL_compare_net);
}

void DNSBL_exit (void)
{
  smartlist_wipe (DNSBL_list, free);
  DNSBL_list = NULL;
}

/**
 * Check if `fname` needs an update.
 * If it does (or if it is truncated or does not exist), download it using
 * WinInet.dll and `touch` it.
 */
static int DNSBL_update_file (const char *fname, const char *tmp_file, const char *url, time_t now, time_t expiry)
{
  struct stat st;
  time_t      when;

  if (!fname || !tmp_file || !url)
     return (0);

  if (stat(tmp_file, &st) != 0)
  {
    st.st_mtime = 0;
    TRACE (2, "File \"%s\" doesn't exist. Forcing a download from \"%s\".\n", tmp_file, url);
  }
  else if (st.st_size == 0)
  {
    TRACE (2, "Updating truncated \"%s\" from \"%s\"\n", tmp_file, url);
  }
  else
  {
    /* Currently for an AppVeyor build, 'g_cfg.DNSBL.max_days' is '0'.
     * (max_days = 0 in the "[dnsbl]" section in the "wsock_trace.appveyor" file).
     * This means these "*drop*.txt" files would be downloaded from SpamHaus's
     * server immediately after being checked out from GitHub!
     * Therefore, give a 10 sec time slack.
     */
    expiry -= 10;

    if (expiry && st.st_mtime > expiry)
    {
      when = now + g_cfg.DNSBL.max_days * 24 * 3600;
      TRACE (2, "Update of \"%s\" not needed until \"%.24s\"\n",
             tmp_file, ctime(&when));
      return (0);
    }

    if (st.st_mtime > 0)
       TRACE (2, "Updating \"%s\" from \"%s\"\n", tmp_file, url);
  }

  if (INET_util_download_file(tmp_file, url) > 0)
  {
    TRACE (1, "%s -> %s\n", tmp_file, fname);
    CopyFile (tmp_file, fname, FALSE);
    INET_util_touch_file (fname);
    return (1);
  }
  return (0);
}

/**
 * Check all `*drop*.txt` files based on file's timestamp. The expiriry in the
 * file header is a bit too hard to parse. Simply use `g_cfg.DNSBL.max_days`
 * and download it if it's too old.
 */
int DNSBL_update_files (bool force_update)
{
  char   tmp_file [_MAX_PATH];
  time_t now, expiry;
  int    num = 0;

  if (!g_cfg.DNSBL.enable)
  {
    TRACE (2, "g_cfg.DNSBL.enable = 0\n");
    return (0);
  }

  now = time (NULL);
  expiry = now;
  if (!force_update)
     expiry -= g_cfg.DNSBL.max_days * 24 * 3600;

  snprintf (tmp_file, sizeof(tmp_file), "%s\\%s", g_data.ws_tmp_dir, basename(g_cfg.DNSBL.drop_file));
  num += DNSBL_update_file (g_cfg.DNSBL.drop_file, tmp_file, g_cfg.DNSBL.drop_url, now, expiry);

  snprintf (tmp_file, sizeof(tmp_file), "%s\\%s", g_data.ws_tmp_dir, basename(g_cfg.DNSBL.dropv6_file));
  num += DNSBL_update_file (g_cfg.DNSBL.dropv6_file, tmp_file, g_cfg.DNSBL.dropv6_url, now, expiry);

  return (num);
}

/**
 * Parser for "drop.txt" file.
 */
static void DNSBL_parse_DROP (smartlist_t *sl, const char *line)
{
  struct DNSBL_info *dnsbl;
  int                bits = 0;
  char               addr [MAX_IP6_SZ+1]; /* In case an IPv6-address shows up */

  if (sscanf(line, "%[0-9.]/%d ; SBL", addr, &bits) != 2)
     return;

  if (bits < 8 || bits > 32) /* Cannot happen */
     return;

  dnsbl = malloc (sizeof(*dnsbl));
  if (!dnsbl)
     return;

  INET_addr_pton2 (AF_INET, addr, &dnsbl->u.ip4.network);
  INET_util_get_mask4 (&dnsbl->u.ip4.mask, bits);

  dnsbl->bits   = bits;
  dnsbl->type   = DNSBL_DROP;
  dnsbl->family = AF_INET;

  str_ncpy (dnsbl->SBL_ref, strchr(line, 'L') + 1, sizeof(dnsbl->SBL_ref));
  smartlist_add (sl, dnsbl);
}

/**
 * Parser for a "dropv6.txt" file.
 */
static void DNSBL_parse_DROPv6 (smartlist_t *sl, const char *line)
{
  struct DNSBL_info *dnsbl;
  int                bits = 0;
  char               addr [MAX_IP6_SZ+1];

  if (sscanf(line, "%[a-f0-9:]/%d ; SBL", addr, &bits) != 2)
     return;

  if (bits < 8)   /* Cannot happen */
     return;

  dnsbl = malloc (sizeof(*dnsbl));
  if (!dnsbl)
     return;

  INET_addr_pton2 (AF_INET6, addr, &dnsbl->u.ip6.network);
  INET_util_get_mask6 (&dnsbl->u.ip6.mask, bits);

  dnsbl->bits   = bits;
  dnsbl->type   = DNSBL_DROPv6;
  dnsbl->family = AF_INET6;

  str_ncpy (dnsbl->SBL_ref, strchr(line, 'L') + 1, sizeof(dnsbl->SBL_ref));
  smartlist_add (sl, dnsbl);
}

/*
 * A small test for DNSBL.
 */
static int show_help (void)
{
  printf ("Usage: %s [-Dftu] <address>\n"
          "       -D:  run 'DNSBL_dump()' to dump the DNSBL list.\n"
          "       -f:  force an update with the '-u' option.\n"
          "       -t:  run 'DNSBL_test()' for a simple test.\n"
          "            if an <address> is specified, test that.\n"
          "       -u:  update the SpamHaus' 'DROP.txt' and 'DROPv6.txt' files.\n",
          g_data.program_name);
  return (0);
}

int dnsbl_main (int argc, char **argv)
{
  int ch, do_dump = 0, do_force = 0, do_test = 0, do_update = 0;

  set_program_name (argv[0]);

  while ((ch = getopt(argc, argv, "Dftuh?")) != EOF)
     switch (ch)
     {
       case 'D':
            do_dump = 1;
            break;
       case 'f':
            do_force = 1;
            break;
       case 't':
            do_test = 1;
            break;
       case 'u':
            do_update = 1;
            break;
       case '?':
       case 'h':
       default:
            return show_help();
  }

  if (do_test || do_dump || do_update)
     g_cfg.DNSBL.enable = 1;

  argc -= optind;
  argv += optind;

  if (do_test)
     DNSBL_test (*argv);
  else if (do_dump)
     DNSBL_dump();
  else if (do_update)
  {
    int save = g_cfg.trace_level;

    g_cfg.trace_level = 2;
    DNSBL_update_files (do_force);
    g_cfg.trace_level = save;
  }
  else
    printf ("Nothing done in %s.\n", g_data.program_name);

  return (0);
}
