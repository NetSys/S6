#ifndef _DISTREF_MAP_
#define _DISTREF_MAP_

template <class T>
class Map {
  typedef std::map<Key*, T*, _dr_key_less> _Map;
  typedef std::unordered_map<Key*, T*, _dr_key_hash, _dr_key_equal_to> _UMap;

 private:
  _Map _map;
  _UMap _umap;

 public:
  T* get(Key& key) {
    typename _Map::iterator it = _map.find(&key);
    if (it != _map.end()) {
      printf("existing key\n");
      return it->second;
    } else {
      T* t = new T();
      _map[&key] = t;
      return t;
    }
  };

  T* uget(Key& key) {
    typename _UMap::iterator it = _umap.find(&key);
    if (it != _umap.end()) {
      return it->second;
    } else {
      T* t = new T();
      _umap[&key] = t;
      return t;
    }
  };
};

#endif
