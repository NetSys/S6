#ifndef _DISTREF_SHA1_KEY_HH_
#define _DISTREF_SHA1_KEY_HH_

#include <iomanip>
#include <openssl/sha.h>
#include <string.h>

#include "key_base.hh"

class SHA1Key : public Key {
 private:
  unsigned char hash[SHA_DIGEST_LENGTH];

  std::ostream &_print(std::ostream &out) const {
    out << std::hex << std::setfill('0');
    for (int i = 0; i < SHA_DIGEST_LENGTH; i++)
      out << std::setw(2) << int(hash[i]);
    return out;
  }

  bool _lessthan(const Key &other) const {
    const SHA1Key *kother = KEY_CAST(SHA1Key, other);
    if (!kother) {
      DEBUG_ERR("Dynamic cast fail from Key to SHA1Key");
      return false;
    }

    for (int i = 0; i < SHA_DIGEST_LENGTH; i++)
      if (this->hash[i] < kother->hash[i])
        return true;
      else if (this->hash[i] > kother->hash[i])
        return false;

    return false;
  }

  bool _equalto(const Key &other) const {
    const SHA1Key *kother = KEY_CAST(SHA1Key, other);
    if (!kother) {
      DEBUG_ERR("Dynamic cast fail from Key to SHA1Key");
      return false;
    }

    for (int i = 0; i < SHA_DIGEST_LENGTH; i++)
      if (this->hash[i] != kother->hash[i])
        return false;
    return true;
  }

 public:
  static KeyTypeID key_tid;

  SHA1Key(const char *str) : Key() {
    SHA1((const unsigned char *)str, sizeof(str) - 1, hash);
  }

  SHA1Key *clone() const { return new SHA1Key(*this); }

  SHA1Key *clone(size_t size, void *buf) const {
    if (size < sizeof(SHA1Key))
      return nullptr;

    memcpy(buf, this, sizeof(SHA1Key));
    return (SHA1Key *)buf;
  };

  Archive *serialize() const {
    Archive *ar = (Archive *)malloc(sizeof(Archive) + sizeof(SHA1Key));
    ar->size = sizeof(SHA1Key);
    ar->class_type = _S6_KEY;
    ar->class_id = SHA1Key::key_tid;
    new (ar->data) SHA1Key(*this);
    return ar;
  };

  uint32_t get_key_size() const { return sizeof(SHA1Key); }

  uint8_t *get_bytes() const { return (uint8_t *)this; }

  std::size_t _hash() const {
    uint32_t ret = 0;
    for (size_t i = 0; i < 4; i++)
      ret = ret | (hash[i] << i);
    return ret;
  };

  static Key *unserialize(Archive *ar) {
    assert(ar->class_id == SHA1Key::key_tid);
    return new SHA1Key(*(SHA1Key *)(void *)ar->data);
  }
};

#endif
