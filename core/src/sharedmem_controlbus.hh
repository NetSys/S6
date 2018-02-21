#ifndef _DISTREF_NETWORK_EMULATOR_HH_
#define _DISTREF_NETWORK_EMULATOR_HH_

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <queue>
#include <tuple>
#include <unordered_map>

#include "controlbus.hh"
#include "worker_address.hh"

class SharedMemControlBus : public ControlBus {
 private:
  // global mutex to manimpulate unordered map
  std::mutex mutex;

 protected:
  std::unordered_map<
      WorkerAddress,
      std::tuple<Connector*, std::queue<MessageBuffer*>*, std::mutex*> >
      workers;

  SharedMemControlBus(){};
  SharedMemControlBus(const SharedMemControlBus& old){};
  const SharedMemControlBus& operator=(const SharedMemControlBus& old);

 public:
  static SharedMemControlBus* GetSharedMemControlBus() {
    static SharedMemControlBus* pInstance = new SharedMemControlBus();
    return pInstance;
  }

  virtual ~SharedMemControlBus(){};

 public:
  virtual Connector* register_address(const WorkerAddress& addr);
  virtual MessageBuffer* allocate_message(const std::size_t messageSize) const;
  virtual MessageBuffer* init_message(uint8_t* buf, std::size_t buf_size) const;

 private:
  virtual bool send(MessageBuffer* message, const WorkerAddress& from,
                    const WorkerAddress& to);
  virtual MessageBuffer* receive(const WorkerAddress& me);

  virtual void unregister_address(const WorkerAddress& worker);
};

#endif /* _DISTREF_NETWORK_EMULATOR_HH_ */
