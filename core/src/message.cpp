#include "message.hh"
#include "controlbus.hh"
#include "key.hh"
#include "log.hh"
#include <cassert>
#include <cstring>
#include <iostream>

void fill_mw_rpc_request(RPCRequest *rpc, int r_idx, int map_id,
                         uint32_t key_size, const Key *key, uint32_t flag,
                         uint32_t method_id, void *args, uint32_t args_size) {
  rpc->r_idx = r_idx;
  rpc->map_id = map_id;
  rpc->flag = flag;
  rpc->method_id = method_id;
  rpc->key_size = key_size;
  rpc->args_size = args_size;

  memcpy(rpc->buf, key->get_bytes(), rpc->key_size);
  memcpy(rpc->buf + key_size, args, rpc->args_size);

  return;
}

MessageBuffer *create_ping(ControlBus *cbus, WorkerID from, WorkerID to,
                           uint32_t sip, uint16_t sp) {
  int msg_size = sizeof(Message) + sizeof(MsgPing);

  MessageBuffer *mb = cbus->allocate_message(msg_size);
  Message *m = (Message *)mb->get_message_body();
  m->mtype = MSG_PING;
  m->from_id = from;
  m->to_id = to;

  MsgPing *ping = (MsgPing *)(void *)m->buf;
  ping->sip = sip;
  ping->sport = sp;

  return mb;
}

MessageBuffer *create_pong(ControlBus *cbus, WorkerID from, WorkerID to,
                           uint32_t sip, uint16_t sp) {
  int msg_size = sizeof(Message) + sizeof(MsgPong);

  MessageBuffer *mb = cbus->allocate_message(msg_size);
  Message *m = (Message *)mb->get_message_body();
  m->mtype = MSG_PONG;
  m->from_id = from;
  m->to_id = to;

  MsgPong *pong = (MsgPong *)(void *)m->buf;
  pong->sip = sip;
  pong->sport = sp;

  return mb;
}

MessageBuffer *create_mw_rpc_request(ControlBus *cbus, WorkerID from,
                                     WorkerID to, int r_idx, int map_id,
                                     const Key *key, uint32_t flag,
                                     uint32_t method_id, void *args,
                                     uint32_t args_size) {
  uint32_t key_size = key->get_key_size();
  int msg_size = sizeof(Message) + sizeof(RPCRequest) + key_size + args_size;

  MessageBuffer *mb = cbus->allocate_message(msg_size);
  Message *m = (Message *)mb->get_message_body();
  m->mtype = MSG_MW_RPC_REQUEST;
  m->from_id = from;
  m->to_id = to;

  fill_mw_rpc_request((RPCRequest *)(void *)m->buf, r_idx, map_id, key_size,
                      key, flag, method_id, args, args_size);

  return mb;
}

MessageBuffer *create_mw_rpc_response(ControlBus *cbus, WorkerID from,
                                      WorkerID to, int r_idx, int map_id,
                                      const Key *key, uint32_t flag,
                                      uint32_t method_id, void *args,
                                      uint32_t args_size) {
  uint32_t key_size = key->get_key_size();
  int msg_size = sizeof(Message) + sizeof(RPCResponse) + key_size + args_size;

  MessageBuffer *mb = cbus->allocate_message(msg_size);
  Message *m = (Message *)mb->get_message_body();
  m->mtype = MSG_MW_RPC_RESPONSE;
  m->from_id = from;
  m->to_id = to;

  RPCResponse *rpc = (RPCResponse *)(void *)m->buf;
  rpc->r_idx = r_idx;
  rpc->map_id = map_id;
  rpc->flag = flag;
  rpc->method_id = method_id;
  rpc->key_size = key_size;
  rpc->args_size = args_size;

  memcpy(rpc->buf, key->get_bytes(), rpc->key_size);
  memcpy(rpc->buf + key_size, args, rpc->args_size);

  return mb;
}

MessageBuffer *create_mw_aggr_request(ControlBus *cbus, WorkerID from,
                                      WorkerID to, int map_id, const Key *key,
                                      void *obj, uint32_t obj_size) {
  uint32_t key_size = key->get_key_size();
  int msg_size = sizeof(Message) + sizeof(MWAggrRequest) + key_size + obj_size;

  MessageBuffer *mb = cbus->allocate_message(msg_size);
  Message *m = (Message *)mb->get_message_body();
  m->mtype = MSG_MW_AGGR_REQUEST;
  m->from_id = from;
  m->to_id = to;

  MWAggrRequest *rpc = (MWAggrRequest *)(void *)m->buf;
  rpc->map_id = map_id;
  rpc->key_size = key_size;
  rpc->obj_size = obj_size;

  memcpy(rpc->buf, key->get_bytes(), rpc->key_size);
  memcpy(rpc->buf + key_size, obj, rpc->obj_size);

  return mb;
}

MessageBuffer *create_object_ownership_request(ControlBus *cbus, WorkerID from,
                                               WorkerID to, int rule_version,
                                               int map_id, const Key *key,
                                               bool force_to_create) {
  uint32_t key_size = key->get_key_size();
  int msg_size = sizeof(Message) + sizeof(RWLeaseRequest) + key_size;

  MessageBuffer *mb = cbus->allocate_message(msg_size);
  Message *m = (Message *)mb->get_message_body();
  m->mtype = MSG_RWLEASE_REQUEST;
  m->from_id = from;
  m->to_id = to;

  RWLeaseRequest *req = (RWLeaseRequest *)(void *)m->buf;
  req->rule_version = rule_version;
  req->map_id = map_id;
  req->key_size = key_size;
  req->force_to_create = force_to_create;
  memcpy(req->buf, key->get_bytes(), key_size);

  return mb;
}

MessageBuffer *create_object_ownership_response(ControlBus *cbus, WorkerID from,
                                                WorkerID to, int map_id,
                                                const Key *key, int version,
                                                void *obj, uint32_t obj_size) {
  uint32_t key_size = key->get_key_size();
  int msg_size =
      sizeof(Message) + sizeof(RWLeaseResponse) + key_size + obj_size;

  MessageBuffer *mb = cbus->allocate_message(msg_size);
  Message *m = (Message *)mb->get_message_body();
  m->mtype = MSG_RWLEASE_RESPONSE;
  m->from_id = from;
  m->to_id = to;

  RWLeaseResponse *res = (RWLeaseResponse *)(void *)m->buf;
  res->map_id = map_id;
  res->key_size = key_size;
  res->version = version;
  res->obj_size = obj_size;

  if (key_size)
    memcpy(res->buf, key->get_bytes(), key_size);

  if (obj_size)
    memcpy(res->buf + key_size, obj, obj_size);

  return mb;
}

MessageBuffer *create_object_ownership_expire_request(ControlBus *cbus,
                                                      WorkerID from,
                                                      WorkerID to, int map_id,
                                                      const Key *key,
                                                      int version) {
  uint32_t key_size = key->get_key_size();
  int msg_size = sizeof(Message) + sizeof(RWLeaseExpireRequest) + key_size;

  MessageBuffer *mb = cbus->allocate_message(msg_size);
  Message *m = (Message *)mb->get_message_body();
  m->mtype = MSG_RWLEASE_EXPIRE_REQUEST;
  m->from_id = from;
  m->to_id = to;

  RWLeaseExpireRequest *req = (RWLeaseExpireRequest *)(void *)m->buf;
  req->map_id = map_id;
  req->key_size = key_size;
  req->version = version;

  memcpy(req->buf, key->get_bytes(), key_size);

  return mb;
}

MessageBuffer *create_object_ownership_expire_response(
    ControlBus *cbus, WorkerID from, WorkerID to, int map_id, const Key *key,
    int version, void *obj, uint32_t obj_size) {
  uint32_t key_size = key->get_key_size();
  int msg_size =
      sizeof(Message) + sizeof(RWLeaseExpireResponse) + key_size + obj_size;

  MessageBuffer *mb = cbus->allocate_message(msg_size);
  Message *m = (Message *)mb->get_message_body();
  m->mtype = MSG_RWLEASE_EXPIRE_RESPONSE;
  m->from_id = from;
  m->to_id = to;

  RWLeaseExpireResponse *req = (RWLeaseExpireResponse *)(void *)m->buf;
  req->map_id = map_id;
  req->key_size = key_size;
  req->version = version;
  req->obj_size = obj_size;

  memcpy(req->buf, key->get_bytes(), key_size);
  memcpy(req->buf + key_size, obj, obj_size);

  return mb;
}

MessageBuffer *create_key_ownership_request(ControlBus *cbus, WorkerID from,
                                            WorkerID to, int rule_version,
                                            int map_id, const Key *key) {
  uint32_t key_size = key->get_key_size();
  int msg_size = sizeof(Message) + sizeof(RWKeyRequest) + key_size;

  MessageBuffer *mb = cbus->allocate_message(msg_size);
  Message *m = (Message *)mb->get_message_body();
  m->mtype = MSG_KEY_REQUEST;
  m->from_id = from;
  m->to_id = to;

  RWKeyRequest *req = (RWKeyRequest *)(void *)m->buf;
  req->rule_version = rule_version;
  req->map_id = map_id;
  req->key_size = key_size;
  memcpy(req->buf, key->get_bytes(), key_size);

  return mb;
}

MessageBuffer *create_key_ownership_response(ControlBus *cbus, WorkerID from,
                                             WorkerID to, int rule_version,
                                             int map_id, const Key *key,
                                             int version, void *obj,
                                             uint32_t obj_size, int waiters) {
  uint32_t key_size = key->get_key_size();
  int msg_size = sizeof(Message) + sizeof(RWKeyResponse) + key_size + obj_size;

  MessageBuffer *mb = cbus->allocate_message(msg_size);
  Message *m = (Message *)mb->get_message_body();
  m->mtype = MSG_KEY_RESPONSE;
  m->from_id = from;
  m->to_id = to;

  RWKeyResponse *res = (RWKeyResponse *)(void *)m->buf;
  res->map_id = map_id;
  res->key_size = key_size;
  res->version = version;
  res->obj_size = obj_size;
  res->waiters = waiters;

  if (key_size)
    memcpy(res->buf, key->get_bytes(), key_size);

  if (obj_size)
    memcpy(res->buf + key_size, obj, obj_size);

  return mb;
}

MessageBuffer *create_rwobj_del_request(ControlBus *cbus, WorkerID from,
                                        WorkerID to, int map_id, const Key *key,
                                        int version, bool cleanup) {
  uint32_t key_size = key->get_key_size();
  int msg_size = sizeof(Message) + sizeof(RWDeleteRequest) + key_size;

  MessageBuffer *mb = cbus->allocate_message(msg_size);
  Message *m = (Message *)mb->get_message_body();
  m->mtype = MSG_RW_DEL_REQUEST;
  m->from_id = from;
  m->to_id = to;

  RWDeleteRequest *req = (RWDeleteRequest *)(void *)m->buf;
  req->map_id = map_id;
  req->key_size = key_size;
  req->version = version;
  req->cleanup = cleanup;

  DEBUG_OBJ("msg: local delete object request " << *key << " from " << from
                                                << " to " << to << " cleanup "
                                                << cleanup);

  memcpy(req->buf, key->get_bytes(), key_size);

  return mb;
}

MessageBuffer *create_rwobj_del_response(ControlBus *cbus, WorkerID from,
                                         WorkerID to, int map_id,
                                         const Key *key, int version) {
  uint32_t key_size = key->get_key_size();
  int msg_size = sizeof(Message) + sizeof(RWDeleteResponse) + key_size;

  MessageBuffer *mb = cbus->allocate_message(msg_size);
  Message *m = (Message *)mb->get_message_body();
  m->mtype = MSG_RW_DEL_RESPONSE;
  m->from_id = from;
  m->to_id = to;

  RWDeleteResponse *res = (RWDeleteResponse *)(void *)m->buf;
  res->map_id = map_id;
  res->key_size = key_size;
  res->version = version;

  memcpy(res->buf, key->get_bytes(), key_size);

  return mb;
}

MessageBuffer *create_rwobj_cleanup_meta_request(ControlBus *cbus,
                                                 WorkerID from, WorkerID to,
                                                 int map_id, const Key *key,
                                                 int version) {
  uint32_t key_size = key->get_key_size();
  int msg_size = sizeof(Message) + sizeof(RWCleanupMetaRequest) + key_size;

  MessageBuffer *mb = cbus->allocate_message(msg_size);
  Message *m = (Message *)mb->get_message_body();
  m->mtype = MSG_RW_CLEANUP_META_REQUEST;
  m->from_id = from;
  m->to_id = to;

  RWCleanupMetaRequest *req = (RWCleanupMetaRequest *)(void *)m->buf;
  req->map_id = map_id;
  req->key_size = key_size;
  req->version = version;

  memcpy(req->buf, key->get_bytes(), key_size);

  return mb;
}
