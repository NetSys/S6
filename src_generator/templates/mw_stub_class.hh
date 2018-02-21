template <>
class MwStub<%CLASSNAME%> : public MwStubBase {
 private:
  int _map_id;
  Key *_key;

 public:
  MwStub() : MwStubBase() {}
  MwStub(int map_id, const Key *key, MwStubManager *mwstub_manager)
      : MwStubBase(mwstub_manager), _map_id(map_id), _key(key->clone()) {}

  ~MwStub() { delete _key; }

  %METHODS%

  static MwStubBase *CreateMwStub(int map_id, const Key *key,
                                  MwStubManager *mwstub_manager) {
   return new MwStub<%CLASSNAME%>(map_id, key, mwstub_manager);
  };
};
