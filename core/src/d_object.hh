#ifndef _DISTREF_DOBJECT_HH_
#define _DISTREF_DOBJECT_HH_

#include <cstdint>

#define _behind __attribute__((annotate("operation_behind")))
#define _stale __attribute__((annotate("operation_stale")))

// XXX Need to be updated with src_analyzer/codegen.py "ATTR_TO_DEF_MAP"
#define _FLAG_STALE (1 << 0)
#define _FLAG_BEHIND (1 << 1)

enum DObjType : int8_t { DOBJECT_UNKNOWN = 0, DOBJECT_SW, DOBJECT_MW };

struct _DR_ClassInfo {
  const char *name;
  DObjType type;
  int size;
};

class DObject {
 public:
  DObject(){};
  virtual ~DObject(){};

  static DObjType GetObjectType() { return DOBJECT_UNKNOWN; }
};

#endif
