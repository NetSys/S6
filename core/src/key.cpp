#include "key.hh"
#include "lb_policy.hh"

#include <unordered_map>

LBPolicy *SticKey::policy = 0;

KeyTypeID num_key_class = 0;
key_unserializer __global_key_unserializer[MAX_KEYS] = {0};
std::unordered_map<std::string, int8_t> __global_keymap
    __attribute__((init_priority(150)));

void __register_key(const char *name, key_unserializer fn) {
  if (num_key_class >= MAX_KEYS) {
    DEBUG_ERR("number of key class exceeds MAX_Keys");
    exit(EXIT_FAILURE);
    return;
  }
  KeyTypeID key_type = num_key_class++;
  __global_key_unserializer[key_type] = fn;
  __global_keymap.insert(std::make_pair(name, (int8_t)key_type));
}

KeyTypeID __get_keytypeid(const char *name) {
  auto iter = __global_keymap.find(std::string(name));
  if (iter == __global_keymap.end()) {
    return -1;
  }

  return iter->second;
}
