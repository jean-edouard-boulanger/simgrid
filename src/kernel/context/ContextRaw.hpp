/* Copyright (c) 2009-2019. The SimGrid Team. All rights reserved.          */

/* This program is free software; you can redistribute it and/or modify it
 * under the terms of the license (GNU LGPL) which comes with this package. */

#ifndef SIMGRID_SIMIX_RAW_CONTEXT_HPP
#define SIMGRID_SIMIX_RAW_CONTEXT_HPP

#include <atomic>
#include <cstdint>
#include <functional>
#include <vector>

#include <xbt/parmap.hpp>
#include <xbt/xbt_os_thread.h>

#include "src/kernel/context/ContextSwapped.hpp"

namespace simgrid {
namespace kernel {
namespace context {

/** @brief Fast context switching inspired from SystemV ucontexts.
  *
  * The main difference to the System V context is that Raw Contexts are much faster because they don't
  * preserve the signal mask when switching. This saves a system call (at least on Linux) on each context switch.
  */
class RawContext : public SwappedContext {
public:
  RawContext(std::function<void()> code, void_pfn_smxprocess_t cleanup_func, smx_actor_t process,
             SwappedContextFactory* factory);

  void swap_into(SwappedContext* to) override;

private:
  /** pointer to top the stack stack */
  void* stack_top_ = nullptr;

#if HAVE_SANITIZER_ADDRESS_FIBER_SUPPORT
  const void* asan_stack_ = nullptr;
  size_t asan_stack_size_ = 0;
  RawContext* asan_ctx_   = nullptr;
  bool asan_stop_         = false;
#endif

  static void wrapper(void* arg);
};

class RawContextFactory : public SwappedContextFactory {
public:
  RawContextFactory() : SwappedContextFactory("RawContextFactory") {}

  Context* create_context(std::function<void()> code, void_pfn_smxprocess_t cleanup, smx_actor_t process) override;
};
}}} // namespace

#endif
