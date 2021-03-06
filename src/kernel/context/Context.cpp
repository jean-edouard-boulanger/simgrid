/* Copyright (c) 2007-2019. The SimGrid Team. All rights reserved.          */

/* This program is free software; you can redistribute it and/or modify it
 * under the terms of the license (GNU LGPL) which comes with this package. */

#include "mc/mc.h"

#include "src/kernel/context/Context.hpp"
#include "src/simix/smx_private.hpp"

XBT_LOG_EXTERNAL_DEFAULT_CATEGORY(simix_context);

/**
 * @brief creates a new context for a user level process
 * @param code a main function
 * @param cleanup_func the function to call when the context stops
 * @param simix_process
 */
smx_context_t SIMIX_context_new(
  std::function<void()> code,
  void_pfn_smxprocess_t cleanup_func,
  smx_actor_t simix_process)
{
  xbt_assert(simix_global, "simix is not initialized, please call MSG_init first");
  return simix_global->context_factory->create_context(
    std::move(code), cleanup_func, simix_process);
}

namespace simgrid {
namespace kernel {
namespace context {

ContextFactoryInitializer factory_initializer = nullptr;

ContextFactory::~ContextFactory() = default;

static thread_local smx_context_t smx_current_context = nullptr;
Context* Context::self()
{
  return smx_current_context;
}
void Context::set_current(Context* self)
{
  smx_current_context = self;
}

void Context::declare_context(std::size_t size)
{
#if SIMGRID_HAVE_MC
  /* Store the address of the stack in heap to compare it apart of heap comparison */
  if(MC_is_active())
    MC_ignore_heap(this, size);
#endif
}

Context* ContextFactory::attach(void_pfn_smxprocess_t cleanup_func, smx_actor_t process)
{
  xbt_die("Cannot attach with this ContextFactory.\n"
    "Try using --cfg=contexts/factory:thread instead.\n");
}

Context* ContextFactory::create_maestro(std::function<void()> code, smx_actor_t process)
{
  xbt_die("Cannot create_maestro with this ContextFactory.\n"
    "Try using --cfg=contexts/factory:thread instead.\n");
}

Context::Context(std::function<void()> code, void_pfn_smxprocess_t cleanup_func, smx_actor_t actor)
    : code_(std::move(code)), actor_(actor)
{
  /* If the user provided a function for the process then use it.
     Otherwise, it is the context for maestro and we should set it as the
     current context */
  if (has_code())
    this->cleanup_func_ = cleanup_func;
  else
    set_current(this);
}

Context::~Context()
{
  if (self() == this)
    set_current(nullptr);
}

void Context::stop()
{
  if (this->cleanup_func_)
    this->cleanup_func_(this->actor_);
  this->actor_->suspended_ = 0;

  this->iwannadie = false;
  simgrid::simix::simcall([this] { SIMIX_process_cleanup(this->actor_); });
  this->iwannadie = true;
}

AttachContext::~AttachContext() = default;

}}}

/** @brief Executes all the processes to run (in parallel if possible). */
void SIMIX_context_runall()
{
  simix_global->context_factory->run_all();
}
