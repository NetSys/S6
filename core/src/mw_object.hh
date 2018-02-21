#ifndef _DISTREF_MWOBJECT_HH_
#define _DISTREF_MWOBJECT_HH_

#include <string>

#include "d_object.hh"

/* Multi-Writer Objects which does RPC
  Current limitation:
  1. No function overloading
  2. Available parameter for function:
    2-1. primitive values (no pointers or references)
    2-2. structures (no class-type instances)
      - fields: primitives (OK), typedef (OK), enum (NO), pointer (NO)
    2-3. MwRef<XXX> (no pointers or references)
  3. Available return type for function:
    3-2. primitive values ONLY
*/

struct param {
  void *param;
  int size;
};

enum _DR_FunctionAttribute { _FCT_NONE, _FCT_ONEWAY, _FCT_STALE };

/* Multi-Writer Object <- Stationary */
class MWObject : public DObject {
 public:
  MWObject() {}
  static DObjType GetObjectType() { return DOBJECT_MW; }

  virtual uint32_t _get_size() = 0;
  virtual char *_get_bytes() = 0;
};

#endif
