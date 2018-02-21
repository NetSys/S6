#ifndef _DISTREF_KEY_HH_
#define _DISTREF_KEY_HH_

#include <cassert>
#include <functional>
#include <iostream>
#include <type_traits>

#include "archive.hh"
#include "log.hh"
#include "type.hh"

/* Key Interface
 * Identifying global objects within a map
 */

// XXX dynamic_cast is slow
// - static_cast might be OK, if framework use them correctly
#define KEY_CAST(T, V) (dynamic_cast<const T*>(&V))  //#to be removed

#define MAX_KEYS 256

class Key;
typedef int8_t KeyTypeID;
typedef Key* (*key_unserializer)(Archive* ar);

extern key_unserializer __global_key_unserializer[MAX_KEYS];

class Key {
 protected:
  virtual std::ostream& _print(std::ostream& out) const = 0;
  virtual bool _lessthan(const Key& other) const = 0;
  virtual bool _equalto(const Key& other) const = 0;

 public:
  virtual ~Key(){};

  virtual Key* clone() const = 0;
  virtual Key* clone(size_t size, void* buf) const = 0;  // to remove
  virtual Archive* serialize() const = 0;

  static Key* unserialize(Archive* ar) {
    assert(ar->class_type == _S6_KEY);
    return __global_key_unserializer[ar->class_id](ar);
  };

  virtual uint32_t get_key_size() const = 0;  // to remove
  virtual uint8_t* get_bytes() const = 0;     // to remove

  virtual std::size_t _hash() const = 0;

  std::size_t get_hash() const { return _hash(); };

  bool operator<(const Key& other) const { return _lessthan(other); };

  bool operator==(const Key& other) const { return _equalto(other); };

  friend std::ostream& operator<<(std::ostream& out, const Key& key) {
    return key._print(out);
  };
};

void __register_key(const char*, key_unserializer);
KeyTypeID __get_keytypeid(const char*);

#define REGISTER_KEY(name, unserializer)                                  \
  void __keyinitfn_##name(void);                                          \
  void __attribute__((constructor(200), used)) __keyinitfn_##name(void) { \
    __register_key(#name, unserializer);                                  \
  }                                                                       \
  KeyTypeID name::key_tid = __get_keytypeid(#name);

#include "lb_policy.hh"

enum _KEY_LOC_ANNOT : uint8_t { _LCAN_NONE, _LCAN_SRC, _LCAN_DST };

/* Key interface for flow-affinity annotation */
class SticKey : public Key {
 protected:
  static LBPolicy* policy;

 public:
  static void set_LBPolicy(LBPolicy* _policy) { policy = _policy; }
  virtual uint32_t get_locality_hash() const = 0;
};

namespace std {
template <>
struct hash<Key*> {
  size_t operator()(const Key* k1) const { return k1->_hash(); };
};
}

/* structures to translate pointer operations to instance operations for map */
struct _dr_key_less
    : public std::binary_function<const Key*, const Key*, bool> {
  bool operator()(const Key* k1, const Key* k2) const { return *k1 < *k2; };
};

struct _dr_key_hash {
  std::size_t operator()(const Key* k1) const { return std::hash<Key*>()(k1); };
};

struct _dr_key_equal_to
    : public std::binary_function<const Key*, const Key*, bool> {
  bool operator()(const Key* k1, const Key* k2) const { return *k1 == *k2; };
};

typedef std::pair<const Key*, int> VKey;

struct _dr_vkey_equal_to : public std::binary_function<VKey, VKey, bool> {
  bool operator()(VKey c1, VKey c2) const {
    return (*c1.first == *c2.first) && (c1.second == c2.second);
  };
};

struct _dr_vkey_hash {
  std::size_t operator()(VKey c1) const {
    return std::hash<Key*>()(c1.first) + c1.second;
  };
};

#endif
