#include "llvm/IR/Module.h"
#include "llvm/Analysis/ValueTracking.h"

#include "liberty/Utilities/FindUnderlyingObjects.h"

using namespace llvm;

typedef llvm::DenseSet<const llvm::PHINode *> PHISet;

static void findUnderlyingObjects(const Value *value,
                                  liberty::ObjectSet &values,
                                  PHISet &visited) {

  //sot: GetUnderlyingObject needs as the second argument the DataLayout
  // In order to get DataLayout need to get the Module. I need to get a grasp of a instruction
  // assert if the value ptr is an instruction. If it is not an instruction then assert and
  // TODO : see if I can relax this requirement
  const Instruction* inst = dyn_cast<Instruction>(value);
  assert(inst);
  const Module* mod = inst->getModule();
  const DataLayout &DL = mod->getDataLayout();

  const Value *object = GetUnderlyingObject(value, DL);
  if(const PHINode *phi = dyn_cast<PHINode>(object)) {

    if(!visited.insert(phi).second)
      return;

    for(unsigned i = 0; i < phi->getNumIncomingValues();  ++i)   {
      findUnderlyingObjects(phi->getIncomingValue(i), values, visited);
    }

    return;
  }

  if(const SelectInst *sel = dyn_cast<SelectInst>(object)) {
    findUnderlyingObjects(sel->getTrueValue(), values, visited);
    findUnderlyingObjects(sel->getFalseValue(), values, visited);
    return;
  }

  values.insert(object);
}

void liberty::findUnderlyingObjects(const Value *value, ObjectSet &values) {
  PHISet visited;
  ::findUnderlyingObjects(value, values, visited);
}

namespace liberty
{

static void GetUnderlyingObjects(
  const Value *ptr,
  UO &beforePHI, UO &visitedBefore,
  UO &afterPHI,  UO &visitedAfter,
  const DataLayout &DL,
  bool isAfterPHI)
{
  // Strip-away pointer casts, GEPs, etc, but stop at any operation
  // which has more than one UO.

  //sot: GetUnderlyingObject needs as the second argument the DataLayout

  const Value *obj = GetUnderlyingObject(ptr, DL, 0);

  // Detect cycles.
  if( isAfterPHI )
  {
    if( visitedAfter.count(obj) )
      return;
    visitedAfter.insert(obj);
  }
  else
  {
    if( visitedBefore.count(obj) )
      return;
    visitedBefore.insert(obj);
  }

  if( const PHINode *phi = dyn_cast< PHINode >(obj) )
  {
    // recur on PHI operands.
    for(unsigned i=0, e=phi->getNumIncomingValues(); i!=e; ++i)
    {
      const Value *v = phi->getIncomingValue(i);
      GetUnderlyingObjects(v, beforePHI, visitedBefore, afterPHI, visitedAfter, DL, true);
    }
  }

  else if( const SelectInst *sel = dyn_cast< SelectInst >(obj) )
  {
    // Recur on select operands.
    GetUnderlyingObjects( sel->getTrueValue(),  beforePHI, visitedBefore, afterPHI, visitedAfter, DL, isAfterPHI);
    GetUnderlyingObjects( sel->getFalseValue(), beforePHI, visitedBefore, afterPHI, visitedAfter, DL, isAfterPHI);
  }

  else
  {
    // Found the underlying object.
    if( isAfterPHI )
      afterPHI.insert(obj);
    else
      beforePHI.insert(obj);
  }
}

void GetUnderlyingObjects(const Value *ptr, UO &beforePHI, UO &afterPHI, bool isAfterPHI, const DataLayout &DL)
{
  UO visitedBefore, visitedAfter;
  GetUnderlyingObjects(ptr, beforePHI, visitedBefore, afterPHI, visitedAfter, DL, isAfterPHI);
}

void GetUnderlyingObjects(const Value *ptr, UO &uo, const DataLayout &DL)
{
  UO visited;
  GetUnderlyingObjects(ptr, uo, visited, uo, visited, DL, false);
}

}



