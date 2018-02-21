#ifndef _DISTREF_MESSAGE_HH_
#define _DISTREF_MESSAGE_HH_

#include <iostream>
#include <type_traits>

#include "type.hh"

class ControlBus;
class MessageBuffer;
class Key;

enum MessageType : uint16_t {
  MSG_PING,
  MSG_PONG,
  MSG_MW_RPC_REQUEST,
  MSG_MW_RPC_REQUEST_MULTI_SYM,
  MSG_MW_RPC_REQUEST_MULTI_ASYM,
  MSG_MW_RPC_RESPONSE,
  MSG_MW_AGGR_REQUEST,
  MSG_RWLEASE_REQUEST,
  MSG_RWLEASE_RESPONSE,
  MSG_RWLEASE_EXPIRE_REQUEST,
  MSG_RWLEASE_EXPIRE_RESPONSE,
  MSG_KEY_REQUEST,
  MSG_KEY_RESPONSE,
  MSG_RW_DEL_REQUEST,
  MSG_RW_DEL_RESPONSE,
  MSG_RW_CLEANUP_META_REQUEST,
  MSG_MW_SKELETON_STREAM,
};

struct Message {
  MessageType mtype;
  WorkerID from_id;
  WorkerID to_id;
  uint8_t buf[0];
};

struct MsgPing {
  uint32_t sip;
  uint16_t sport;
};

struct MsgPong {
  uint32_t sip;
  uint16_t sport;
};

/* Message Body Types */

struct RPCRequest {
  uint16_t r_idx;
  uint8_t map_id;
  uint8_t flag;
  uint8_t method_id;
  uint8_t key_size;
  uint16_t args_size;

  // key_offset: (void*) buf
  // args_offset: (void*) bug + key_size
  // buf_size = key_size + arg_size
  uint8_t buf[0];
};

/* RPC request zipping opt 1 */
/* for a single map_id, key, method */
struct RPCRequestMultiSym {
  int count;
  int map_id;
  uint32_t flag;
  uint32_t method_id;
  uint32_t key_size;
  uint32_t args_size;

  // key_offset: (void*) buf
  // args_offset[n] (void*) bug + key_size + args_size * (n-1)
  // buf_size = key_size + arg_size * (count)
  uint8_t buf[0];
};

/* RPC request zipping opt 2 */
/* for different map_id, key, method */
struct RPCRequestMultiAsym {
  int count;  // number of RPCRequest
  uint8_t rpc_request[0];
};

struct RPCResponse {
  int r_idx;
  int map_id;
  uint32_t flag;
  uint32_t method_id;
  uint32_t key_size;
  uint32_t args_size;

  // key_offset: (void*) buf
  // args_offset: (void*) bug + key_size
  // buf_size = key_size + args_size
  uint8_t buf[0];
};

struct MWAggrRequest {
  int map_id;
  uint32_t method_id;
  uint32_t key_size;
  uint32_t obj_size;

  // key_offset: (void*) buf
  // obj_offset: (void*) buf + key_size
  // buf_size = key_size + obj_size
  uint8_t buf[0];
};

struct RWLeaseRequest {
  int rule_version;
  int map_id;
  uint32_t key_size;
  bool force_to_create;

  // key_offset: (void*) buf
  // buf_size = key_size;
  uint8_t buf[0];
};

struct RWLeaseResponse {
  int map_id;
  uint32_t key_size;
  int version;
  uint32_t obj_size;

  // key_offset: (void*) buf
  // obj_offset: (void*) buf + key_size
  // buf_size = key_size + obj_size;
  uint8_t buf[0];
};

struct RWLeaseExpireRequest {
  int map_id;
  int key_size;
  int version;

  // key_offset: (void*) buf
  // buf_size = key_size;
  uint8_t buf[0];
};

struct RWLeaseExpireResponse {
  int map_id;
  int key_size;
  int version;
  uint32_t obj_size;

  // key_offset: (void*) buf
  // obj_offset: (void*) buf + key_size
  // buf_size = key_size + obj_size;
  uint8_t buf[0];
};

struct RWKeyRequest {
  int rule_version;
  int map_id;
  uint32_t key_size;

  // key_offset: (void*) buf
  // buf_size = key_size;
  uint8_t buf[0];
};

struct RWKeyResponse {
  int map_id;
  uint32_t key_size;
  int version;
  uint32_t obj_size;
  int waiters;

  // key_offset: (void*) buf
  // obj_offset: (void*) buf + key_size
  // buf_size = key_size + obj_size;
  uint8_t buf[0];
};

struct RWDeleteRequest {
  int map_id;
  uint32_t key_size;
  int version;
  bool cleanup;

  // key_offset: (void*) buf
  // buf_size = key_size;
  uint8_t buf[0];
};

struct RWDeleteResponse {
  int map_id;
  uint32_t key_size;
  int version;

  // key_offset: (void*) buf
  // buf_size = key_size;
  uint8_t buf[0];
};

struct RWCleanupMetaRequest {
  int map_id;
  uint32_t key_size;
  int version;

  // key_offset: (void*) buf
  // buf_size = key_size;
  uint8_t buf[0];
};

struct SkeletonStream {
  int map_id;
  uint32_t obj_count;
  uint32_t key_size;
  uint32_t buf_size;

  // key_offset: (void *) buf
  // buf_size =
  uint8_t buf[0];
};

void fill_mw_rpc_request(RPCRequest *rpc, int r_idx, int map_id,
                         uint32_t key_size, const Key *key, uint32_t flag,
                         uint32_t method_id, void *args, uint32_t args_size);

MessageBuffer *create_ping(ControlBus *cbus, WorkerID from, WorkerID to,
                           uint32_t sip, uint16_t sp);

MessageBuffer *create_pong(ControlBus *cbus, WorkerID from, WorkerID to,
                           uint32_t sip, uint16_t sp);

MessageBuffer *create_mw_rpc_request(ControlBus *cbus, WorkerID from,
                                     WorkerID to, int r_idx, int map_id,
                                     const Key *key, uint32_t flag,
                                     uint32_t method_id, void *args,
                                     uint32_t args_size);

MessageBuffer *create_mw_rpc_response(ControlBus *cbus, WorkerID from,
                                      WorkerID to, int r_idx, int map_id,
                                      const Key *key, uint32_t flag,
                                      uint32_t method_id, void *args,
                                      uint32_t args_size);

MessageBuffer *create_mw_aggr_request(ControlBus *cbus, WorkerID from,
                                      WorkerID to, int map_id, const Key *key,
                                      void *obj, uint32_t obj_size);

MessageBuffer *create_object_ownership_request(ControlBus *cbus, WorkerID from,
                                               WorkerID to, int rule_version,
                                               int map_id, const Key *key,
                                               bool force_to_create);

MessageBuffer *create_object_ownership_response(ControlBus *cbus, WorkerID from,
                                                WorkerID to, int map_id,
                                                const Key *key, int version,
                                                void *obj, uint32_t obj_size);

MessageBuffer *create_object_ownership_expire_request(ControlBus *cbus,
                                                      WorkerID from,
                                                      WorkerID to, int map_id,
                                                      const Key *key,
                                                      int version);

MessageBuffer *create_object_ownership_expire_response(
    ControlBus *cbus, WorkerID from, WorkerID to, int map_id, const Key *key,
    int version, void *obj, uint32_t obj_size);

MessageBuffer *create_key_ownership_request(ControlBus *cbus, WorkerID from,
                                            WorkerID to, int rule_version,
                                            int map_id, const Key *key);

MessageBuffer *create_key_ownership_response(ControlBus *cbus, WorkerID from,
                                             WorkerID to, int rule_version,
                                             int map_id, const Key *key,
                                             int version, void *obj,
                                             uint32_t obj_size, int waiters);

MessageBuffer *create_rwobj_del_request(ControlBus *cbus, WorkerID from,
                                        WorkerID to, int map_id, const Key *key,
                                        int version, bool cleanup);

MessageBuffer *create_rwobj_del_response(ControlBus *cbus, WorkerID from,
                                         WorkerID to, int map_id,
                                         const Key *key, int version);

MessageBuffer *create_rwobj_cleanup_meta_request(ControlBus *cbus,
                                                 WorkerID from, WorkerID to,
                                                 int map_id, const Key *key,
                                                 int version);

#endif /* _DISTREF_MESSAGE_H */
