#ifndef _DISTREF_TYPE_HH_
#define _DISTREF_TYPE_HH_

#include <cstdint>

#define _DR_NAME_LEN 20
#define _MAX_DMAPS 64
typedef int16_t WorkerID;

enum WorkerType { PACKET_WORKER, BACKGROUND_WORKER };

enum ADTType {
  ADT_MAP,
  /* ADT_LIST,
	ADT_HASHTABLE */  // TODO
};

struct _DR_ADTInfo {
  const char *name;
  ADTType type;
  int obj_id;
  int obj_size;
};

typedef int (*init_func)(int param);
typedef int (*packet_func)(struct rte_mbuf *);
typedef void (*background_func)(void);

#endif /* _DISTREF_TYPE_HH_ */
