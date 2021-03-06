/* Copyright (c) 2009-2019. The SimGrid Team. All rights reserved.          */

/* This program is free software; you can redistribute it and/or modify it
 * under the terms of the license (GNU LGPL) which comes with this package. */

#include "simgrid/modelchecker.h"
#include "src/internal_config.h"
#include "src/kernel/context/context_private.hpp"
#include "src/simix/ActorImpl.hpp"
#include "src/simix/smx_private.hpp"
#include "xbt/parmap.hpp"

#include "src/kernel/context/ContextSwapped.hpp"

#ifdef _WIN32
#include <malloc.h>
#include <windows.h>
#else
#include <sys/mman.h>
#endif

#ifdef __MINGW32__
#define _aligned_malloc __mingw_aligned_malloc
#define _aligned_free __mingw_aligned_free
#endif /*MINGW*/

#if HAVE_VALGRIND_H
#include <valgrind/valgrind.h>
#endif

XBT_LOG_EXTERNAL_DEFAULT_CATEGORY(simix_context);

namespace simgrid {
namespace kernel {
namespace context {

/* rank of the execution thread */
thread_local uintptr_t SwappedContext::worker_id_;             /* thread-specific storage for the thread id */

SwappedContextFactory::SwappedContextFactory(std::string name)
    : ContextFactory(name), parallel_(SIMIX_context_is_parallel())
{
  parmap_ = nullptr; // will be created lazily with the right parameters if needed (ie, in parallel)
  workers_context_.clear();
  workers_context_.resize(parallel_ ? SIMIX_context_get_nthreads() : 1, nullptr);
}
SwappedContextFactory::~SwappedContextFactory()
{
  delete parmap_;
  parmap_ = nullptr;
  workers_context_.clear();
}

SwappedContext::SwappedContext(std::function<void()> code, void_pfn_smxprocess_t cleanup_func, smx_actor_t process,
                               SwappedContextFactory* factory)
    : Context(std::move(code), cleanup_func, process), factory_(factory)
{
  if (has_code()) {
    if (smx_context_guard_size > 0 && not MC_is_active()) {

#if !defined(PTH_STACKGROWTH) || (PTH_STACKGROWTH != -1)
      xbt_die(
          "Stack overflow protection is known to be broken on your system: you stacks grow upwards (or detection is "
          "broken). "
          "Please disable stack guards with --cfg=contexts:guard-size:0");
      /* Current code for stack overflow protection assumes that stacks are growing downward (PTH_STACKGROWTH == -1).
       * Protected pages need to be put after the stack when PTH_STACKGROWTH == 1. */
#endif

      size_t size = smx_context_stack_size + smx_context_guard_size;
#if SIMGRID_HAVE_MC
      /* Cannot use posix_memalign when SIMGRID_HAVE_MC. Align stack by hand, and save the
       * pointer returned by xbt_malloc0. */
      char* alloc           = (char*)xbt_malloc0(size + xbt_pagesize);
      stack_                = alloc - ((uintptr_t)alloc & (xbt_pagesize - 1)) + xbt_pagesize;
      *((void**)stack_ - 1) = alloc;
#elif !defined(_WIN32)
      if (posix_memalign(&this->stack_, xbt_pagesize, size) != 0)
        xbt_die("Failed to allocate stack.");
#else
      this->stack_ = _aligned_malloc(size, xbt_pagesize);
#endif

#ifndef _WIN32
      if (mprotect(this->stack_, smx_context_guard_size, PROT_NONE) == -1) {
        xbt_die(
            "Failed to protect stack: %s.\n"
            "If you are running a lot of actors, you may be exceeding the amount of mappings allowed per process.\n"
            "On Linux systems, change this value with sudo sysctl -w vm.max_map_count=newvalue (default value: 65536)\n"
            "Please see http://simgrid.gforge.inria.fr/simgrid/latest/doc/html/options.html#options_virt for more "
            "info.",
            strerror(errno));
        /* This is fatal. We are going to fail at some point when we try reusing this. */
      }
#endif
      this->stack_ = (char*)this->stack_ + smx_context_guard_size;
    } else {
      this->stack_ = xbt_malloc0(smx_context_stack_size);
    }

#if HAVE_VALGRIND_H
    unsigned int valgrind_stack_id =
        VALGRIND_STACK_REGISTER(this->stack_, (char*)this->stack_ + smx_context_stack_size);
    memcpy((char*)this->stack_ + smx_context_usable_stack_size, &valgrind_stack_id, sizeof valgrind_stack_id);
#endif
  }
}

SwappedContext::~SwappedContext()
{
  if (stack_ == nullptr) // maestro has no extra stack
    return;

#if HAVE_VALGRIND_H
  unsigned int valgrind_stack_id;
  memcpy(&valgrind_stack_id, (char*)stack_ + smx_context_usable_stack_size, sizeof valgrind_stack_id);
  VALGRIND_STACK_DEREGISTER(valgrind_stack_id);
#endif

#ifndef _WIN32
  if (smx_context_guard_size > 0 && not MC_is_active()) {
    stack_ = (char*)stack_ - smx_context_guard_size;
    if (mprotect(stack_, smx_context_guard_size, PROT_READ | PROT_WRITE) == -1) {
      XBT_WARN("Failed to remove page protection: %s", strerror(errno));
      /* try to pursue anyway */
    }
#if SIMGRID_HAVE_MC
    /* Retrieve the saved pointer.  See SIMIX_context_stack_new above. */
    stack_ = *((void**)stack_ - 1);
#endif
  }
#endif /* not windows */

  xbt_free(stack_);
}

void SwappedContext::set_maestro(SwappedContext* ctx)
{
  if (factory_->threads_working_ == 0) // Don't save the soul of minions, only the one of maestro
    factory_->workers_context_[0] = ctx;
}

void* SwappedContext::get_stack()
{
  return stack_;
}

void SwappedContext::stop()
{
  Context::stop();
  /* We must cut the actor execution using an exception to properly free the C++ RAII variables */
  throw StopRequest();
}

/** Maestro wants to run all ready actors */
void SwappedContextFactory::run_all()
{
  /* This function is called by maestro at the beginning of a scheduling round to get all working threads executing some
   * stuff It is much easier to understand what happens if you see the working threads as bodies that swap their soul
   * for the ones of the simulated processes that must run.
   */
  if (parallel_) {
    threads_working_ = 0;

    // We lazily create the parmap so that all options are actually processed when doing so.
    if (parmap_ == nullptr)
      parmap_ = new simgrid::xbt::Parmap<smx_actor_t>(SIMIX_context_get_nthreads(), SIMIX_context_get_parallel_mode());

    // Usually, Parmap::apply() executes the provided function on all elements of the array.
    // Here, the executed function does not return the control to the parmap before all the array is processed:
    //   - suspend() should switch back to the worker_context (either maestro or one of its minions) to return
    //     the control to the parmap. Instead, it uses parmap_->next() to steal another work, and does it directly.
    //     It only yields back to worker_context when the work array is exhausted.
    //   - So, resume() is only launched from the parmap for the first job of each minion.
    parmap_->apply(
        [](smx_actor_t process) {
          SwappedContext* context = static_cast<SwappedContext*>(process->context_);
          context->resume();
        },
        simix_global->process_to_run);
  } else { // sequential execution
    if (simix_global->process_to_run.empty())
      return;

    /* maestro is already saved in the first slot of workers_context_ */
    smx_actor_t first_actor = simix_global->process_to_run.front();
    process_index_          = 1;
    /* execute the first actor; it will chain to the others when using suspend() */
    static_cast<SwappedContext*>(first_actor->context_)->resume();
  }
}

/** Maestro wants to yield back to a given actor, so awake it on the current thread
 *
 * In parallel, it is only applied to the N first elements of the parmap array,
 * where N is the amount of worker threads in the parmap.
 * See SwappedContextFactory::run_all for details.
 */
void SwappedContext::resume()
{
  if (factory_->parallel_) {
    // Save the thread number (my body) in an os-thread-specific area
    worker_id_ = factory_->threads_working_.fetch_add(1, std::memory_order_relaxed);
    // Save my current soul (either maestro, or one of the minions) in a permanent area
    SwappedContext* worker_context = static_cast<SwappedContext*>(self());
    factory_->workers_context_[worker_id_] = worker_context;
    // Switch my soul and the actor's one
    Context::set_current(this);
    worker_context->swap_into(this);
    // No body runs that soul anymore at this point, but it is stored in a safe place.
    // When the executed actor will do a blocking action, SIMIX_process_yield() will call suspend(), below.
  } else { // sequential execution
    SwappedContext* old = static_cast<SwappedContext*>(self());
    Context::set_current(this);
    old->swap_into(this);
  }
}

/** The actor wants to yield back to maestro, because it is blocked in a simcall (ie in SIMIX_process_yield())
 *
 * Actually, it does not really yield back to maestro, but directly into the next executable actor.
 *
 * This makes the parmap::apply awkward (see ParallelUContext::run_all()) because it only apply regularly
 * on the few first elements of the array, but it saves a lot of context switches back to maestro,
 * and directly forth to the next executable actor.
 */
void SwappedContext::suspend()
{
  if (factory_->parallel_) {
    // Get some more work to directly swap into the next executable actor instead of yielding back to the parmap
    boost::optional<smx_actor_t> next_work = factory_->parmap_->next();
    SwappedContext* next_context;
    if (next_work) {
      // There is a next soul to embody (ie, another executable actor)
      XBT_DEBUG("Run next process");
      next_context = static_cast<SwappedContext*>(next_work.get()->context_);
    } else {
      // All actors were run, go back to the parmap context
      XBT_DEBUG("No more actors to run");
      // worker_id_ is the identity of my body, stored in thread_local when starting the scheduling round
      next_context = factory_->workers_context_[worker_id_];
      // When given that soul, the body will wait for the next scheduling round
    }

    // Get the next soul to run, either from another actor or the initial minion's one
    Context::set_current(next_context);
    this->swap_into(next_context);

  } else { // sequential execution
    /* determine the next context */
    SwappedContext* next_context;
    unsigned long int i = factory_->process_index_;
    factory_->process_index_++;

    if (i < simix_global->process_to_run.size()) {
      /* Actually swap into the next actor directly without transiting to maestro */
      XBT_DEBUG("Run next actor");
      next_context = static_cast<SwappedContext*>(simix_global->process_to_run[i]->context_);
    } else {
      /* all processes were run, actually return to maestro */
      XBT_DEBUG("No more actors to run");
      next_context = factory_->workers_context_[0];
    }
    Context::set_current(next_context);
    this->swap_into(next_context);
  }
}

} // namespace context
} // namespace kernel
} // namespace simgrid
