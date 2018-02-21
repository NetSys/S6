#ifndef _DISTREF_SREF_HH_
#define _DISTREF_SREF_HH_

#include <iostream>

#include "d_object.hh"
#include "reference_interceptor.hh"
#include "sw_stub.hh"

#include "log.hh"

template <class X>
class SwStub;
class ReferenceInterceptor;

struct SwStubSerial {
  int size;
  int map_id;
  bool is_const;
  Archive* key_archive;
};

template <class X>
class SwRef {
 public:
  /* constructors */
  SwRef() {
    DEBUG_REF("default constructor " << this);

    /* we don't know whether the reference is const or not */
    map_id = -1;
    key = nullptr;
    is_const = true;
    is_registered = false;
    ref_p = nullptr;
  }

  /* embeded object constructor */
  SwRef(void* object) {
    // XXX relationship with parents object required
    DEBUG_REF("default constructor with object");

    map_id = -1;
    key = nullptr;

    if (object == nullptr) {
      is_const = true;
      is_registered = false;
      ref_p = nullptr;
    } else {
      is_const = true;
      ref_p = (SwStub<X>*)SwStub<X>::CreateSwStub(nullptr, map_id, key, -1,
                                                  (X*)object, true /* init */,
                                                  false /* is_const */);
      is_registered = false;
    }
  }

  // Create a reference for 'new object'
  SwRef(int mid, const Key* k, RefState& state)
      : map_id(mid), key(k->clone()), is_const(false) {
    // XXX clonning every key might be inefficient :(
    DEBUG_REF("default constructor " << this << "[" << map_id << ":" << key
                                     << "]"
                                     << " is_const " << is_const);

    ref_p = HOOK->create_object(this, state);
    if (ref_p)
      is_registered = true;
    else
      is_registered = false;
  }

  // Create a reference for 'object', if the object doesn't exist, create it
  SwRef(int mid, const Key* k) : map_id(mid), key(k->clone()), is_const(false) {
    // XXX clonning every key might be inefficient :(
    DEBUG_REF("default constructor " << this << "[" << map_id << ":" << key
                                     << "]"
                                     << " is_const " << is_const);

    ref_p = HOOK->get_object(this);
    if (ref_p)
      is_registered = true;
    else
      is_registered = false;
  }

  // lookup a reference for 'existing objects'
  SwRef(int mid, const Key* k, bool _is_const)
      : map_id(mid), key(k->clone()), is_const(_is_const) {
    // XXX clonning every key might be inefficient :(
    DEBUG_REF("default constructor " << this << "[" << map_id << ":" << key
                                     << "]"
                                     << " is_const " << is_const);

    ref_p = HOOK->lookup_object(this);
    if (ref_p)
      is_registered = true;
    else
      is_registered = false;
  }

  SwRef(const SwRef& r) {
    DEBUG_REF("copy constructor " << this << "[" << r.map_id << ":" << r.key
                                  << "]"
                                  << " is_const " << r.is_const);

    map_id = r.map_id;
    if (r.key)
      key = r.key->clone();
    else
      key = nullptr;

    is_const = r.is_const;
    is_registered = false;

    if (key) {
      ref_p = HOOK->lookup_object(this);
      if (ref_p)
        is_registered = true;
    } else
      ref_p = nullptr;
  }

  /* destructors */
  ~SwRef() {
    DEBUG_REF("destructor " << this);
    if (is_registered)
      HOOK->release_ref(this);

    if (key != nullptr)
      delete key;
  }

  /* remove objects */
  void delete_object() {
    DEBUG_REF("remove objects ");

    HOOK->delete_object(this);
    is_registered = false;

    if (key != nullptr)
      delete key;
    key = nullptr;

    ref_p = nullptr;
  }

  const SwRef& operator=(const SwRef& r) const {
    DEBUG_REF("assignment operator: copy requested to "
              << this << "[" << r.map_id << ":" << r.key << "]"
              << " is_const " << r.is_const);

    map_id = r.map_id;
    is_const = r.is_const;

    if (r.key != nullptr) {
      key = r.key->clone();
      ref_p = HOOK->lookup_object(this);
    } else
      ref_p = r.ref_p;

    if (ref_p)
      is_registered = true;
    else
      is_registered = false;

    return *this;
  }

// XXX We may prohibit * operation on SwRef
#if 0 
		/* access operations '*' */
		const X& operator*() const  {
			DEBUG_REF ("const * operator " << sw->object);	
			return *sw->object; 
		}

		X& operator*() { 
			DEBUG_REF (" * operator " << sw->object);	
			return *sw->object; 
		}
#endif

  SwStub<X>* get() const { return ref_p; }

  const SwStub<X>* operator->() const {
    if (ref_p == nullptr)
      return nullptr;

    DEBUG_REF("const -> operator " << ref_p);
    return ref_p;
  }

  SwStub<X>* operator->() {
    if (ref_p == nullptr)
      return nullptr;

    DEBUG_REF(" -> operator " << ref_p);
    return ref_p;
  }

  explicit operator bool() const { return (ref_p != nullptr); }

  bool isConst() const { return is_const; }

  Key* getKey() const { return key; }

  int getMapId() const { return map_id; }

  int getObjectType() const { return DOBJECT_SW; }

  int getVersion() const { return ((SwStubBase*)ref_p)->_obj_version; }

  int get_serial_size() const {
    return sizeof(SwStubSerial) + key->get_key_size();
  }

  /* How to handle SwRef when serialize/deserialize */

  struct SwStubSerial* serialize() const {
    size_t buf_size = sizeof(SwStubSerial) + key->get_key_size();
    void* buf = malloc(buf_size);

    struct SwStubSerial* dr_buf = (struct SwStubSerial*)buf;
    dr_buf->size = buf_size;
    dr_buf->map_id = map_id;
    dr_buf->is_const = is_const;
    dr_buf->key_archive = key->serialize();

    return dr_buf;
  }

  static SwRef<X>* deserialize(struct SwStubSerial* s) {
    Key* key = Key::unserialize(s->key_archive);
    SwRef<X>* dr = new SwRef<X>(s->map_id, key, s->is_const);
    return dr;
  }

  friend std::ostream& operator<<(std::ostream& out, const SwRef& r) {
    return out << (SwStub<X>&)(*r.ref_p);
  }

  friend bool operator==(const SwRef<X>& lhs, const SwRef<X>& rhs) {
    if (lhs.map_id == -1 || rhs.map_id == -1)
      return lhs.ref_p == rhs.ref_p;

    return (lhs.map_id == rhs.map_id) && (*lhs.key == *rhs.key) &&
           (lhs.ref_p == rhs.ref_p);
  };

  friend bool operator!=(const SwRef<X>& lhs, const SwRef<X>& rhs) {
    return !(lhs == rhs);
  };

 private:
  thread_local static ReferenceInterceptor* HOOK;

  /* object identifier (map_id, key) tuple */
  mutable int map_id;
  mutable Key* key;

  /* reference characteristics */
  mutable bool is_const;
  mutable bool is_registered;

  /* reference for SW/MW-Objects */
  mutable SwStub<X>* ref_p;
};

template <class X>
thread_local ReferenceInterceptor* SwRef<X>::HOOK =
    ReferenceInterceptor::GetReferenceInterceptor();

#endif  // #ifndef _DISTREF_DIST_REF_H
