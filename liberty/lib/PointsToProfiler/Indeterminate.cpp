#include "llvm/IR/Function.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Intrinsics.h"
#include "liberty/Utilities/CallSiteFactory.h"

#include "liberty/SpecPriv/Indeterminate.h"

namespace liberty
{
namespace SpecPriv
{
using namespace llvm;

cl::opt<bool> SanityCheckMode(
  "spec-priv-profiler-sanity", cl::init(false), cl::NotHidden,
  cl::desc("Put profiler into sanity checking mode"));

static bool calls(const CallSite &cs, const char *fcnname)
{
  if( !cs.getInstruction() )
    return false;
  const Function *fcn = cs.getCalledFunction();
  if( !fcn )
    return false;
  return fcn->getName() == fcnname;
}


void Indeterminate::findIndeterminateObjects(BasicBlock &bb, UO &objects)
{
  UO pointers;
  findIndeterminateObjects(bb,pointers,objects);
}

// Map instructions to pointers
void Indeterminate::findIndeterminateObjects(BasicBlock &bb, UO &pointers, UO &objects)
{
  const DataLayout &DL = bb.getParent()->getParent()->getDataLayout();
  for(BasicBlock::iterator j=bb.begin(), z=bb.end(); j!=z; ++j)
  {
    Instruction *inst = &*j;

    if( LoadInst *load = dyn_cast< LoadInst >(inst) )
    {
      Value *ptr = load->getPointerOperand();
      findIndeterminateObjects(ptr, pointers, objects, DL);
      continue;
    }
    else if( StoreInst *store = dyn_cast< StoreInst >(inst) )
    {
      Value *ptr = store->getPointerOperand();
      findIndeterminateObjects(ptr, pointers, objects, DL);

      if( SanityCheckMode )
      {
        Value *stored_value = store->getValueOperand();
        if( stored_value->getType()->isPointerTy() )
          findIndeterminateObjects(stored_value, pointers, objects, DL);
      }

      continue;
    }
    else if( CmpInst *cmp = dyn_cast< CmpInst >(inst) )
    {
      if( cmp->getOperand(0)->getType()->isPointerTy() )
      {
        findIndeterminateObjects( cmp->getOperand(0), pointers, objects, DL );
        findIndeterminateObjects( cmp->getOperand(1), pointers, objects, DL );
      }

      continue;
    }
    else if( AtomicRMWInst *atomic = dyn_cast< AtomicRMWInst >(inst) )
    {
      Value *ptr = atomic->getPointerOperand();
      findIndeterminateObjects(ptr, pointers, objects, DL);
      continue;
    }

    CallSite cs = getCallSite(inst);
    if( cs.getInstruction() )
    {
      Function *callee = cs.getCalledFunction();
      if( !callee || callee->isDeclaration() )
      {
        for(CallSite::arg_iterator k=cs.arg_begin(), q=cs.arg_end(); k!=q; ++k)
        {
          Value *argument = *k;
          if( argument->getType()->isPointerTy() )
            findIndeterminateObjects(argument, pointers, objects, DL);
        }
      }
      continue;
    }

    assert( !inst->mayReadFromMemory() && !inst->mayWriteToMemory() );
  }
}

// Map pointers to objects
void Indeterminate::findIndeterminateObjects(Value *ptr, UO &pointers, UO &objects, const DataLayout &DL)
{
  pointers.insert(ptr);

  UO base_objects;
  GetUnderlyingObjects(ptr, base_objects, DL);

  // The result (objects) is either unique or not.

  if( base_objects.empty() )
    // No object found; the pointer itself is indeterminate
    objects.insert(ptr);

  /*
  else if( base_objects.size() > 1 )
    // Non-unique object; maybe only one manifests dynamically...
    objects.insert(ptr);

  else
    findIndeterminateObject( *base_objects.begin(), objects);
  */

  if( base_objects.size() > 1  )
    base_objects.insert(ptr);

  for(UO::iterator i=base_objects.begin(), e=base_objects.end(); i!=e; ++i)
  {
    const Value *ptr = *i;

    pointers.insert(ptr);
    findIndeterminateObject(ptr,objects);
  }
}

// Filter objects
void Indeterminate::findIndeterminateObject(const Value *v, UO &objects)
{
  Value *unique_object = const_cast<Value*>(v);

  if( isa< ConstantPointerNull >(unique_object) )
    return;
  if( isa< UndefValue >(unique_object) )
    return;
  if( isa< GlobalValue >(unique_object) )
    return;

  if( const IntrinsicInst *intrin = dyn_cast<IntrinsicInst>(v) )
    if( intrin->getIntrinsicID() == Intrinsic::invariant_start )
      return;

  // Is it a cast of an integer constant
  // to a pointer?  (clearly, this doesn't
  // give a valid memory address, but occurs
  // in several spec benchmarks.
  if( ConstantExpr *unary = dyn_cast< ConstantExpr >(unique_object) )
    if( unary->isCast() && isa< ConstantInt >( unary->getOperand(0) ) )
      return;

    /* screw it. context is worthwhile
  else if( isa< AllocaInst >(unique_object) )
    return;
  else if( isMalloc(unique_object) || isRealloc(unique_object) )
    return;
    */
  objects.insert(unique_object);
}

bool Indeterminate::isMallocOrCalloc(const Value *inst)
{
  CallSite cs = getCallSite(inst);
  return isMalloc(cs) || isCalloc(cs);
}

bool Indeterminate::isMallocOrCalloc(const CallSite &cs)
{
  return isMalloc(cs) || isCalloc(cs);
}

bool Indeterminate::isMalloc(const Value *inst)
{
  CallSite cs = getCallSite(inst);
  return isMalloc(cs);
}

bool Indeterminate::isCalloc(const Value *inst)
{
  CallSite cs = getCallSite(inst);
  return isCalloc(cs);
}

bool Indeterminate::isFopen(const Value *inst)
{
  CallSite cs = getCallSite(inst);
  return isFopen(cs);
}

bool Indeterminate::isFclose(const Value *inst)
{
  CallSite cs = getCallSite(inst);
  return isFclose(cs);
}

bool Indeterminate::isMalloc(const CallSite &cs)
{
  return calls(cs,"malloc")
  ||     calls(cs,"_Znwm") // C++ operator new
  ||     calls(cs,"_Znam"); // C++ operator new[]
}

bool Indeterminate::isCalloc(const CallSite &cs)
{
  return calls(cs,"calloc");
}

bool Indeterminate::isRealloc(const Value *inst)
{
  CallSite cs = getCallSite(inst);
  return isRealloc(cs);
}

bool Indeterminate::isRealloc(const CallSite &cs)
{
  return calls(cs,"realloc");
}

bool Indeterminate::isFree(const Value *inst)
{
  CallSite cs = getCallSite(inst);
  return isFree(cs);
}

bool Indeterminate::isFree(const CallSite &cs)
{
  return calls(cs,"free")
  ||     calls(cs,"_ZdlPv") // C++ operator delete
  ||     calls(cs,"_ZdaPv"); // C++ operator delete[];
}

bool Indeterminate::isFopen(const CallSite &cs)
{
  return calls(cs,"fopen");
}

bool Indeterminate::isFdopen(const CallSite &cs)
{
  return calls(cs,"fdopen");
}

bool Indeterminate::isFreopen(const CallSite &cs)
{
  return calls(cs,"freopen");
}

bool Indeterminate::isPopen(const CallSite &cs)
{
  return calls(cs,"popen");
}

bool Indeterminate::isTmpfile(const CallSite &cs)
{
  return calls(cs,"tmpfile");
}

bool Indeterminate::isOpendir(const CallSite &cs)
{
  return calls(cs,"opendir");
}

bool Indeterminate::returnsNewFilePointer(const CallSite &cs)
{
  return isFopen(cs) || isFdopen(cs) || isFreopen(cs) || isPopen(cs) || isTmpfile(cs) || isOpendir(cs);
}

bool Indeterminate::isFclose(const CallSite &cs)
{
  return calls(cs,"fclose");
}

bool Indeterminate::isClosedir(const CallSite &cs)
{
  return calls(cs,"closedir");
}

bool Indeterminate::returnsLibraryConstantString(const CallSite &cs)
{
  return calls(cs,"getenv")
  ||     calls(cs,"strerror");
}

bool Indeterminate::closesFilePointer(const CallSite &cs)
{
  return isFclose(cs) || isClosedir(cs);
}

}
}
