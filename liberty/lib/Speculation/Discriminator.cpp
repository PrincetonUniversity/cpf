#define DEBUG_TYPE "discriminator"

#include "llvm/ADT/Statistic.h"
#include "llvm/Transforms/Utils/Cloning.h"

#include "liberty/Speculation/Discriminator.h"

namespace liberty
{
namespace SpecPriv
{

STATISTIC(numRounds,      "Rounds of discriminator-cloning");
STATISTIC(numGroupClones, "Discriminator group clones");

Discriminator::Discriminator(const HeapAssignment &a)
  :asgn(a)
{
  recompute();
}

void Discriminator::recompute()
{
  static2heap.clear();
  addAUs( HeapAssignment::Shared,   asgn.getSharedAUs() );
  addAUs( HeapAssignment::Local,    asgn.getLocalAUs() );
  addAUs( HeapAssignment::Private,  asgn.getPrivateAUs() );
  addAUs( HeapAssignment::ReadOnly, asgn.getReadOnlyAUs() );
  addAUs( HeapAssignment::Redux,    asgn.getReductionAUs() );
}

unsigned Discriminator::determineShortestSuffix(const HeapGivenContext &heapGivenCtx) const
{
  // Try short suffices first, then try longer ones
  bool bottomedOut = false;
  for(unsigned suffix=1; !bottomedOut; ++suffix)
  {
    bool set_distinguishable = true;

    // Compare every heap|ctx to every other heap|ctx.
    // O(n**2) comparisons
    for(HeapGivenContext::const_iterator i=heapGivenCtx.begin(), e=heapGivenCtx.end(); i!=e && set_distinguishable; ++i)
    {
      const HeapSpec &hsi = i->first;
      const Ctx *ctxi = i->second;

      for(HeapGivenContext::const_iterator j=heapGivenCtx.upper_bound(hsi); j!=e && set_distinguishable; ++j)
      {
        const HeapSpec &hsj = j->first;
        const Ctx *ctxj = j->second;

        DEBUG(
        errs() << "  The two heap|ctx:\n"
               << "    " << (hsi.first) << '.' << (hsi.second) << " | " << *ctxi << '\n'
               << "    " << (hsj.first) << '.' << (hsj.second) << " | " << *ctxj << '\n');

        // Compare suffices.
        bool pair_distinguishable = false;
        const Ctx *ci = ctxi, *cj = ctxj;
        for(unsigned k=0; k<suffix; ++k, ci=ci->parent, cj=cj->parent)
        {
          if( ci == 0 || cj == 0 )
          {
            DEBUG(errs() << "  Suffix longer than one/both context!\n");
            bottomedOut = true;
            break;
          }

          if( ! ci->step_equal(cj) )
          {
            pair_distinguishable = true;
            break;
          }
        }

        DEBUG(
        errs() << "    "  << (pair_distinguishable ? "Can" : "CANNOT")
               << " be distinguished with a ctx-suffix of length " << suffix << "\n\n");

        set_distinguishable &= pair_distinguishable;
      }
    }

    if( set_distinguishable  )
      return suffix;
  }

  assert(false && "The assignments cannot be determined using a context suffix");
}

void Discriminator::addAUs(HeapAssignment::Type heap, const HeapAssignment::AUSet &aus)
{
  assert( heap != HeapAssignment::Redux  );

  for(HeapAssignment::AUSet::const_iterator i=aus.begin(), e=aus.end(); i!=e; ++i)
  {
    AU *au = *i;

    HeapSpec heapspec(heap, Reduction::NotReduction);

    static2heap[ au->value ].insert( HeapGivenContext::value_type(heapspec, au->ctx) );
  }
}

void Discriminator::addAUs(HeapAssignment::Type heap, const HeapAssignment::ReduxAUSet &aus)
{
  assert( heap == HeapAssignment::Redux  );

  for(HeapAssignment::ReduxAUSet::const_iterator i=aus.begin(), e=aus.end(); i!=e; ++i)
  {
    AU *au = i->first;
    Reduction::Type rty = i->second;


    HeapSpec heapspec(heap, rty);

    static2heap[ au->value ].insert( HeapGivenContext::value_type(heapspec,au->ctx) );
  }
}

static const Discriminator::HeapGivenContext empty;
const Discriminator::HeapGivenContext &Discriminator::classify(const Value *ptr) const
{
  Value2HeapGivenContext::const_iterator i = static2heap.find(ptr);
  if( i == static2heap.end() )
    return empty;

  return i->second;
}

void Discriminator::resolveGroupAmbiguity(unsigned sfx, const group_iterator &begin, const group_iterator &end, UpdateOnClone &changes, FoldManager &fmgr)
{
  const HeapSpec &hs = begin->first;
  DEBUG(errs() << "  - Group" << hs.first << "." << hs.second << '\n');

  // Clone the tail for this group.
  ValueToValueMapTy vmap;
  const Function *enterTail = 0;
  const Ctx *cc = begin->second;
  for(unsigned i=0; i<sfx-1; ++i, cc=cc->parent)
  {
    const Function *fcn = cc->getFcn();
    if( vmap.count(fcn) )
      continue;

    //Function *clone = CloneFunction(fcn, vmap, false, 0);
    //sot: explicit conversion of const to non-const
    Function *clone = CloneFunction((Function*) fcn, vmap);
    Twine newName = Twine(fcn->getName()) +
        "_group" + Twine( (unsigned) hs.first) +
        "_" + Twine( (unsigned) hs.second);
    clone->setName( newName );
    clone->setLinkage( GlobalValue::InternalLinkage );

    // sot: CloneFunction in LLVM 5.0 inserts the cloned function in the function's module
    //Module *mod = const_cast< Module* >( fcn->getParent() );
    //mod->getFunctionList().push_back(clone);

    DEBUG(errs() << "  . - Clone tail " << fcn->getName() << " => " << clone->getName() << '\n');

    vmap[fcn] = clone;
    enterTail = fcn;
  }

  for(group_iterator i=begin; i!=end; ++i)
  {
    const Ctx *ctx = i->second;
    for(unsigned q=0; q<sfx-1; ++q)
      ctx = ctx->parent;

    DEBUG(
    errs() << "  . - Discriminator ";
    ctx->print_step( errs() );
    errs() << '\n';
    );

    // Replace calls from the discriminator into the tail.
    // This would be a lot easier if contexts included callsites...

    // TODO: this is wrong, but approximately right...
    // Replace all uses of 'enterFunction' within the discriminator context.
    const Function *discriminator_fcn = ctx->getFcn();
    std::vector<Value*> users;
    for(Value::const_user_iterator j=enterTail->user_begin(), e=enterTail->user_end(); j!=e; ++j)
    {
      const Value *cu = *j;
      Value *u = const_cast<Value*>( cu ); // fuck it.
      users.push_back( u );
    }

    for(std::vector<Value*>::const_iterator j=users.begin(), e=users.end(); j!=e; ++j)
      if( Instruction *user = dyn_cast< Instruction >( *j ) )
        if( user->getParent()->getParent() == discriminator_fcn )
        {
          DEBUG(
          errs() << "  . . - Replacing:\n"
                 << "        " << *user << '\n');
          for(unsigned k=0; k<user->getNumOperands(); ++k)
            if( enterTail == user->getOperand(k) )
              user->setOperand(k, &*vmap[ enterTail ] );

          DEBUG(
          errs() << "        " << *user << '\n');
        }
  }

  // Now do all sorts of magic to update all other data in the program ;)
  DEBUG(errs() << "  . - Updating intermediate analyses...\n");
  CtxToCtxMap cmap;
  AuToAuMap amap;
  for(group_iterator i=begin; i!=end; ++i)
  {
    const Ctx *ctx = i->second;
    for(unsigned q=0; q<sfx-1; ++q)
    {
      fmgr.cloneContext(ctx, vmap, cmap, amap);
      changes.contextRenamedViaClone(ctx, vmap, cmap, amap);
    }
  }
  DEBUG(errs() << "  . - done.\n");
}


bool Discriminator::resolveOneAmbiguityViaCloning(UpdateOnClone &changes, FoldManager &fmgr)
{
  for(iterator i=static2heap.begin(), e=static2heap.end(); i!=e; ++i)
  {
    const HeapGivenContext &heapGivenCtx = i->second;

    // Is this static AU only assigned to ONE distinct heap?
    const HeapSpec &firstHeap = heapGivenCtx.begin()->first;
    // (there might be many entries with the same heap but
    //  different contexts; that's why we have this upper_bound thing...)
    if( heapGivenCtx.upper_bound(firstHeap) == heapGivenCtx.end() )
      continue;

    // for AUs of type AU_IO, AU_Unknown or AU_Null the au->value could be null.
    // avoid handling these ones
    if (!i->first)
      continue;

    // This static object has two different assignments.
    // Can we distinguish them via calling context?

    const unsigned sfx = determineShortestSuffix(heapGivenCtx);
    assert( sfx > 1 );

    // Yes, we can distinguish them.

    // Identify the discriminator contexts
    DEBUG(errs() << "Discriminator groups:\n");
    unsigned groupNumber = 0;
    for(group_iterator j=heapGivenCtx.begin(), z=heapGivenCtx.end(); j!=z; ++groupNumber)
    {
      const HeapSpec &hs = j->first;
      group_iterator end = heapGivenCtx.upper_bound( hs );

      if( groupNumber == 0 )
        DEBUG(errs() << "  - (skipping first group " << hs.first << '.' << hs.second << ")\n");
      else
      {
        resolveGroupAmbiguity(sfx, j, end, changes, fmgr);
        ++numGroupClones;
      }

      j = end;
    }

    return true;
  }

  return false;
}

bool Discriminator::resolveAmbiguitiesViaCloning(UpdateOnClone &changes, FoldManager &fmgr)
{
  bool modified = false;
  for(;;)
  {
    if( !resolveOneAmbiguityViaCloning(changes,fmgr) )
      break;

    modified = true;
    ++numRounds;
    recompute();
  }

  assert( asgn.isSimpleCase() && "That should have guaranteed a simple case!");
  errs() << "\n\n\nAll context sensitivies have been removed from AU assignments :)\n\n\n";

  return modified;
}


}
}
