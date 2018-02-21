#ifndef _DISTREF_MEM_ALLOC_HH_
#define _DISTREF_MEM_ALLOC_HH_

#include <assert.h>
#include <iostream>
#include <string.h>
#include <sys/mman.h>

#include "log.hh"

class MemPool {
 private:
  typedef struct tag_mem_chunk {
    int mc_free_chunks;
    struct tag_mem_chunk *mc_next;
  } mem_chunk;

  bool use_system_malloc = false;

  int total_chunks;
  int free_chunks;
  size_t chunk_size;

  void *start_ptr = nullptr;
  mem_chunk *free_ptr = nullptr;

 public:
  MemPool() {
    use_system_malloc = true;

    total_chunks = 0;
    free_chunks = 0;
    chunk_size = 0;

    start_ptr = nullptr;
    free_ptr = nullptr;

    DEBUG_INFO("Fallback to use system malloc");
  }

  MemPool(size_t _chunk_size, int _chunk_count) {
    if (_chunk_size < sizeof(mem_chunk)) {
      std::cerr << "Chunk size should be larger than " << sizeof(mem_chunk)
                << std::endl;
      return;
    }

    if (_chunk_size % 4 != 0) {
      _chunk_size = _chunk_size / 4 * 4 + 4;
    }

    this->total_chunks = _chunk_count;
    this->free_chunks = _chunk_count;
    this->chunk_size = _chunk_size;

    uint64_t total_size = this->chunk_size * this->total_chunks;

    int ret = posix_memalign((void **)&start_ptr, getpagesize(), total_size);
    if (ret != 0) {
      std::cerr << "Fail to allocate" << std::endl;
      return;
    }

    if (geteuid() == 0) {
      if (mlock(start_ptr, total_size) < 0)
        std::cerr << "Fail to locking memory" << std::endl;
    }

    free_ptr = (mem_chunk *)start_ptr;
    free_ptr->mc_free_chunks = free_chunks;
    free_ptr->mc_next = nullptr;

    DEBUG_INFO("Memory pool for chunk size "
               << this->chunk_size << " chunk count " << this->total_chunks);
  }

  ~MemPool() {
    if (start_ptr)
      ::free(start_ptr);
  }

  void *malloc(size_t size) {
    if (use_system_malloc)
      return ::malloc(size);

    if (size > chunk_size) {
      std::cerr << "requested size " << size << " should be smaller than "
                << chunk_size << std::endl;
    }

    mem_chunk *p = free_ptr;

    if (free_chunks == 0)
      return nullptr;

    assert(free_chunks > 0 && p->mc_free_chunks <= free_chunks);

    p->mc_free_chunks--;
    free_chunks--;

    if (p->mc_free_chunks) {
      free_ptr = (mem_chunk *)((u_char *)p + chunk_size);
      free_ptr->mc_free_chunks = p->mc_free_chunks;
      free_ptr->mc_next = p->mc_next;
    } else {
      free_ptr = p->mc_next;
    }

    return p;
  }

  void free(void *m) {
    if (use_system_malloc)
      return ::free(m);

    mem_chunk *p = (mem_chunk *)m;

    assert(((u_char *)p - (u_char *)start_ptr) % chunk_size == 0);

    p->mc_free_chunks = 1;
    p->mc_next = free_ptr;

    free_ptr = p;
    free_chunks++;

    return;
  }
};
#endif
