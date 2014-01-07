/** Basic galois contention manager base classes -*- C++ -*-
 * @file
 * @section License
 *
 * Galois, a framework to exploit amorphous data-parallelism in irregular
 * programs.
 *
 * Copyright (C) 2014, The University of Texas at Austin. All rights reserved.
 * UNIVERSITY EXPRESSLY DISCLAIMS ANY AND ALL WARRANTIES CONCERNING THIS
 * SOFTWARE AND DOCUMENTATION, INCLUDING ANY WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR ANY PARTICULAR PURPOSE, NON-INFRINGEMENT AND WARRANTIES OF
 * PERFORMANCE, AND ANY WARRANTY THAT MIGHT OTHERWISE ARISE FROM COURSE OF
 * DEALING OR USAGE OF TRADE.  NO WARRANTY IS EITHER EXPRESS OR IMPLIED WITH
 * RESPECT TO THE USE OF THE SOFTWARE OR DOCUMENTATION. Under no circumstances
 * shall University be liable for incidental, special, indirect, direct or
 * consequential damages or loss of profits, interruption of business, or
 * related expenses which may arise from use of Software or Documentation,
 * including but not limited to those resulting from defects in Software and/or
 * Documentation, or loss or inaccuracy of data of any kind.
 *
 * @section Description
 *
 * @author Andrew Lenharth <andrewl@lenharth.org>
 */
#ifndef GALOIS_RUNTIME_LOCKABLE_H
#define GALOIS_RUNTIME_LOCKABLE_H

#include "Galois/Runtime/ll/PtrLock.h"
#include "Galois/gdeque.h"

#include <boost/utility.hpp>

#include <ostream>

namespace Galois {
namespace Runtime {

class LockManagerBase;

/**
 * All objects that may be locked (nodes primarily) must inherit from
 * Lockable. 
 */
class Lockable {
  LL::PtrLock<LockManagerBase, true> owner;
  friend class LockManagerBase;
};

/**
 * Basic manager of lockable objects.  Holds and release lockable things
 */

class LockManagerBase: private boost::noncopyable {
  gdeque<Lockable*> locks;

public:
  enum AcquireStatus {
    FAIL=0, NEW_OWNER=1, ALREADY_OWNER=3
  };

  //Try to acquire an object
  AcquireStatus tryAcquire(Lockable* lockable);

  //Steals an object.  The old owner most only use a checked release
  //to release the object, a normal release will disrupt meta-data
  LockManagerBase* forceAcquire(Lockable* lockable);

  //Release all objects.  Returns number of objects
  unsigned  releaseAll();

  //Release all objects which haven't been stolen.  Returns number of objects
  unsigned releaseAllChecked();

  //dump objects and owners
  void dump(std::ostream& os);

};

}
} // end namespace Galois

#endif
