#define DEBUG_TYPE "std-in-out-err-aa"

#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/Analysis/Passes.h"
#include "llvm/Support/Debug.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/Support/raw_ostream.h"

#include "StdInOutErr.h"
#include "liberty/Analysis/Introspection.h"
#include "liberty/Utilities/FindUnderlyingObjects.h"

namespace liberty
{

using namespace llvm;


StdInOutErr::StdInOutErr() : ModulePass(ID) {
}

bool StdInOutErr::runOnModule(Module &M) {
  Mod = &M;
  const DataLayout &DL = M.getDataLayout();
  InitializeLoopAA(this, DL);
  return false;
}

static bool is(const Value *ptr, StringRef name)
{
  const GlobalVariable *gv = dyn_cast< GlobalVariable >( ptr );
  if( !gv )
    return false;

  if( gv->getName() != name )
    return false;

  return true;
}

static bool isL(const ClassicLoopAA::Pointer &P1, StringRef name,
                const DataLayout &DL)
{
  UO underlying;
  GetUnderlyingObjects(P1.ptr, underlying, DL);

  for(UO::const_iterator i=underlying.begin(), e=underlying.end(); i!=e; ++i)
  {
    const Value *object = *i;

    const LoadInst *load = dyn_cast< LoadInst >(object);
    if( !load )
      return false;


    if( !is(load->getPointerOperand(), name) )
      return false;
  }

  return true;
}

static bool diff(const ClassicLoopAA::Pointer &P1, const ClassicLoopAA::Pointer &P2, StringRef name, const DataLayout &DL)
{
  return isL(P1,name,DL) && !isL(P2,name,DL);
}

static bool different(const ClassicLoopAA::Pointer &P1, const ClassicLoopAA::Pointer &P2, StringRef name, const DataLayout &DL)
{
  return diff(P1,P2,name,DL) || diff(P2,P1,name,DL);
}

static bool definitelyDifferent(const ClassicLoopAA::Pointer &P1, const ClassicLoopAA::Pointer &P2, const DataLayout &DL)
{
  return
    different(P1,P2,"stdin",DL)
  ||different(P1,P2,"stdout",DL)
  ||different(P1,P2,"stderr",DL);
}

LoopAA::AliasResult StdInOutErr::aliasCheck(const Pointer &P1,
                                            TemporalRelation Rel,
                                            const Pointer &P2, const Loop *L,
                                            Remedies &R,
                                            DesiredAliasResult dAliasRes) {

  if (dAliasRes == DMustAlias)
    return MayAlias;

  //sot
  const DataLayout &DL = Mod->getDataLayout();

  INTROSPECT(ENTER(P1,Rel,P2,L));
  if( definitelyDifferent(P1,P2,DL) )
  {
    INTROSPECT(EXIT(P1,Rel,P2,L,NoAlias));
    LLVM_DEBUG(errs() << "StdInOutErr: alias(" << *P1.ptr << ", " << *P2.ptr << ")\n");
    return NoAlias;
  }

  INTROSPECT(EXIT(P1,Rel,P2,L));
  return MayAlias;
}

/// May not call down the LoopAA stack, but may top
LoopAA::ModRefResult StdInOutErr::getModRefInfo(
  CallSite CS1,
  TemporalRelation Rel,
  CallSite CS2,
  const Loop *L,
  Remedies &R)
{
  return ModRef;
}

/// May not call down the LoopAA stack, but may top
LoopAA::ModRefResult StdInOutErr::getModRefInfo(
  CallSite CS1,
  TemporalRelation Rel,
  const Pointer &P,
  const Loop *L,
  Remedies &R)
{
  //sot
  const DataLayout *DL;
  if (L) {
    Module *M = L->getHeader()->getParent()->getParent();
    DL = &M->getDataLayout();
  }

  INTROSPECT(ENTER(CS1,Rel,P,L));
  if( is(P.ptr,"stdin")
  ||  is(P.ptr,"stdout")
  ||  is(P.ptr,"stderr")
  || ( L && (
          isL(P,"stdin",*DL)
      ||  isL(P,"stdout",*DL)
      ||  isL(P,"stderr",*DL) ) ) )
  {
    LLVM_DEBUG(errs() << "StdInOutErr: getModRefInfo(" << *CS1.getInstruction() << ", " << *P.ptr << ")\n");
    INTROSPECT(EXIT(CS1,Rel,P,L,Ref));
    return Ref;
  }

  INTROSPECT(EXIT(CS1,Rel,P,L));
  return ModRef;
}

bool StdInOutErr::pointsToConstantMemory(const Value *v, const Loop *L)
{
  return is(v,"stdin")
  ||     is(v,"stdout")
  ||     is(v,"stderr");
}

void StdInOutErr::getAnalysisUsage(AnalysisUsage &AU) const {
  LoopAA::getAnalysisUsage(AU);
  AU.setPreservesAll();                         // Does not transform code
}

/// getAdjustedAnalysisPointer - This method is used when a pass implements an
/// analysis interface through multiple inheritance.  If needed, it should
/// override this to adjust the this pointer as needed for the specified pass
/// info.
void *StdInOutErr::getAdjustedAnalysisPointer(AnalysisID PI) {
  if (PI == &LoopAA::ID)
    return (LoopAA*)this;
  return this;
}

char StdInOutErr::ID = 0;

namespace {
  RegisterPass<StdInOutErr>
  X("std-in-out-err-aa", "Alias analysis of the globals stdin, stdout and stderr", false, true);
  RegisterAnalysisGroup<LoopAA> Y(X);
}

}
