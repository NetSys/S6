/* Load and match signatures
 *
 * (c) Kacper Wysocki <kacperw@gmail.com> for PRADS, 2009
 *
 * straight port of p0f load,match_sigs and support functions - nothing new here
 *
 * p0f loads sigs as struct fp_entry into the sig[] array 
 * ... and uses a static hash lookup to jump into this array
 * - based on size, option count, quirks and don't fragment
 *
 * thoughts to improve:
  - decouple fingerprints from signatures,
  provide consistent interface for matching assets
  across services (arp,ip,tcp,udp,link,dhcp,...)

  *** The interface (should be) ***

    *** sigs ***
    load_sigs() <- create hashtable from file
    usage: 
    sigs* = load_sigs(file)
    load_sigs_{syn,ack,synack,..}()

    match_fp(char *fp, struct *fp) <- take a fingerprint string/struct
      and lookup into hash. return unique, fuzzy, best match.

    match_fp(packetinfo) - guess OS based on packet info

 TODO:
  - ipv6 fix
  - collide
  - frob ipfp* stuff for sanity
  - walk through find_match() and return the match properly
  - run update_asset_os() with a looked-up asset
  - sanity check asset lookups

    update_asset_os(pi, de, fp, tstamp?tstamp:0);

  - prepare_tcp()
  - parse_tcp()


 */

#include "dist.hh"
#include "stub.asset.hh"
#include "common.h"
#include "prads.h"
//#include "dhcp.h"
#include "sys_func.h"
#include "mtu.h"
//#include "tos.h"
#include "config.h"
//#include "assets.h"

extern globalconfig config;

#define MAXLINE 1024
#define SIG_HASHSIZE 1024
#define MAXDIST 512
#define PKT_DLEN 16
#define PKT_MAXPAY 145

// in open mode, how many options to parse
#define TCPOPT_LIMIT 3

/* SIGHASH needs some tweaking
 * the addition of wsize has reduced collisions
 * but similar signatures still collide.
 *
 * best case (and least efficient) would be to hash on
 * full options and quirks
 */
#define SIGHASH(tsize,optcnt,q,df) \
	( ((tsize) << 2) ^ ((optcnt) << 1) ^ (df) ^ (q) )
	//( ((wsize) << 3) ^ ((tsize) << 2) ^ ((optcnt) << 1) ^ (df) ^ (q) )

uint32_t packet_count;
uint8_t operating_mode;
uint32_t st_time;
static uint8_t no_extra,
    no_osdesc,
    no_known,
    no_unknown,
    rst_mode,
    mode_oneline,
    always_sig,
    do_resolve,
    check_collide,
    full_dump, use_fuzzy, payload_dump;

bstring gen_fp_tcpopt(uint32_t ocnt, uint8_t op[], uint16_t mss, uint16_t wss, uint16_t wsc, uint32_t tstamp)
{
    uint32_t j;
    bstring fp = bformat("");
    for (j = 0; j < ocnt; j++) {
        switch (op[j]) {
        case TCPOPT_NOP:
            bformata(fp, "N");
            break;
        case TCPOPT_WSCALE:
            bformata(fp, "W%d", wsc);
            break;
        case TCPOPT_MAXSEG:
            bformata(fp, "M%d", mss);
            break;
        case TCPOPT_TIMESTAMP:
            bformata(fp, "T");
            if (!tstamp)
                bformata(fp, "0");
            break;
        case TCPOPT_SACKOK:
            bformata(fp, "S");
            break;
        case TCPOPT_EOL:
            bformata(fp, "E");
            break;
        default:
            bformata(fp, "?%d", op[j]);
            break;
        }
        if(!config.tcpopt_parsable) // experimental format change!
          if (j != ocnt - 1)
            bformata(fp, ",");
    }

    if (blength(fp) < 2)
        bformata(fp, ".");

    return fp;
}

bstring gen_fp_tcpquirks(uint32_t quirks)
{
    bstring fp = bformat("");
    if (!quirks)
        bformata(fp, ".");
    else {
        if (quirks & QUIRK_RSTACK)
            bformata(fp, "K");
        if (quirks & QUIRK_SEQEQ)
            bformata(fp, "Q");
        if (quirks & QUIRK_SEQ0)
            bformata(fp, "0");
        if (quirks & QUIRK_PAST)
            bformata(fp, "P");
        if (quirks & QUIRK_ZEROID)
            bformata(fp, "Z");
        if (quirks & QUIRK_IPOPT)
            bformata(fp, "I");
        if (quirks & QUIRK_URG)
            bformata(fp, "U");
        if (quirks & QUIRK_X2)
            bformata(fp, "X");
        if (quirks & QUIRK_ACK)
            bformata(fp, "A");
        if (quirks & QUIRK_T2)
            bformata(fp, "T");
        if (quirks & QUIRK_FLAGS)
            bformata(fp, "F");
        if (quirks & QUIRK_DATA)
            bformata(fp, "D");

        // edward 
        if (quirks & QUIRK_FINACK)
            bformata(fp, "N");
        if (quirks & QUIRK_FLOWL)
            bformata(fp, "L");

        if (quirks & QUIRK_BROKEN)
            bformata(fp, "!");
    }
    return fp;
}

/* generate a bstring fingerprint based on packet
 * allocates memory */
bstring gen_fp_tcp(fp_entry *e, uint32_t tstamp, uint8_t tf)
    /*
                uint8_t ttl,
                uint16_t tot,
                uint8_t df,
                uint8_t * op,
                uint8_t ocnt,
                uint16_t mss,
                uint16_t wss,
                uint8_t wsc,
                uint32_t tstamp,
                uint32_t quirks,
                uint8_t ftype,
                packetinfo *pi)
     */
{

    uint16_t mss, wss;//, tot;
    uint8_t ttl;
    bstring fp, fpopt, fpquirks;
    //uint8_t q = 0;

    mss = e->mss;
    wss = e->wsize;
    //tot = e->size;
    ttl = e->ttl; //normalize_ttl(e->ttl);
    fp = bformat("");

    // mss/wss code might make the fpstring look different from the file sig
    if (mss && wss && !(wss % mss))
        bformata(fp, "S%d", (wss / mss));
    else if (wss && !(wss % 1460))
        bformata(fp, "S%d", (wss / 1460));
    else if (mss && wss && !(wss % (mss + 40)))
        bformata(fp, "T%d", (wss / (mss + 40)));
    else if (wss && !(wss % 1500))
        bformata(fp, "T%d", (wss / 1500));
    else if (wss == 12345)
        bformata(fp, "*(12345)");
    else {
        bformata(fp, "%d", wss);
    }

    if ( tf == TF_ACK || tf == TF_RST ) {
        bformata(fp, ":%d:%d:*:",ttl, e->df);
    } else {
        if (e->size < PACKET_BIG)
            bformata(fp, ":%d:%d:%d:", ttl, e->df, e->size);
        else
            bformata(fp, ":%d:%d:*(%d):", ttl, e->df, e->size);
    }

    // TCP Options
    fpopt = gen_fp_tcpopt(( tf == TF_ACK? TCPOPT_LIMIT : e->optcnt), e->opt, mss, wss, e->wsc, tstamp);
    bconcat(fp, fpopt);
    bdestroy(fpopt);

    bformata(fp, ":");

    // Quirks
    fpquirks = gen_fp_tcpquirks(e->quirks);
    bconcat(fp, fpquirks);
    bdestroy(fpquirks);

    //if (tstamp) printf("(* uptime: %d hrs)\n",tstamp/360000);
    //update_asset_os(pi, tf, fp, tstamp?tstamp:0);
    return fp;
}

void print_sig(fp_entry * e)
{
    // gen_fp_tcp takes (fingerprint, uptime, TCP_FLAG)
    // meaning that e->zero_stamp is wrong!
    bstring b = gen_fp_tcp(e, e->zero_stamp, 0);
    char *c = bstr2cstr(b, '-');
    printf("[%s", c);
    bcstrfree(c);
    bdestroy(b);

    printf("],%s:%s\n", e->os, e->desc);
}
void print_sigs(fp_entry * e)
{
    print_sig(e);
    if (e->next)
        print_sigs(e->next);
}

/* recursively free signatures */
static void free_sigs(fp_entry *e){
    if(e->next)
        free_sigs(e->next);
    free(e->os);
    free(e->desc);
    free(e);
}

/* alloc_sig return a newly allocated copy of *e */
static fp_entry *alloc_sig(fp_entry *e)
{
    fp_entry *n = (fp_entry *) calloc(1, sizeof(fp_entry));
    *n = *e; // copy
    return n;
}

/* parse the wss field of the signature line */
static int parse_sig_wsize(fp_entry *sig, char* w)
{
    if (w[0] == '*') {
        sig->wsize = 1;
        sig->wsize_mod = MOD_CONST;
    } else if (tolower(w[0]) == 's') {
        sig->wsize_mod = MOD_MSS;
        if (!isdigit(*(w + 1)))
            fatal("Bad Snn value in WSS in line %d.\n", sig->line);
        sig->wsize = atoi(w + 1);
    } else if (tolower(w[0]) == 't') {
        sig->wsize_mod = MOD_MTU;
        if (!isdigit(*(w + 1)))
            fatal("Bad Tnn value in WSS in line %d.\n", sig->line);
        sig->wsize = atoi(w + 1);
    } else if (w[0] == '%') {
        if (!(sig->wsize = atoi(w + 1)))
            fatal("Null modulo for window size in config line %d.\n",
                  sig->line);
        sig->wsize_mod = MOD_CONST;
    } else
        sig->wsize = atoi(w);

    return 0;
}

/* parse the option field of the signature line */
static int parse_sig_options(fp_entry *sig, char* p)
{
    sig->zero_stamp = 1;

    if (*p == '.')
        p++;

    while (*p) {
        uint8_t optcnt = sig->optcnt;
        switch (tolower(*p)) {

            case 'n':
                sig->opt[optcnt] = TCPOPT_NOP;
                break;

            case 'e':
                sig->opt[optcnt] = TCPOPT_EOL;
                //if (*(p + 1))  // Old! Improved fingerprints with also collecting options after EOL
                //    fatal("EOL not the last option (line %d).\n", sig->line);
                break;

            case 's':
                sig->opt[optcnt] = TCPOPT_SACKOK;
                break;

            case 't':
                sig->opt[optcnt] = TCPOPT_TIMESTAMP;
                if (*(p + 1) != '0') {
                    sig->zero_stamp = 0;
                    if (isdigit(*(p + 1)))
                        fatal("Bogus Tstamp specification in line %d.\n",
                              sig->line);
                }
                break;

            case 'w':
                sig->opt[optcnt] = TCPOPT_WSCALE;
                if (p[1] == '*') {
                    sig->wsc = 1;
                    sig->wsc_mod = MOD_CONST;
                } else if (p[1] == '%') {
                    if (!(sig->wsc = atoi(p + 2)))
                        fatal
                            ("Null modulo for wscale in config line %d.\n",
                             sig->line);
                    sig->wsc_mod = MOD_CONST;
                } else if (!isdigit(*(p + 1)))
                    fatal("Incorrect W value in line %d.\n", sig->line);
                else
                    sig->wsc = atoi(p + 1);
                break;

            case 'm':
                sig->opt[optcnt] = TCPOPT_MAXSEG;
                if (p[1] == '*') {
                    sig->mss = 1;
                    sig->mss_mod = MOD_CONST;
                } else if (p[1] == '%') {
                    if (!(sig->mss = atoi(p + 2)))
                        fatal("Null modulo for MSS in config line %d.\n",
                              sig->line);
                    sig->mss_mod = MOD_CONST;
                } else if (!isdigit(*(p + 1)))
                    fatal("Incorrect M value in line %d.\n", sig->line);
                else
                    sig->mss = atoi(p + 1);
                break;

                /*
                 * Yuck! 
                 */
            case '?':
                if (!isdigit(*(p + 1)))
                    fatal("Bogus ?nn value in line %d.\n", sig->line);
                else
                    sig->opt[optcnt] = atoi(p + 1);
                break;

            default:
                fatal("Unknown TCP option '%c' in config line %d.\n", *p,
                      sig->line);
        }

        if (++sig->optcnt >= MAXOPT)
            fatal
                ("Too many TCP options specified in config line %d.\n",
                 sig->line);

        /*
         * Skip separators 
         */
        do {
            p++;
        } while (*p && !isalpha(*p) && *p != '?');
    }
    return 0;
}

/* parse the quirks field of the signature line */
static int parse_sig_quirks(fp_entry *sig, uint8_t *p)
{
    while (*p){
        switch (toupper(*(p++))) {
            case 'E':
                fatal
                    ("Quirk 'E' (line %d) is obsolete. Remove it, append E to the "
                     "options.\n", sig->line);

            case 'K':
                //if (!rst_mode)
                if (!IS_COSET(&config,CO_RST))
                    fatal("Quirk 'K' (line %d) is valid only in RST+ (-R)"
                          " mode (wrong config file?).\n", sig->line);
                sig->quirks |= QUIRK_RSTACK;
                break;

            case 'D':
                sig->quirks |= QUIRK_DATA;
                break;

            case 'Q':
                sig->quirks |= QUIRK_SEQEQ;
                break;
            case '0':
                sig->quirks |= QUIRK_SEQ0;
                break;
            case 'P':
                sig->quirks |= QUIRK_PAST;
                break;
            case 'Z':
                sig->quirks |= QUIRK_ZEROID;
                break;
            case 'I':
                sig->quirks |= QUIRK_IPOPT;
                break;
            case 'U':
                sig->quirks |= QUIRK_URG;
                break;
            case 'X':
                sig->quirks |= QUIRK_X2;
                break;
            case 'A':
                sig->quirks |= QUIRK_ACK;
                break;
            case 'T':
                sig->quirks |= QUIRK_T2;
                break;
            case 'F':
                sig->quirks |= QUIRK_FLAGS;
                break;
            case 'N':
                sig->quirks |= QUIRK_FINACK;
                break;
            case 'L':
                sig->quirks |= QUIRK_FLOWL;
                break;
            case '!':
                sig->quirks |= QUIRK_BROKEN;
                break;
            case '.':
                break;
            default:
                fatal("Bad quirk '%c' in line %d.\n", *(p - 1), sig->line);
        }
    }
    return 0;
}



/* load_sigs: fill **sig with fp_entry signatures from *file
 *
 * sigp is a pointer to either 
 ** a pointer to a preallocated buffer of size max_sigs * fp_entry OR
 ** a NULL pointer indicating that we should allocate max_sigs for you
 * max_sigs is the maximal size of the buffer, or 0 in which case we decide
 *
 * Theory:   snarf sigs in serially, easypeasy
 * Practice: lookups are a bitch and require a buckethash.
 ** -> store sigs directly into hash.
 * 
 * returns errno
 */
int load_sigs(const char *file, fp_entry **sigp[], int hashsize)
{
    fp_entry **sig; // output
    uint32_t ln = 0;
    //debug("opening %s\n", file);
    FILE *f = fopen(file, "r");
    char buf[MAXLINE];
    char *p;
    if (!f) {
        perror("failed to open file");
        return errno;
    }
    if(!sigp){
        fclose(f);
        perror("need a pointer to fill");
        return -1;
    }
    if(!hashsize)
        hashsize = SIG_HASHSIZE;
    if(*sigp == NULL){
        *sigp = (fp_entry **) calloc(hashsize, sizeof(fp_entry*));
    }
    sig = *sigp;

	uint16_t idx = 0;
    while ((p = fgets(buf, sizeof(buf), f))) {
        uint32_t l;

        char obuf[MAXLINE], genre[MAXLINE], desc[MAXLINE];
        uint8_t quirks[MAXLINE];
        char w[MAXLINE], sb[MAXLINE];
        char *gptr = genre;
        uint32_t t, d, s;
        fp_entry asig = {0}; //guarantee it's empty this sig
        fp_entry *e;

        ln++;

        /* Remove leading and trailing blanks */
        while (isspace(*p))
            p++;
        l = strlen(p);
        while (l && isspace(*(p + l - 1)))
            *(p + (l--) - 1) = 0;

        /* Skip empty lines and comments */
        if (!l)
            continue;
        if (*p == '#')
            continue;

        if (sscanf
            (p, "%[0-9%*()ST]:%d:%d:%[0-9()*]:%[^:]:%[^ :]:%[^:]:%[^:]", 
            w, &t, &d, sb, obuf, quirks, genre, desc) != 8)
            fatal("Syntax error in config line %d.\n", ln);

        gptr = genre;

        if (*sb != '*') {
            s = atoi(sb);
        } else
            s = 0;

      reparse_ptr:

        switch (*gptr) {
        case '-':
            asig.userland = 1;
            gptr++;
            goto reparse_ptr;
        case '*':
            asig.no_detail = 1;
            gptr++;
            goto reparse_ptr;
        case '@':
            asig.generic = 1;
            gptr++;
            //gencnt++;
            goto reparse_ptr;
        case 0:
            fatal("Empty OS genre in line %d.\n", ln);
        }

		asig.idx = idx++;
        asig.os = strdup(gptr);
        asig.desc = strdup(desc);
        asig.ttl = t;
        asig.size = s;
        asig.df = d;

        parse_sig_wsize(&asig, w);
        asig.line = ln;
        parse_sig_options(&asig, obuf);
        parse_sig_quirks(&asig, quirks);
        uint32_t index = SIGHASH(s, asig.optcnt, asig.quirks, d) % hashsize;
        e = sig[index];
		
        if (!e) {
            sig[index] = alloc_sig(&asig);
        } else {
            int cc = 0;
            // collision!
            while (e->next){
                e = e->next;
                cc++;
            }
            /*
            fprintf(stderr, "hash collision %d: \n%d: %s - %s\n%d: %s - %s\n",
            cc, asig.line, asig.os, asig.desc, e->line, e->os, e->desc);
            */
            e->next = alloc_sig(&asig);
        }

        /*
        if (check_collide)
            collide(sigcnt);
            */

        /* 
        if (++sigcnt >= hashsize)
            fatal("Maximum signature count exceeded.\n");
            */

    }

    fclose(f);
#ifdef DUMP_SIG_HASH
    {
        int i;
        for (i = 0; i < sigcnt; i++) {
            print_sig(&sig[i]);
        }
    }
#endif
#ifdef DEBUG_HASH
    {
        int i;
        fp_entry *p;
        printf("Hash table layout: ");
        for (i = 0; i < hashsize; i++) {
            int z = 0;
            p = sig[i];
            while (p) {
                p = p->next;
                z++;
            }
            printf("%d ", z);
        }
        putchar('\n');
    }
#endif                          /* DEBUG_HASH */

    if (check_collide)
        debug("[+] Signature collision check successful.\n");

    /*
    if (!sigcnt)
        debug("[!] WARNING: no signatures loaded from config file.\n");
        */

    return 0;
}

/* run through the hash, free entries, then free hash */
void unload_sigs(fp_entry **sigp, int size)
{
    int i = size;
    fp_entry *e;
    while(i--){
        e = sigp[i];
        if (e)
            free_sigs(e);
        sigp[i] = NULL; // clear
    }
    free(*sigp);
    *sigp = NULL;
}


#if 0
/* a dns cache of one? */
#define MY_MAXDNS 32

#include <netdb.h>
static inline char* grab_name(uint8_t* a) {
  struct hostent* r;
  static char rbuf[MY_MAXDNS+6] = "/";
  uint32_t j;
  char *s,*d = rbuf+1;

  if (!do_resolve) return "";
  r = gethostbyaddr(a,4,AF_INET);
  if (!r || !(s = r->h_name) || !(j = strlen(s))) return "";
  if (j > MY_MAXDNS) return "";

  while (j--) {
    if (isalnum(*s) || *s == '-' || *s == '.') *d = *s;
      else *d = '?';
    d++; s++;
  }

  *d=0;

  return rbuf;

}
#endif

#if 0
uint8_t* lookup_link(uint16_t mss, uint8_t txt) {
  uint32_t i;
  static char tmp[32];

  if (!mss) return txt ? (uint8_t *)"unspecified" : 0;
  mss += 40;
  
  for (i=0;i<MTU_CNT;i++) {
   if (mss == mtu[i].mtu) return (uint8_t*) mtu[i].dev;
   if (mss < mtu[i].mtu)  goto unknown;
  }

unknown:

  if (!txt) return 0;
  sprintf(tmp,"unknown-%d",mss);
  return (uint8_t*)tmp;

}
#endif

#if 0
static char* lookup_tos(uint8_t t) {
  uint32_t i;

  if (!t) return 0;

  for (i=0;i<TOS_CNT;i++) {
   if (t == tos[i].tos) return tos[i].desc;
   if (t < tos[i].tos) break;
  }

  return 0;

}
#endif

#if 0
void dump_packet(const uint8_t* pkt,uint16_t plen) {
  uint32_t i;
  uint8_t  tbuf[PKT_DLEN+1];
  uint8_t* t = tbuf;
 
  for (i=0;i<plen;i++) {
    uint8_t c = *(pkt++);
    if (!(i % PKT_DLEN)) dlog("  [%02x] ",i);
    dlog("%02x ",c);
    *(t++) = isprint(c) ? c : '.';
    if (!((i+1) % PKT_DLEN)) {
      *t=0;
      dlog(" | %s\n",(t=tbuf));
    }
  }
  
  if (plen % PKT_DLEN) {
    *t=0;
    while (plen++ % PKT_DLEN) dlog("   ");
    dlog(" | %s\n",tbuf);
  }

}
#endif

#if 0
void dump_payload(const uint8_t* data,uint16_t dlen) {
  uint8_t  tbuf[PKT_MAXPAY+2];
  uint8_t* t = tbuf;
  uint8_t  i;
  uint8_t  max = dlen > PKT_MAXPAY ? PKT_MAXPAY : dlen;

  if (!dlen) return;

  for (i=0;i<max;i++) {
    if (isprint(*data)) *(t++) = *data; 
      else if (!*data)  *(t++) = '?';
      else *(t++) = '.';
    data++;
  }

  *t = 0;

  plog( "  # Payload: \"%s\"%s",tbuf,dlen > PKT_MAXPAY ? "...\n" : "\n");
}
#endif


/* parse TCP packet quirks */
static inline void parse_quirks(uint8_t ftype, tcp_header *tcph, uint32_t *quirks)
{
    if (ftype == TF_RST && (tcph->t_flags & TF_ACK))
        *quirks |= QUIRK_RSTACK;
    if (ftype == TF_FIN && (tcph->t_flags & TF_ACK))
        *quirks |= QUIRK_FINACK;

    if (tcph->t_seq == tcph->t_ack)
        *quirks |= QUIRK_SEQEQ;
    if (!tcph->t_seq)
        *quirks |= QUIRK_SEQ0;
        // ftype makes little sense here
    if (tcph->t_flags & ~(TF_SYN | TF_ACK | TF_RST | TF_ECE | TF_CWR
                          | ((ftype == TF_ACK)? TF_PUSH : 0)))
        *quirks |= QUIRK_FLAGS;
    if (tcph->t_ack)
        *quirks |= QUIRK_ACK;
    if (tcph->t_urgp)
        *quirks |= QUIRK_URG;
    if (TCP_X2(tcph))
        *quirks |= QUIRK_X2;
}
/* parse TCP option header field
 * yes, this function returns the timestamp for now */ 
static inline uint32_t parse_tcpopt(const uint8_t *opt_ptr, int32_t ilen, const uint8_t *end_ptr, fp_entry *e)
{
    uint8_t ocnt = 0;
    int32_t olen;
    // mnemonics
    uint32_t *quirks = &e->quirks;
    uint8_t *op = e->opt;
    // timestamp is 64bit, but see if I care
    uint32_t tstamp = 0;

    while (ilen > 0) {
        ilen--;

        // * let the phun begin... 
        switch (*(opt_ptr++)) {
        case TCPOPT_EOL:
            // * EOL 
            op[ocnt] = TCPOPT_EOL;

            if (ilen) {
                *quirks |= QUIRK_PAST;
            }
            break;

        case TCPOPT_NOP:
            // * NOP 
            op[ocnt] = TCPOPT_NOP;
            break;

        case TCPOPT_SACKOK:
            // * SACKOK LEN 
            op[ocnt] = TCPOPT_SACKOK;
            ilen--;
            opt_ptr++;
            break;

        case TCPOPT_MAXSEG:
            // * MSS LEN D0 D1 
            if (opt_ptr + 3 > end_ptr) {
borken:
                *quirks |= QUIRK_BROKEN;
                goto end_parsing;
            }
            op[ocnt] = TCPOPT_MAXSEG;
            e->mss = GET16(opt_ptr + 1);
            ilen -= 3;
            opt_ptr += 3;
            break;

        case TCPOPT_WSCALE:
            // * WSCALE LEN D0 
            if (opt_ptr + 2 > end_ptr)
                goto borken;
            op[ocnt] = TCPOPT_WSCALE;
            e->wsc = *(uint8_t *) (opt_ptr + 1);
            ilen -= 2;
            opt_ptr += 2;
            break;

        case TCPOPT_TIMESTAMP:
            // * TSTAMP LEN T0 T1 T2 T3 A0 A1 A2 A3 
            // ugly handling of a beautiful 64bit field
            if (opt_ptr + 9 > end_ptr)
                goto borken;
            op[ocnt] = TCPOPT_TIMESTAMP;

            memcpy(&tstamp, opt_ptr + 5, 4);
            if (tstamp)
                *quirks |= QUIRK_T2;

            memcpy(&tstamp, opt_ptr + 1, 4);
            tstamp = ntohl(tstamp);

            ilen -= 9;
            opt_ptr += 9;
            break;
        case TCPOPT_PROXBLUECOAT:
        case TCPOPT_PROXCISCO:
        case TCPOPT_PROXRIVERBED1:
        case TCPOPT_PROXRIVERBED2:
            dlog("magic middleware option %02x detected", *(opt_ptr - 1) );
            // fallthru for now..
        default:
            // * Hrmpf... 
            if (opt_ptr + 1 > end_ptr)
                goto borken;

            op[ocnt] = *(opt_ptr - 1);
            olen = *(uint8_t *) (opt_ptr) - 1;
            if (olen > 32 || (olen < 0))
                goto borken;

            ilen -= olen;
            opt_ptr += olen;
            break;

        }
        ocnt++;
        if (ocnt >= MAXOPT - 1)
            goto borken;

        // * Whoops, we're past end_ptr 
        if (ilen > 0)
            if (opt_ptr >= end_ptr)
                goto borken;
    }

end_parsing:
    e->optcnt = ocnt;
    return tstamp;
}

/* find_match(): lookup packet with fingerprint e and info pi in sighash sig[]
 *
 * match is returned as e->os and e->desc in e
 * NB NOTE XXX: the os and desc fields are statically allocated, do not free()!
 */
fp_entry *find_match(
    fp_entry *sig[], uint32_t hashsize,
    fp_entry *e, packetinfo *pi,
    uint32_t tstamp,
    uint8_t plen,
    uint8_t *pay
    )
{

  uint32_t j;
  fp_entry* p;
  uint8_t  orig_df  = e->df;
//  char* tos_desc = 0;

  fp_entry* fuzzy = 0;
  char outbuf[INET6_ADDRSTRLEN+1];

re_lookup:
  p = sig[SIGHASH(e->size,e->optcnt,e->quirks,e->df) % hashsize];
  
  while (p) {
  
    /* Cheap and specific checks first... */
    // esize == 0 => open_mode
    if(e->size){
      /* psize set to zero means >= PACKET_BIG */
      if (p->size) { if (e->size ^ p->size) { p = p->next; continue; } }
      else if (e->size < PACKET_BIG) { p = p->next; continue; }
    }

    if (e->optcnt ^ p->optcnt) { p = p->next; continue; }

    if (p->zero_stamp ^ (!tstamp)) { p = p->next; continue; }
    if (p->df ^ e->df) { p = p->next; continue; }
    if (p->quirks ^ e->quirks) { p = p->next; continue; }

    /* Check e->mss and WSCALE... */
    if (!p->mss_mod) {
      if (e->mss ^ p->mss) { p = p->next; continue; }
    } else if (e->mss % p->mss) { p = p->next; continue; }

    if (!p->wsc_mod) {
      if (e->wsc ^ p->wsc) { p = p->next; continue; }
    } else if (e->wsc % p->wsc) { p = p->next; continue; }

    /* Then proceed with the most complex e->wsize check... */
    switch (p->wsize_mod) {
      case 0:
        if (e->wsize ^ p->wsize) { p = p->next; continue; }
        break;
      case MOD_CONST:
        if (e->wsize % p->wsize) { p = p->next; continue; }
        break;
      case MOD_MSS:
        if (e->mss && !(e->wsize % e->mss)) {
          if ((e->wsize / e->mss) ^ p->wsize) { p = p->next; continue; }
        } else if (!(e->wsize % 1460)) {
          if ((e->wsize / 1460) ^ p->wsize) { p = p->next; continue; }
        } else { p = p->next; continue; }
        break;
      case MOD_MTU:
        if (e->mss && !(e->wsize % (e->mss+40))) {
          if ((e->wsize / (e->mss+40)) ^ p->wsize) { p = p->next; continue; }
        } else if (!(e->wsize % 1500)) {
          if ((e->wsize / 1500) ^ p->wsize) { p = p->next; continue; }
        } else { p = p->next; continue; }
        break;
     }

    /* Numbers agree. Let's check options */

    for (j=0;j<e->optcnt;j++){
      if (p->opt[j] ^ e->opt[j]) goto continue_search;
    }

    /* Check TTLs last because we might want to go fuzzy. */
    if (p->ttl < e->ttl) {
      if (use_fuzzy) fuzzy = p;
      p = p->next;
      continue;
    }

    /* Naah... can't happen ;-) */
    if (!p->no_detail){
      if (p->ttl - e->ttl > MAXDIST) { 
        if (use_fuzzy) fuzzy = p;
        p = p->next; 
        continue; 
      }
    }

continue_fuzzy:    
    
    /* Match! */
    
    if (!no_known) {
      // copy in the os/desc pointers. These are not to be free()d!
      e->os = p->os;
      e->desc = p->desc;
    }

    return e;

continue_search:

    p = p->next;

  }
	/* From here FOR S6 TEST !!! Randomly found signature */
  long int random_idx = random();
  uint16_t idx = random_idx % hashsize;
  p = sig[idx];
  if (p) {
	  e->idx = p->idx;
	  e->os = p->os;
	  e->desc = p->desc;
	  return e;
  }
	/* Up to here FOR S6 TEST !!! */
  

  if (!e->df) { e->df = 1; goto re_lookup; }

  if (use_fuzzy && fuzzy) {
    e->df = orig_df;
    p = fuzzy;
    fuzzy = 0;
    goto continue_fuzzy;
  }

  return e;
}

fp_entry *fp_tcp(packetinfo *pi, uint8_t ftype)
{
    uint8_t *opt_ptr;
    const uint8_t * end_ptr;
    uint8_t *payload = 0;
    fp_entry e = { 0 };
    int32_t ilen;
    uint32_t tstamp = 0;

    /* * If the declared length is shorter than the snapshot (etherleak
     * or such), truncate the package.
     * These tests are IP-specific and should one day go into into IP preproc*/
    end_ptr = pi->end_ptr;
    switch(pi->af){
        case AF_INET:
            opt_ptr = (uint8_t *) pi->ip4 + ntohs(pi->ip4->ip_len);
            if (end_ptr > opt_ptr)
                end_ptr = opt_ptr;
            if ((uint8_t *) (pi->ip4 + 1) > end_ptr)
                return NULL;
            ilen = pi->ip4->ip_vhl & 15;

            /* * B0rked packet */
            if (ilen < 5)
                return NULL;

            if (ilen > 5) {
                e.quirks |= QUIRK_IPOPT;
            }
            e.ttl = pi->ip4->ip_ttl;
            e.size = (ftype == TF_ACK) ? 0 : ntohs(pi->ip4->ip_len);
            e.df = (ntohs(pi->ip4->ip_off) & IP_DF) != 0;
            if (!pi->ip4->ip_id)
                e.quirks |= QUIRK_ZEROID;
            break;
            // default: there is no default
        default:
            fprintf(stderr, "tcp_fp: something very unsafe happened!\n");
            return NULL;
    }
    //printf("\nend_ptr:%u  opt_ptr:%u",end_ptr,opt_ptr);

    parse_quirks(ftype,pi->tcph,&e.quirks);
    ilen = (TCP_OFFSET(pi->tcph) << 2) - TCP_HEADER_LEN;

    opt_ptr = (uint8_t *) (pi->tcph + 1);
    if ((uint8_t *) opt_ptr + ilen < end_ptr) {
        if (ftype != TF_ACK)
            e.quirks |= QUIRK_DATA;
        payload = opt_ptr + ilen;
    }
    tstamp = parse_tcpopt(opt_ptr, ilen, pi->end_ptr, &e);

    e.wsize = ntohs(pi->tcph->t_win);

    fp_entry **sig = NULL;
    if (ftype == CO_SYN) {
        sig=config.sig_syn;
    } else if (ftype == CO_SYNACK) {
        sig=config.sig_synack;
    } else if (ftype == CO_RST) {
        sig=config.sig_rst;
    } else if (ftype == CO_FIN) {
        sig=config.sig_fin;
    } else if (ftype == CO_ACK) {
        sig=config.sig_ack;
    }


    if (sig != NULL) {
        find_match(sig,
                   config.sig_hashsize,
                   &e,
                   pi, // pass all packet characteristics
                   tstamp,
                   end_ptr - (uint8_t *) pi->ip4,
                   payload
                  );
    }

	if (e.os != NULL) {
		if (pi->asset != nullptr) {
			//printf("matched!! %s %s\n", e.os, e.desc);
			pi->asset->update_asset_os(ftype, e.idx, pi->pheader->ts.tv_sec);
		} else /* should not reach here */
			fprintf(stderr, "No asset!!");
		//update_asset_os(pi, ftype, NULL, &e, tstamp);
	}
    return NULL; // can't return stack-allocated * fp_entry e; 
}

void dump_sigs(fp_entry *mysig[], int max)
{
    int i;
    for (i = 0; i < max; i++){
        if (!mysig[i] || !mysig[i]->os)
            continue;
        print_sigs(mysig[i]);
    }
}
