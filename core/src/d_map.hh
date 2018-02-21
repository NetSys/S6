#ifndef _DR_MAP_HH_
#define _DR_MAP_HH_

#include <map>
#include <unordered_map>

#include "key.hh"

#define MAX_ITERATOR_CNT 10

template <class T>
class DMap {
  typedef std::map<const Key*, T*, _dr_key_less> _Map;

  struct MapIterator {
    int map_id = -1;
    bool is_valid = false;
    typename _Map::iterator current;
  };

 private:
  _Map _map;

  int iter_cnt = 0;
  MapIterator _iterator[MAX_ITERATOR_CNT];

 public:
  T* get(const Key& key) {
    typename _Map::iterator it = _map.find(&key);
    if (it != _map.end()) {
      return it->second;
    } else {
      T* t = new T();
      _map[key.clone()] = t;
      return t;
    }
  };

  int create_iterator(int map_id) {
    if (iter_cnt >= MAX_ITERATOR_CNT)
      return -1;

    MapIterator* iter;
    int iter_idx = 0;
    for (; iter_idx < MAX_ITERATOR_CNT; iter_idx++) {
      iter = &_iterator[iter_cnt];
      if (!iter->is_valid)
        break;
    }

    if (iter_idx >= MAX_ITERATOR_CNT)
      return -1;

    iter->map_id = map_id;
    iter->is_valid = true;
    iter->current = _map.begin();

    return iter_idx;
  };

  const Key* get_next_key(int iter_idx) {
    if (_iterator[iter_idx].current == _map.end())
      return nullptr;

    const Key* key = _iterator[iter_idx].current->first;
    _iterator[iter_idx].current++;
    return key;
  };

  void release_iterator(int iter_idx) {
    _iterator[iter_idx].is_valid = false;
    return;
  };
};

template <class T>
class UDMap {
  typedef std::unordered_map<const Key*, T*, _dr_key_hash, _dr_key_equal_to>
      _UMap;

 private:
  _UMap _umap;

 public:
  T* uget(const Key& key) {
    typename _UMap::iterator it = _umap.find(&key);
    if (it != _umap.end()) {
      return it->second;
    } else {
      T* t = new T();
      _umap[key.clone()] = t;
      return t;
    }
  };
};

#endif
