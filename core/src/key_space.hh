#ifndef __DISTREF_KEY_SPACE_HH_
#define __DISTREF_KEY_SPACE_HH_

#include "key.hh"
#include "type.hh"
#include <iostream>

#define MAX_VERSION 10

// XXX: should be updated with enum LocType 's6ctl/controller.py'
enum LocalityType {
  _LC_NONE,
  _LC_LOCAL,
  _LC_STATIC,
  _LC_HASHING,
  _LC_CHASHING,
  _LC_BALANCED,
};

struct Rule {
  LocalityType loc_type;
  int param; /*	static location id for _LC_STATIC */
};

class KeySpace {
 private:
  int version;
  bool active[MAX_VERSION];
  int node_cnt[MAX_VERSION]; /* static location id for _LC_HASHING,
        _LC_CHASHING,
        _LC_BALANCED */
  Rule rules[MAX_VERSION][_MAX_DMAPS];

 public:
  KeySpace() {
    version = -1;
    for (int i = 0; i < MAX_VERSION; i++)
      active[i] = false;
  }

  KeySpace(LocalityType loc_type, int param) {
    for (int i = 0; i < MAX_VERSION; i++)
      active[i] = false;

    version = 0;
    active[version] = true;

    for (int i = 0; i < _MAX_DMAPS; i++) {
      rules[version][i].loc_type = loc_type;
      rules[version][i].param = param;
    }
  }

  int get_version() { return version; }

  int get_prev_version() { return (version + MAX_VERSION - 1) % MAX_VERSION; }

  int get_next_version() { return (version + MAX_VERSION + 1) % MAX_VERSION; }

  int update_next_version() {
    int next_version = get_next_version();
    if (active[next_version] == true) {
      return -1;
    }

    active[next_version] = true;

    for (int i = 0; i < _MAX_DMAPS; i++) {
      rules[next_version][i].loc_type = _LC_NONE;
      rules[next_version][i].param = 0;
    }
    return next_version;
  }

  int activate_next_version() {
    int next_version = get_next_version();
    if (active[next_version] != true) {
      return -1;
    }
    version = next_version;
    return version;
  }

  void discard_version(int version) { active[version] = false; }

  void set_node_cnt(int v, int node_cnt) { this->node_cnt[v] = node_cnt; }

  void set_rule(int v, LocalityType loc_type, int param) {
    if (v == -1 || !active[v]) {
      v = 0;
      active[v] = true;
    }

    for (int i = 0; i < _MAX_DMAPS; i++) {
      rules[v][i].loc_type = loc_type;
      rules[v][i].param = param;
    }
  }

  void set_rule(int v, int map_id, LocalityType loc_type, int param) {
    if (v == -1 || !active[v]) {
      v = 0;
      active[v] = true;

      for (int i = 0; i < _MAX_DMAPS; i++) {
        rules[v][i].loc_type = loc_type;
        rules[v][i].param = param;
      }
    }

    rules[v][map_id].loc_type = loc_type;
    rules[v][map_id].param = param;
  }

  WorkerID get_manager_of(int map_id, const Key *key) {
    return get_manager_of(version, map_id, key);
  }

  WorkerID get_prev_manager_of(int map_id, const Key *key) {
    return get_manager_of(get_prev_version(), map_id, key);
  }

  WorkerID get_next_manager_of(int map_id, const Key *key) {
    return get_manager_of(get_next_version(), map_id, key);
  }

  WorkerID get_manager_of(int version, int map_id, const Key *key) {
    if (version == -1 || !active[version]) {
      DEBUG_ERR("No active version " << version << " cur_version "
                                     << this->version);
      print_stack();
      assert(0);
      return -1;
    }

    const SticKey *skey = nullptr;

    if (map_id >= _MAX_DMAPS) {
      DEBUG_ERR("No known map id with " << map_id);
      assert(0);
      return -1;
    }

    switch (rules[version][map_id].loc_type) {
      case _LC_LOCAL:
        return -1;
      case _LC_STATIC:
        return rules[version][map_id].param;
      case _LC_HASHING:
        return key->get_hash() % node_cnt[version];
      case _LC_CHASHING:
        DEBUG_ERR("No support for consistent hashing yet");
        assert(0);
        return key->get_hash() % node_cnt[version];
      case _LC_BALANCED:
        skey = KEY_CAST(SticKey, *key);
        if (skey)
          return skey->get_locality_hash() % node_cnt[version];
        else
          return key->get_hash() % node_cnt[version];
      default:
        DEBUG_ERR("No known locality policy for map_id " << map_id << " ");
        assert(0);
        return -1;
    }
  }

  friend std::ostream &operator<<(std::ostream &out, const KeySpace &ks) {
    if (ks.version == -1 || ks.active[ks.version])
      out << "KeySpace has no valid version of rule";

    out << "KeySpace Version: " << ks.version << "\n";
    out << "\tmap_id\trule_name\tparam\n";
    for (int i = 0; i < _MAX_DMAPS; i++) {
      out << "\t" << i << "\t" << ks.rules[ks.version][i].loc_type << "\t"
          << ks.rules[ks.version][i].param << "\n";
    }
    return out;
  }
};

#endif
