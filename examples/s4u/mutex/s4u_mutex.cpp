/* Copyright (c) 2006-2015. The SimGrid Team. All rights reserved.          */

/* This program is free software; you can redistribute it and/or modify it
 * under the terms of the license (GNU LGPL) which comes with this package. */

#include <xbt/sysdep.h>
#include <mutex>

#include "simgrid/s4u.h"

#define NB_ACTOR 2

XBT_LOG_NEW_DEFAULT_CATEGORY(s4u_test, "a sample log category");

// simgrid::s4u::Mutex mtx; //FIXME generate error -> You must run MSG_init before using MSG

//Create an actor as a c++ functor
class Worker {
  simgrid::s4u::Mutex mutex_;
  int *results_;
public:
  Worker(int  *res, simgrid::s4u::Mutex mutex) :
    mutex_(std::move(mutex)),  results_(res) {};
  // Define the code of the actor
  void operator()() {
    // Do the calculation
    simgrid::s4u::this_actor::execute(1000);

    // lock the mutex before enter in the critical section
    std::lock_guard<simgrid::s4u::Mutex> lock(mutex_);
    XBT_INFO("Hello s4u, I'm ready to compute");

    // And finaly add it to the results
    *results_ += 1;
    XBT_INFO("I'm done, good bye");
  }
};

// This class is an example of how to use lock_guard with simgrid mutex
class WorkerLockGuard {
  simgrid::s4u::Mutex mutex_;
  int *results_;
public:
  WorkerLockGuard(int  *res, simgrid::s4u::Mutex mutex) :
    mutex_(std::move(mutex)),  results_(res) {};
  void operator()() {

    simgrid::s4u::this_actor::execute(1000);

    // Simply use the std::lock_guard like this
    std::lock_guard<simgrid::s4u::Mutex> lock(mutex_);

    // then you are in a safe zone
    XBT_INFO("Hello s4u, I'm ready to compute");
    // update the results
    *results_ += 1;
    XBT_INFO("I'm done, good bye");
  }
};

class MainActor {
public:
  void operator()() {
    int res = 0;
    simgrid::s4u::Mutex mutex;
    simgrid::s4u::Actor workers[NB_ACTOR*2];

    for (int i = 0; i < NB_ACTOR * 2 ; i++) {
      // To create a worker use the static method simgrid::s4u::Actor.
      if((i % 2) == 0 )
        workers[i] = simgrid::s4u::Actor("worker",
          simgrid::s4u::Host::by_name("Jupiter"),
          WorkerLockGuard(&res, mutex));
      else
        workers[i] = simgrid::s4u::Actor("worker",
          simgrid::s4u::Host::by_name("Tremblay"),
          Worker(&res, mutex));
    }

    simgrid::s4u::this_actor::sleep(10);
    XBT_INFO("Results is -> %d", res);
  }
};


int main(int argc, char **argv) {
  simgrid::s4u::Engine *e = new simgrid::s4u::Engine(&argc,argv);
  e->loadPlatform("../../platforms/two_hosts.xml");
  simgrid::s4u::Actor("main", simgrid::s4u::Host::by_name("Tremblay"), 0, MainActor());
  e->run();
  return 0;
}