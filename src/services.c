/**\file    services.c
 * \ingroup inet_util
 *
 * \brief
 * Parsing of Windows or Wireshark `services` files for wsock_trace.
 *
 * By Gisle Vanem <gvanem@yahoo.no> 2022.
 */
#include "common.h"
#include "init.h"
#include "smartlist.h"
#include "csv.h"
#include "wsock_trace.h"
#include "services.h"

/**
 * \def MAX_SERV_LEN
 * Maximum length of a service entry.
 * In WireShark's `services` file, the longest is `"subntbcst-tftp"`.
 * 15 characters.
 */
#define MAX_SERV_LEN   20

/**
 * \def MAX_PROTO_LEN
 * Maximum length of a protocol name. Currently `"sctp"`.
 */
#define MAX_PROTO_LEN  10

/**
 * \def MAX_PROTOS_LEN
 * Maximum combined protocols length. Like `"/tcp/udp/sctp/dccp"`.
 */
#define MAX_PROTOS_LEN 30

/**
 * \def PROTO_UDP
 * The bitvalue for `udp`
 *
 * \def PROTO_TCP
 * The bitvalue for `tcp`
 *
 * \def PROTO_DCCP
 * The bitvalue for `dccp` (Datagram Congestion Control Protocol)
 *
 * \def PROTO_SCTP
 * The bitvalue for `sctp` (Stream Control Transmission Protocol)
 */
#define PROTO_UNKNOWN 0x01
#define PROTO_UDP     0x02
#define PROTO_TCP     0x04
#define PROTO_DCCP    0x08
#define PROTO_SCTP    0x10

/**
 * The protocol list and their names for decoding.
 */
static const struct search_list protocol_list[] = {
                   { PROTO_UNKNOWN, "?"    },
                   { PROTO_UDP,     "udp"  },
                   { PROTO_TCP,     "tcp"  },
                   { PROTO_DCCP,    "dccp" },
                   { PROTO_SCTP,    "sctp" }
                 };

/*
 * \todo
 * Support a download and parsing of this too:
 *   http://www.iana.org/assignments/service-names-port-numbers/service-names-port-numbers.csv
 *
 * \todo
 * Handle other line formats besides Wireshark's format in parse_port_proto
 */

/**
 * \struct service_entry
 * The structure for service-entries we read from a file.
 */
struct service_entry {
       char        name [MAX_SERV_LEN];  /**< name of `services` entry */
       uint16_t    port;                 /**< the port on host order */
       int         proto;                /**< A bitset of PROTO_UDP, PROTO_TCP etc. */
       const char *file;                 /**< which `g_cfg.services_file[]` is this entry from? */
     };

/**
 * The smartlist of services entries.
 */
static smartlist_t *services_list;

/**
 * The current services file we are parsing. <br>
 * In range `[0 ... DIM(g_cfg.services_file)-1]` == [0 ... 2].
 */
static int current_services_file;

/**
 * The filled structure from `fill_servent()`.
 */
static struct servent ret_fill_servent;

/**
 * Add an entry to the given `smartlist_t` that becomes `services_list`.
 */
static void add_entry (const struct service_entry *se)
{
  struct service_entry *copy = calloc (1, sizeof(*copy));

  if (copy)
  {
    *copy = *se;
    copy->file = g_cfg.services_file [current_services_file];
    smartlist_add (services_list, copy);
  }
}

/**
 * Decode a protocol into a string. Like `(PROTO_UDP | _PROTO_TCP)` -> `udp|tcp`.
 */
static const char *decode_proto_str (int protocol)
{
  return flags_decode (protocol, protocol_list, DIM(protocol_list));
}

/**
 * Encode a `proto_str` value into a bitvalue. The opposite of decode_proto_str().
 *
 * \param[in] proto_str     the protocol string to parse.
 * \param[in] multi_fields  if TRUE, parse a string like `udp/tcp` recursively into
 *                          a bitvalue as `(PROTO_UDP | PROTO_TCP)`.
 */
static int encode_proto_str (const char *proto_str, BOOL multi_fields)
{
  UINT protocol;
  int  rc;

  if (!multi_fields)
  {
    protocol = list_lookup_value (proto_str, protocol_list, DIM(protocol_list));
    if (protocol == UINT_MAX)
         rc = PROTO_UNKNOWN;
    else rc = protocol;
  }
  else
  {
    char *end, *tok;
    char  copy [MAX_PROTOS_LEN];
    int   i;

    _strlcpy (copy, proto_str, sizeof(copy));
    rc = PROTO_UNKNOWN;
    i = 0;
    for (tok = _strtok_r (copy, "/", &end); tok;
         tok = _strtok_r (NULL, "/", &end), i++)
    {
      TRACE (3, "tok[%d]: '%s'.\n", i, tok);
      rc |= encode_proto_str (tok, FALSE);
    }
    TRACE (3, "rc: 0x%02X.\n", rc);
  }
  return (rc);
}

/**
 * Compare 2 protocol bitvalues.
 * \retval 0  if bit in `proto_a` matches bit in `proto_b`.
 * \retval -1 if `proto_a` contains fewer protocols than `proto_b`.
 * \retval +1 if `proto_a` contains more protocols than `proto_b`.
 *
 * Hence TCP gets higher rank than UDP.
 */
static int compare_proto (int proto_a, int proto_b)
{
  if (proto_a & proto_b)
     return (0);
  if (proto_a < proto_b)
     return (-1);
  if (proto_a > proto_b)
     return (1);
  return (0);
}

/**
 * `smartlist_sort()` and `smartlist_make_uniq()` helper.
 * Compare on port. Then on protocol.
 */
static int services_compare_port_proto (const void **_a, const void **_b)
{
  const struct service_entry *a = *_a;
  const struct service_entry *b = *_b;
  int   rc = ((int)a->port - (int)b->port);

  if (rc == 0)
     rc = compare_proto (a->proto, b->proto);
  return (rc);
}

/**
 * `smartlist_bsearch()` helper.
 * Compare on port. Then on protocol if it's given.
 */
static int services_bsearch_port_proto (const void *key, const void **member)
{
  const struct service_entry *se     = *member;
  const struct service_entry *lookup = key;
  int   rc = (int)lookup->port - (int)se->port;

  if (rc == 0 && (lookup->proto & PROTO_UNKNOWN) == 0)
     rc = compare_proto (lookup->proto, se->proto);

  TRACE (3, "key: %4u se->name: %-20s se->port: %5u, se->proto: %-20s, rc: %d\n",
         lookup->port, se->name, se->port, decode_proto_str(se->proto), rc);
  return (rc);
}

#define _STR2(x) #x
#define _STR(x)  _STR2(x)

/*
 * Parse a Wireshark style port/protocol string like `1/tcp/udp`
 * and encode the parts into a bitfield of `PROTO_x`. \see PROTO_UDP above.
 */
static int parse_port_proto (struct service_entry *se, const char *value)
{
  char the_rest [MAX_PROTOS_LEN] = { '\0' };
  UINT port;

  if (sscanf(value, "%u/%" _STR(MAX_PROTOS_LEN) "s", &port, the_rest) == 2)
  {
    se->port  = port;
    se->proto = encode_proto_str (the_rest, TRUE);
    if (se->proto > PROTO_UNKNOWN)
       se->proto &= ~PROTO_UNKNOWN;  /* clear this bit */
    return (1);
  }
  return (0);
}

/**
 * \li Handle field 0 which is the service name.
 * \li Handle field 1 which is the port / protocol field. Like `1/tcp/udp`.
 */
static int services_CSV_add (struct CSV_context *ctx, const char *value)
{
  static struct service_entry se;

  switch (ctx->field_num)
  {
    case 0:
         _strlcpy (se.name, value, sizeof(se.name));
         break;
    case 1:
         if (parse_port_proto(&se, value))
            add_entry (&se);
         memset (&se, '\0', sizeof(se));
         break;
  }
  return (1);
}

/**
 * Print some statistics and the details of the `services_list`.
 */
static void services_file_dump (int max, int duplicates)
{
  int i;

  trace_printf ("\nDuplicates: %d. A total of %d entries in these file(s):\n", duplicates, max);

  for (i = 0; g_cfg.services_file[i]; i++)
      trace_printf ("  %d: \"%s\"\n", i, g_cfg.services_file[i]);

  trace_puts ("\nService entries sorted on port:\n"
              "Idx - Service ------------- Port / proto ------------------------ Services-file\n");

  for (i = 0; i < max; i++)
  {
    const struct service_entry *se = smartlist_get (services_list, i);
    char  buf [100];
    int   file_idx;

    for (file_idx = 0; g_cfg.services_file [file_idx]; file_idx++)
    {
      if (se->file == g_cfg.services_file[file_idx])
         break;
    }
    snprintf (buf, sizeof(buf), "%5u / %-30s", se->port, decode_proto_str(se->proto));
    trace_printf ("%4d: %-20s %-20s %d\n", i, se->name, buf, file_idx);
  }
}

struct test_table {
       const char *service;
       uint16_t    port;
       const char *protocol;
       void       *expect;
     };

/**
 * Test `ws_getservbyport()` with these service entries:
 * ```
 *  bgp   179/tcp/udp/sctp         # Border Gateway Protocol
 *  exp2  1022/udp/tcp/dccp/sctp   # RFC3692-style Experiment 2
 * ```
 */
static const struct test_table tests[] = {
                 { "bgp",   179,  "tcp",  &ret_fill_servent },
                 { "bgp",   179,  "udp",  &ret_fill_servent },
                 { "bgp",   179,  "sctp", &ret_fill_servent },
                 { "bgp",   179,  "dccp", NULL              },
                 { "bgp",   179,  NULL,   &ret_fill_servent },   /* the ANY protocol case */
                 { "bgp",   179,  "geek", NULL              },   /* the unknown protocol case */
                 { "exp2", 1022,  "udp",  &ret_fill_servent },
                 { "exp2", 1022,  "tcp",  &ret_fill_servent },
                 { "exp2", 1022,  "dccp", &ret_fill_servent },
                 { "exp2", 1022,  "sctp", &ret_fill_servent },
                 { "exp2", 1022,  NULL,   &ret_fill_servent }
               };

static void services_run_tests (void)
{
  BOOL fallback;
  int  i;

  trace_puts ("\nRunning 'tests[]':\n");

  if (startup_count > 0)   /* Call Winsock's `getservbyport()` too */
       fallback = TRUE;
  else fallback = FALSE;

  for (i = 0; i < DIM(tests); i++)
  {
    const struct servent *se = ws_getservbyport (swap16(tests[i].port),
                                                 tests[i].protocol, fallback, TRUE);

    BOOL match = (se == tests[i].expect);

    trace_printf ("%2d: %-4s/%5s: %s\n", i,
                  tests[i].service,
                  tests[i].protocol ? tests[i].protocol : "NULL",
                  match ? "OKAY" : "FAIL");
    if (se)
         trace_printf ("    name: %-5s port: %4u, proto: %s\n", se->s_name, swap16(se->s_port), se->s_proto ? se->s_proto : "NULL");
    else trace_puts   ("    NULL\n");
  }
  trace_putc ('\n');
}

/**
 * Free the memory in `services_list` and free the list itself.
 */
void services_file_exit (void)
{
  if (services_list)
     smartlist_wipe (services_list, free);
  services_list = NULL;
}

/**
 * Build the `services_file` smartlist.
 *
 * We support loading multiple `services` files;
 * Currently max 3.
 */
void services_file_init (void)
{
  struct CSV_context ctx;
  int    duplicates;

  assert (services_list == NULL);
  services_list = smartlist_new();
  if (!services_list)
     return;

  for (current_services_file = 0; g_cfg.services_file[current_services_file];
       current_services_file++)
  {
    memset (&ctx, '\0', sizeof(ctx));
    ctx.file_name  = g_cfg.services_file [current_services_file];
    ctx.num_fields = 2;
    ctx.delimiter  = '\t';
    ctx.callback   = services_CSV_add;
    CSV_open_and_parse_file (&ctx);
  }

  smartlist_sort (services_list, services_compare_port_proto);
  duplicates = smartlist_make_uniq (services_list, services_compare_port_proto, free);

  if (g_cfg.trace_level >= 2)
  {
    int max = smartlist_len (services_list);

    services_file_dump (max, duplicates);
    services_run_tests();
  }
}

/**
 * Convert a `struct service_entry*` to a `struct servent*`.
 * Does not support aliases.
 */
static __inline const struct servent *fill_servent (const struct service_entry *se, int protocol)
{
  static char  name [MAX_SERV_LEN];
  static char  proto [MAX_PROTO_LEN];
  static char *null_aliases [1] = { NULL };
  const char  *proto_ret;

  if (protocol == PROTO_UNKNOWN)
       proto_ret = NULL;
  else proto_ret = _strlcpy (proto, list_lookup_name(protocol, protocol_list, DIM(protocol_list)), sizeof(proto));

  ret_fill_servent.s_name    = _strlcpy (name, se->name, sizeof(name));
  ret_fill_servent.s_proto   = (char*) proto_ret;
  ret_fill_servent.s_aliases = null_aliases;
  ret_fill_servent.s_port    = swap16 (se->port);
  return (&ret_fill_servent);
}

/**
 * The internal `getservbyport()` function that does a
 * binary search in the `services_list`.
 *
 * \param[in] port      the port on network that we'll search the name for.
 * \param[in] protocol  the optional protocol name to look for.
 * \param[in] fallback  call the Winsock function `getservbyport()` if this
 *                      function fails to find a match.
 */
const struct servent *ws_getservbyport (uint16_t port, const char *protocol, BOOL fallback, BOOL do_wstrace)
{
  const struct servent *ret = NULL;
  const struct service_entry *se = NULL;
  struct service_entry lookup;

  lookup.port  = swap16 (port);
  lookup.proto = protocol ? encode_proto_str(protocol, FALSE) : PROTO_UNKNOWN;

  /**
   * Give up if:
   *  \li We're asked to lookup a service-name for a protocol we do not support.
   *  \li Or `services_list == NULL` (malloc failed) or no services file.
   *
   *  but possibly ask Winsock about it.
   */
  if (protocol && lookup.proto == PROTO_UNKNOWN)
      TRACE (3, "Unknown protocol: '%s'.\n", protocol);
  else if (!services_list || g_cfg.num_services_files == 0)
       TRACE (3, "No services file(s).\n");
  else se = smartlist_bsearch (services_list, &lookup, services_bsearch_port_proto);

  if (se)
     ret = fill_servent (se, lookup.proto);

  /* if not found, do the fallback to `getservbyport()`?
   * But we cannot call it after a `WSACleanup()` has been done.
   */
  if (!se && fallback && !cleaned_up)
  {
    if (!do_wstrace)
       trace_level_save_restore (0);
    ret = getservbyport (port, protocol);
    if (!do_wstrace)
       trace_level_save_restore (1);
  }
  return (ret);
}