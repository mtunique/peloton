//===----------------------------------------------------------------------===//
//
//                         Peloton
//
// allocator.cpp
//
// Identification: src/backend/common/allocator.cpp
//
// Copyright (c) 2015-16, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include <new>
#include <iostream>
#include <stdlib.h>
#include <stdint.h>
#include <execinfo.h>
#include <unistd.h>

#include "backend/common/macros.h"

# define JEMALLOC_NO_DEMANGLE
# include <jemalloc/jemalloc.h>
# define malloc(size) je_malloc(size)
# define calloc(count, size) je_calloc(count, size)
# define realloc(ptr, size) je_realloc(ptr, size)
# define free(ptr) je_free(ptr)

namespace peloton {

void *do_allocation(size_t size, bool do_throw){

  void *location = malloc(size);
  if (unlikely_branch(!location && do_throw)) {
    throw std::bad_alloc();
  }

  return location;
}

void do_deletion(void *location) {
  free(location);
}

}  // End peloton namespace

void* operator new(size_t size) throw (std::bad_alloc){
  return peloton::do_allocation(size, true);
}

void* operator new(size_t size, const std::nothrow_t&) throw () {
  return peloton::do_allocation(size, false);
}

void* operator new[](size_t size) throw (std::bad_alloc) {
  return peloton::do_allocation(size, true);
}

void* operator new[](size_t size, std::nothrow_t &) throw () {
  return peloton::do_allocation(size, false);
}

void operator delete(void *location) throw () {
  return peloton::do_deletion(location);
}

void operator delete(void *location, const std::nothrow_t &) throw () {
  return peloton::do_deletion(location);
}

void operator delete[](void *location) throw () {
  return peloton::do_deletion(location);
}

void operator delete[](void *location, const std::nothrow_t &) throw () {
  return peloton::do_deletion(location);
}