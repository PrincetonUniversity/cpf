#define DEBUG_TYPE "nocapturefcn"

#include "liberty/Analysis/NoCaptureFcn.h"

#include "llvm/IR/Module.h"
#include "llvm/IR/Operator.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"

#include "scaf/Utilities/FindUnderlyingObjects.h"
#include "scaf/Utilities/CallSiteFactory.h"

namespace liberty
{
using namespace llvm;

void NoCaptureFcn::getAnalysisUsage(AnalysisUsage &au) const
{
  au.setPreservesAll();
}

bool NoCaptureFcn::runOnModule(Module &mod)
{
  // Identify all functions which can be called indirectly.
  for(Module::iterator i=mod.begin(), e=mod.end(); i!=e; ++i)
  {
    Function *fcn = &*i;
    if( functionAddressMayBeCaptured(fcn) )
      setCaptured(fcn);
  }

  LLVM_DEBUG(
    errs() << "Captured functions:\n";
    unsigned total = 0;
    for(MinArity2FcnList::iterator i=captured.begin(), e=captured.end(); i!=e; ++i)
    {
      const unsigned n = i->second.size();
      total += n;
      errs() << " o min-arity " << i->first << " : " << n << '\n';
    }
    errs() << "Total " << total << '\n';
  );
  return false;
}

bool NoCaptureFcn::isCaptured(const Function *fcn) const
{
  const unsigned min_arity = fcn->getFunctionType()->getNumParams();
  MinArity2FcnList::const_iterator i = captured.find(min_arity);
  if( i == captured.end() )
    return false;

  const FcnList &list = i->second;
  return std::binary_search(list.begin(), list.end(), fcn);
}

void NoCaptureFcn::setCaptured(Function *fcn)
{
  const unsigned min_arity = fcn->getFunctionType()->getNumParams();
  FcnList &list = captured[ min_arity ];

  FcnList::iterator i = std::lower_bound(list.begin(),list.end(), fcn);
  if( i == list.end() || fcn != *i )
    list.insert(i, fcn);
}

bool NoCaptureFcn::functionAddressMayBeCaptured(Function *fcn) const
{
  if( fcn->isIntrinsic() )
    return false;

  if( ! fcn->hasInternalLinkage() )
    return true;

  // Consider if a pointer to this function ever escapes...
  if( ! onlyUsedAsCallTarget(fcn) )
    return true;

  return false;
}

bool NoCaptureFcn::onlyUsedAsCallTarget(Value *v) const
{
  ValueSet already;
  return onlyUsedAsCallTarget(v,already);
}

bool NoCaptureFcn::onlyUsedAsCallTarget(Value *v, ValueSet &already) const
{
  if( already.count(v) )
    return true;
  already.insert(v);

  for(Value::user_iterator i=v->user_begin(), e=v->user_end(); i!=e; ++i)
  {
    User *user = *i;

    if( ConstantExpr *cexpr = dyn_cast< ConstantExpr >(user) )
      if( cexpr->isCast() )
      {
        if( ! onlyUsedAsCallTarget(cexpr, already) )
          return false;
        continue;
      }

    CallSite cs = getCallSite(user);
    if( !cs.getInstruction() )
      return false; // used by something other than call, invoke

    // Ensure that 'fcn' is not used by this callsite as a
    // parameter.
    for(CallSite::arg_iterator j=cs.arg_begin(), z=cs.arg_end(); j!=z; ++j)
      if( v == *j )
        return false; // address of function escapes.

    // This use is a call to fcn
  }

  return true;
}

NoCaptureFcn::iterator NoCaptureFcn::begin(unsigned arity) const { return captured.begin(); }
NoCaptureFcn::iterator NoCaptureFcn::end(unsigned arity) const
{
  if( arity == ~0u )
    return captured.end();
  else
    return captured.upper_bound(arity);
}

char NoCaptureFcn::ID = 0;
static RegisterPass<NoCaptureFcn> mp(
  "no-capture-fcn", "Analyze functions whose address is never captured.");
}
