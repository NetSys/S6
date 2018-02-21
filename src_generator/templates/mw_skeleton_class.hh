template <>
class Skeleton<%CLASSNAME%> : public MWSkeleton {
 private:
  Skeleton<%CLASSNAME%>() {}
  Skeleton<%CLASSNAME%>(const Skeleton<%CLASSNAME%> &old);
  const Skeleton<%CLASSNAME%> &operator=(const Skeleton<%CLASSNAME%> &old);
  ~Skeleton<%CLASSNAME%>() {}

 public:
  Skeleton<%CLASSNAME%>(int map_id, const Key *key, %CLASSNAME% *obj) {
    _map_id = map_id;
    _key = key;
    _obj = obj;
  }

#define _static_obj (static_cast<%CLASSNAME%*>(_obj))
  void exec(std::uint32_t _method_id, void *_args, void **_ret,
            uint32_t *_ret_size) {
    char *_ptr = reinterpret_cast<char *>(_args);
    (void)_ptr;  // to avoid "unused variable" warning

    switch (_method_id) {
      %METHODS%

      default : assert(0);
    }
  }
#undef _static_obj

  static MWSkeleton *CreateSkeleton(int map_id, const Key *key, void *obj,
                                    bool init) {
    if (init)
      return new Skeleton<%CLASSNAME%>(map_id, key,
                                       new (obj)%CLASSNAME%());
    else
      return new Skeleton<%CLASSNAME%>(map_id, key, (%CLASSNAME% *)obj);
  }
};
