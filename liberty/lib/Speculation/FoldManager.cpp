#include "llvm/Support/raw_ostream.h"

#include "liberty/Speculation/FoldManager.h"

namespace liberty
{
namespace SpecPriv
{
using namespace llvm;


AU *FoldManager::fold(AU *a)
{
  AU *a0 = auManager.GetOrInsertNode(a);
  if( a0 == a )
  {
    // This is the first time we've seen this AU
    allAUs.push_back(a);
  }
  else
  {
    // already seen this AU
    delete a;
  }

  return a0;
}

Ctx *FoldManager::fold(Ctx *c)
{
  Ctx *c0 = ctxManager.GetOrInsertNode(c);
  if( c0 != c )
    delete c;

  return c0;
}

FoldManager::~FoldManager()
{
  // Delete the objects saved in the folding sets.
  // Since the objects themselves are the structure of the
  // folding set, we must extract them and then delete them,
  // but we cannot delete them while they're still in the
  // folding set :(

  std::vector<Ctx*> ctxs;
  for(FoldingSet<Ctx>::iterator i=ctxManager.begin(), e=ctxManager.end(); i!=e; ++i)
    ctxs.push_back(&*i);
  ctxManager.clear();
  while( !ctxs.empty() )
  {
    delete ctxs.back();
    ctxs.pop_back();
  }

  std::vector<AU*> aus;
  for(FoldingSet<AU>::iterator i=auManager.begin(), e=auManager.end(); i!=e; ++i)
    aus.push_back(&*i);
  auManager.clear();
  while( !aus.empty() )
  {
    delete aus.back();
    aus.pop_back();
  }
}

void FoldManager::cloneContext(
  const Ctx *oldCtx,
  const ValueToValueMapTy &vmap,
  CtxToCtxMap &cmap,
  AuToAuMap &amap)
{
  const ValueToValueMapTy::const_iterator vmap_end = vmap.end();

  Ctx *newCtx = 0;
  switch( oldCtx->type )
  {
  case Ctx_Fcn:
    {
      const ValueToValueMapTy::const_iterator i = vmap.find( oldCtx->fcn );
      assert( i != vmap_end );

      const Function *newFcn = cast< Function >( &*(i->second) );
      newCtx = new Ctx(Ctx_Fcn, oldCtx->parent);
      newCtx->fcn = newFcn;
    }
    break;
  case Ctx_Loop:
    {
      const ValueToValueMapTy::const_iterator i = vmap.find( oldCtx->header );
      assert( i != vmap_end );

      const BasicBlock *newHeader = cast< BasicBlock >( &*(i->second) );
      newCtx = new Ctx(Ctx_Loop, oldCtx->parent);
      newCtx->header = newHeader;
    }
    break;
  case Ctx_Top:
  default:
    return;
  }

  newCtx = fold( newCtx );
  cmap[ oldCtx ] = newCtx;

  // Update all contexts which used old context,
  // transitively.
  typedef std::vector<const Ctx *> Fringe;
  Fringe fringe; // holds changed contexts.
  fringe.push_back( oldCtx );
  while( !fringe.empty() )
  {
    const Ctx *someChangedCtx = fringe.back();
    fringe.pop_back();

    const Ctx *replacement = cmap[ someChangedCtx ];

    // Update the children of the changed context.

    // First, clone the list of affected contexts
    // since we are going to modify the collection
    Fringe children;
    for(CtxManager::iterator i=ctxManager.begin(), e=ctxManager.end(); i!=e; ++i)
    {
      const Ctx *cc = &*i;
      if( cc->parent == someChangedCtx )
        children.push_back(cc);
    }

    // Update each affected context.
    for(Fringe::const_iterator i=children.begin(), e=children.end(); i!=e; ++i)
    {
      const Ctx *child = *i;

      // clone child
      Ctx *clone = new Ctx();
      clone->type = child->type;
      clone->fcn = child->fcn;
      clone->header = child->header;
      clone->parent = replacement;
      clone = fold(clone);

      cmap[ child ] = clone;
      fringe.push_back( child );
    }
  }

  // Since cloneContext() is called repeatedly and the cmap,amap are built
  // progressively, it's possible that a ctx/au should change twice. e.g.
  //   a -> b and b -> c.
  // Flatten the cmap so that a -> c.
  for(;;)
  {
    bool changed = false;
    for(CtxToCtxMap::iterator i=cmap.begin(), e=cmap.end(); i!=e; ++i)
    {
      const Ctx *from = i->first, *to = i->second;
      if( cmap.count(to) )
      {
        cmap[from] = cmap[to];
        changed = true;
        break;
      }
    }

    if( !changed )
      break;
  }

/*
  errs() << "cmap:\n";
  for(CtxToCtxMap::iterator i=cmap.begin(), e=cmap.end(); i!=e; ++i)
  {
    const Ctx *from = i->first, *to = i->second;
    errs() << " from " << *from << '\n'
           << "   to " << *to << '\n';
  }
*/

  // Update all AUs which used any changed value or context.
  const CtxToCtxMap::const_iterator cmap_end = cmap.end();
  AUs aus( allAUs );
  for(AUs::const_iterator i=aus.begin(), e=aus.end(); i!=e; ++i)
  {
    AU *oldAU = *i;

    const CtxToCtxMap::const_iterator l = cmap.find( oldAU->ctx );

    if( l == cmap_end )
      continue;

    AU *newAU = new AU( oldAU->type );
    const ValueToValueMapTy::const_iterator k = vmap.find( oldAU->value );
    if( k == vmap_end )
      newAU->value = oldAU->value;
    else
      newAU->value = &* k->second;

    newAU->ctx = l->second;

    newAU = fold(newAU);
    amap[ oldAU ] = newAU;
  }

  // Since cloneContext() is called repeatedly and the cmap,amap are built
  // progressively, it's possible that a ctx/au should change twice. e.g.
  //   a -> b and b -> c.
  // Flatten the cmap so that a -> c.
  for(;;)
  {
    bool changed = false;
    for(AuToAuMap::iterator i=amap.begin(), e=amap.end(); i!=e; ++i)
    {
      const AU *from = i->first, *to = i->second;
      if( amap.count(to) )
      {
        amap[from] = amap[to];
        changed = true;
        break;
      }
    }

    if( !changed )
      break;
  }

/*
  errs() << "amap:\n";
  for(AuToAuMap::iterator i=amap.begin(), e=amap.end(); i!=e; ++i)
  {
    const AU *from = i->first, *to = i->second;
    errs() << " from " << *from << '\n'
           << "   to " << *to << '\n';
  }
*/
}


void FoldManager::inlineContext(
  const Ctx *oldCtx,
  const ValueToValueMapTy &vmap,
  CtxToCtxMap &cmap,
  AuToAuMap &amap)
{
  const ValueToValueMapTy::const_iterator vmap_end = vmap.end();

  const Ctx *newCtx = oldCtx->parent;
  cmap[ oldCtx ] = newCtx;

  // duplicate all contexts which used old context,
  // transitively.
  typedef std::vector<const Ctx *> Fringe;
  Fringe fringe; // holds changed contexts.
  fringe.push_back( oldCtx );
  while( !fringe.empty() )
  {
    const Ctx *someChangedCtx = fringe.back();
    fringe.pop_back();

    const Ctx *replacement = cmap[ someChangedCtx ];

    // Update the children of the changed context.

    // First, clone the list of affected contexts
    // since we are going to modify the collection
    Fringe children;
    for(CtxManager::iterator i=ctxManager.begin(), e=ctxManager.end(); i!=e; ++i)
    {
      const Ctx *cc = &*i;
      if( cc->parent == someChangedCtx )
        children.push_back(cc);
    }

    // Update each affected context.
    for(Fringe::const_iterator i=children.begin(), e=children.end(); i!=e; ++i)
    {
      const Ctx *child = *i;

      // clone child
      Ctx *clone = new Ctx();
      clone->type = child->type;
      clone->fcn = child->fcn;

      clone->header = child->header;
      // Loops from the old function were inlined; map the loop header.
      if( child->header != 0
      &&  child->getFcnContext() == oldCtx )
      {
        const ValueToValueMapTy::const_iterator i = vmap.find( child->header );
        assert( i != vmap_end
        && "Can't find image of loop header in the vmap");
        clone->header = cast< BasicBlock >( &*(i->second) );
      }

      clone->parent = replacement;
      clone = fold(clone);

      cmap[ child ] = clone;
      fringe.push_back( child );
    }
  }

  // Since inlineContext() is called repeatedly and the cmap,amap are built
  // progressively, it's possible that a ctx/au should change twice. e.g.
  //   a -> b and b -> c.
  // Flatten the cmap so that a -> c.
  for(;;)
  {
    bool changed = false;
    for(CtxToCtxMap::iterator i=cmap.begin(), e=cmap.end(); i!=e; ++i)
    {
      const Ctx *from = i->first, *to = i->second;
      if( cmap.count(to) )
      {
        cmap[from] = cmap[to];
        changed = true;
        break;
      }
    }

    if( !changed )
      break;
  }

/*
  errs() << "************ cmap ***********\n";
  for(CtxToCtxMap::iterator i=cmap.begin(), e=cmap.end(); i!=e; ++i)
  {
    const Ctx *from = i->first, *to = i->second;
    errs() << " from " << *from << '\n'
           << "   to " << *to << '\n'
           << '\n';
  }
*/

  // Update all AUs which used any changed value or context.
  const CtxToCtxMap::const_iterator cmap_end = cmap.end();
  AUs aus( allAUs );
  for(AUs::const_iterator i=aus.begin(), e=aus.end(); i!=e; ++i)
  {
    AU *oldAU = *i;

    const CtxToCtxMap::const_iterator l = cmap.find( oldAU->ctx );

    if( l == cmap_end )
      continue;

    AU *newAU = new AU( oldAU->type );
    const ValueToValueMapTy::const_iterator k = vmap.find( oldAU->value );
    if( k == vmap_end )
      newAU->value = oldAU->value;
    else
      newAU->value = &* k->second;

    newAU->ctx = l->second;

    newAU = fold(newAU);
    amap[ oldAU ] = newAU;
  }

  // Since inlineContext() is called repeatedly and the cmap,amap are built
  // progressively, it's possible that a ctx/au should change twice. e.g.
  //   a -> b and b -> c.
  // Flatten the amap so that a -> c.
  for(;;)
  {
    bool changed = false;
    for(AuToAuMap::iterator i=amap.begin(), e=amap.end(); i!=e; ++i)
    {
      const AU *from = i->first, *to = i->second;
      if( amap.count(to) )
      {
        amap[from] = amap[to];
        changed = true;
        break;
      }
    }

    if( !changed )
      break;
  }

/*
  errs() << "************ amap ***********\n";
  for(AuToAuMap::iterator i=amap.begin(), e=amap.end(); i!=e; ++i)
  {
    const AU *from = i->first, *to = i->second;
    errs() << " from " << *from << '\n'
           << "   to " << *to << '\n'
           << '\n';
  }
*/
}

}
}

