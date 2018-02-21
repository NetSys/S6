#ifndef _DISTREF_ARCHIVE_HH_
#define _DISTREF_ARCHIVE_HH_

#include <cstdint>

#define _S6_KEY 0
#define _S6_OBJ 1

struct Archive {
  uint32_t size;
  uint16_t class_id;
  uint8_t class_type;
  uint8_t data[0];
};

#endif
