#ifndef _DISTREF_SWOBJECT_HH_
#define _DISTREF_SWOBJECT_HH_

#include <cstdint>

#include "d_object.hh"

/* Single-Writer Object <- was 'Floatable' */
class SWObject : public DObject {
 public:
  SWObject() {}
  static DObjType GetObjectType() { return DOBJECT_SW; }
};

#endif
