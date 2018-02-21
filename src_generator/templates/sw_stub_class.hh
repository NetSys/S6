template <>
class SwStub<%CLASSNAME%> : public SwStubBase {
 private:
 public:
  SwStub<%CLASSNAME%>(SwStubManager *swstub_manager, int map_id,
                      const Key *key, bool is_const, %CLASSNAME% *obj,
                      int version)
      : SwStubBase(swstub_manager) {
    _map_id = map_id;
    if (key)
      _key = key->clone();
    else
      _key = nullptr;
    _is_const = is_const;
    _obj = obj;
    _obj_version = version;

    if (!is_const)
      _obj_size = sizeof(%CLASSNAME%);
  }

  ~SwStub<%CLASSNAME%>() {
    if (_key)
      delete _key;
  }

#define _static_obj (static_cast<%CLASSNAME%*>(_obj))
  void exec(int _method_id, void *_args, uint32_t args_size, void **_ret,
            uint32_t *_ret_size) {
    char *_ptr = reinterpret_cast<char *>(_args);
    (void)_ptr;  // to avoid "unused variable" warning

    switch (_method_id) {
      %EXEC_METHODS%

      default : assert(0);
    }
  }

  %METHODS%

  friend std::ostream &operator<<(std::ostream &out,
                                  const SwStub<%CLASSNAME%> &ref) {
    return out << ref._obj;
  }

#undef _static_obj

  static SwStubBase *CreateSwStub(SwStubManager *swstub_manager, int map_id,
                                  const Key *key, int version, void *obj,
                                  bool init, bool is_const)

  {
    /* Read only */
    if (is_const) {
      return new SwStub<%CLASSNAME%>(swstub_manager, map_id, key,
                                     true /* it is read only */, nullptr,
                                     version);
    }

    if (!obj) {
      obj = malloc(sizeof(%CLASSNAME%));
      init = true;
    }

    if (init)
      return new SwStub<%CLASSNAME%>(swstub_manager, map_id, key,
                                       false /* it is rw */,
                                       new (obj) %CLASSNAME%(), version);
    else
      return new SwStub<%CLASSNAME%>(swstub_manager, map_id, key,
                                       false /* it is rw */,
                                       (%CLASSNAME% *)obj, version);
  }
};
