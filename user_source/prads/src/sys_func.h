#ifndef SYSFUNC_H
#define SYSFUNC_H

#define plog(fmt, ...) do{ fprintf(stdout, (fmt), ##__VA_ARGS__); }while(0)

#define olog(fmt, ...) \
do{ \
    if(!(ISSET_CONFIG_QUIET(config))) { \
        if(ISSET_CONFIG_SYSLOG(config)) { \
            syslog(LOG_INFO, (fmt), ##__VA_ARGS__); \
        } else { \
            fprintf(stdout, (fmt), ##__VA_ARGS__); \
        } \
    } \
}while(0)

#ifdef DEBUG
#define DEBUG_ON 1
#define dlog(fmt, ...) do { fprintf(stderr, ("[%s:%d(%s)] " fmt), __FILE__, __LINE__, __PRETTY_FUNCTION__, ##__VA_ARGS__);} while(0)
#define vlog(v, fmt, ...) do{ if(DEBUG == v) fprintf(stderr, ("[%s:%d(%s)] " fmt), __FILE__, __LINE__, __PRETTY_FUNCTION__, ##__VA_ARGS__); }while(0)

#define elog(fmt, ...) fprintf(stderr, ("[%s:%d(%s)] " fmt), __FILE__, __LINE__, __PRETTY_FUNCTION__, ##__VA_ARGS__)

#else
#define DEBUG_ON 0

#define elog(fmt, ...) \
    do { \
        syslog(LOG_ERR, (fmt), ##__VA_ARGS__); \
        fprintf(stderr, (fmt), ##__VA_ARGS__); \
} while(0)

#define dlog(fmt, ...) do { ; } while(0)
#define vlog(fmt, ...) do { ; } while(0)
#endif
#define debug(x...)	fprintf(stderr,x)
#define fatal(x...)	do { debug("[-] ERROR: " x); exit(1); } while (0)

#define SETFLAG(key,flags) do{ key |= (flags); }while(0)
#define RESETFLAG(key,flags) do { key &= ~(flags); }while(0)

#endif
