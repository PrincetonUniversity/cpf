#include "liberty/SpecPriv/Pieces.h"

namespace liberty
{
namespace SpecPriv
{
using namespace llvm;

const Function *Ctx::getFcn() const
{
  if( type == Ctx_Fcn )
    return fcn;
  else if( type == Ctx_Loop )
    return header->getParent();
  else
    return 0;
}

const Ctx *Ctx::getFcnContext() const
{
  if( type == Ctx_Top )
    return 0;
  else if( type == Ctx_Fcn )
    return this;
  else
    return parent->getFcnContext();
}

void Ctx::Profile(FoldingSetNodeID &id) const
{
  id.AddInteger( type );

  if( type == Ctx_Fcn )
  {
    id.AddPointer(fcn);
    id.AddPointer(parent);
  }

  else if( type == Ctx_Loop )
  {
    id.AddPointer(fcn);
    id.AddPointer(header);
    id.AddInteger(depth);
    id.AddPointer(parent);
  }
}

void Ctx::print_step(raw_ostream &fout) const
{
  if( type == Ctx_Top )
    fout << "TOP";
  else if( type == Ctx_Loop )
    fout << "LOOP " << header->getParent()->getName() << ':' << header->getName();
  else if( type == Ctx_Fcn )
    fout << "FCN " << fcn->getName();
}

void Ctx::print(raw_ostream &fout) const
{
  print_step(fout);

  if( parent )
  {
    fout << " WITHIN ";
    parent->print(fout);
  }
}

raw_ostream &operator<<(raw_ostream &fout, const Ctx &c)
{
  c.print(fout);
  return fout;
}

bool Ctx::step_equal(const Ctx *other) const
{
  if( type != other->type )
    return false;

  if( type == Ctx_Fcn && fcn != other->fcn )
    return false;

  if( type == Ctx_Loop && (header != other->header || depth != other->depth) )
    return false;

  return true;
}

bool Ctx::matches(const Ctx *cc) const
{
  if( !cc )
    return true;
  else if( !this )
    return false;

  for(const Ctx *i=this; i; i=i->parent)
  {
    if( ! i->step_equal(cc) )
      continue;

    const Ctx *j=i, *k = cc;
    while( j && k )
    {
      if( j->step_equal(k) )
      {
        j = j->parent;
        k = k->parent;
      }
      else
      {
        j = j->parent;
      }
    }

    if( !k )
      return true;
  }

  return false;
}

bool Ctx::isWithinSubloopOf(const Ctx *cc) const
{
  for(const Ctx *i=this; i; i=i->parent)
  {
    // Have we reached cc yet?
    if( i->type == cc->type )
    {
      if( i->type == Ctx_Fcn && i->fcn == cc->fcn )
        return false;
      if( i->type == Ctx_Loop && i->header == cc->header )
        return false;
      if( i->type == Ctx_Top )
        return false;
    }

    // No, not yet.  Are we in a different loop?
    if( i->type == Ctx_Loop )
    {
      if( cc->type != Ctx_Loop )
        return true;
      else if( i->header != cc->header )
        return true;

      assert(false && "Reached cc, but didn't detect it.");
    }
  }

  assert( this->matches(cc) && "*this doesn't match cc!");
  assert( false && "I don't know how this happened");
}


void AU::Profile(FoldingSetNodeID &id) const
{
  id.AddInteger(type);

  if( type != AU_Unknown && type != AU_Undefined && type != AU_Null )
    id.AddPointer(value);

  if( type == AU_Stack || type == AU_Heap )
    id.AddPointer(ctx);
}

void AU::print(raw_ostream &fout) const
{
  if( type == AU_Unknown )
    fout << "UNKNOWN";
  else if( type == AU_Undefined )
    fout << "UNDEFINED";
  else if( type == AU_IO )
    fout << "IO";
  else if( type == AU_Null )
    fout << "NULL";
  else if( type == AU_Constant )
    fout << "CONSTANT " << value->getName();
  else if( type == AU_Global )
    fout << "GLOBAL " << value->getName();
  else // stack or heap
  {
    if( type == AU_Stack )
      fout << "STACK ";
    else
      fout << "HEAP ";

    const Instruction *inst = cast< Instruction >(value);
    const BasicBlock *bb = inst->getParent();
    const Function *fcn = bb->getParent();

    fout << fcn->getName() << ' ' << bb->getName() << ' ' << inst->getName();
  }

  if( ctx )
  {
    fout << " AT ";
    ctx->print(fout);
  }
}

raw_ostream &operator<<(raw_ostream &fout, const AU &a)
{
  a.print(fout);
  return fout;
}

bool RepeatableOrderForAUs::operator()(const AU * a, const AU * b)
{
  if( a->type < b->type )
    return true;
  else if( a->type > b->type )
    return false;

  if( a->value != b->value )
  {
    if( !a->value->hasName() && b->value->hasName() )
      return true;
    else if( a->value->hasName() && !b->value->hasName() )
      return false;
    else if( a->value->hasName() && b->value->hasName() )
    {
      if( a->value->getName() < b->value->getName() )
        return true;
      else if( a->value->getName() > b->value->getName() )
        return false;
    }

    const Instruction *ia = dyn_cast< Instruction >( a->value ),
                      *ib = dyn_cast< Instruction >( b->value );

    assert( ia && ib );

    if( ia != ib )
    {
      const BasicBlock *bba = ia->getParent(),
                       *bbb = ib->getParent();

      const Function *fa = bba->getParent(),
                     *fb = bbb->getParent();

      const std::string &nfa = fa->getName(),
                        &nfb = fb->getName();

      if( nfa < nfb )
        return true;
      else if ( nfa > nfb )
        return false;

      const std::string &nbba = bba->getName(),
                        &nbbb = bbb->getName();

      if( nbba < nbbb )
        return true;
      else if( nbba > nbbb )
        return false;

      assert( ia->hasName() );
      assert( ib->hasName() );

      if( ia->getName() < ib->getName() )
        return true;
      else if( ia->getName() > ib->getName() )
        return false;

      assert( false && "different instructions are identical" );
    }
  }

  // Compare contexts.
  return compareContexts(a->ctx, b->ctx);
}

bool RepeatableOrderForAUs::compareContexts(const Ctx *a, const Ctx *b)
{
  if( a == b )
    return false;

  if( !a && b )
    return true;
  if( a && !b )
    return false;

  if( a->type < b->type )
    return true;
  else if( a->type > b->type )
    return false;

  // same type.
  switch( a->type )
  {
    case Ctx_Top:
      return false; // equal

    case Ctx_Fcn:
      if( a->fcn->getName() < b->fcn->getName() )
        return true;
      else if( a->fcn->getName() > b->fcn->getName() )
        return false;
      break;

    case Ctx_Loop:
      if( a->header->getName() < b->header->getName() )
        return true;
      else if( a->header->getName() > b->header->getName() )
        return false;

      if( a->depth < b->depth )
        return true;
      else if( a->depth > b->depth )
        return false;

      if( a->header->getParent()->getName() < b->header->getParent()->getName() )
        return true;
      else if( a->header->getParent()->getName() > b->header->getParent()->getName() )
        return false;
      break;
  }

  if( !a->parent && b->parent )
    return false;
  if( a->parent && !b->parent )
    return true;

  return compareContexts(a->parent, b->parent);
}

bool Ctx::referencesValue(const Value *v) const
{
  if( parent )
    if( parent->referencesValue(v) )
      return true;

  switch( type )
  {
  case Ctx_Top:
    return false;

  case Ctx_Fcn:
    return fcn == (const Function *)v;

  case Ctx_Loop:
    return header == (const BasicBlock *)v;
  }

  // Dead code; gcc complains
  return false;
}

bool AU::referencesValue(const Value *v) const
{
  if( ctx )
    if( ctx->referencesValue(v) )
      return true;

  switch( type )
  {
  case AU_Unknown:
  case AU_Undefined:
  case AU_IO:
  case AU_Null:
    return false;

  case AU_Constant:
  case AU_Global:
  case AU_Stack:
  case AU_Heap:
    return value == v;
  }

  // Dead code; gcc complains
  return false;
}

bool Ptr::referencesValue(const Value *v) const
{
  if( !au )
    return false;

  return au->referencesValue(v);
}

}
}

