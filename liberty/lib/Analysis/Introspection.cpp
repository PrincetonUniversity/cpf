#include "llvm/IR/Value.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/CommandLine.h"

#include "liberty/Analysis/Introspection.h"

namespace liberty
{

using namespace llvm;

cl::opt<bool> WatchCallsitePair(
  "watch-callsite-pair", cl::init(false), cl::NotHidden,
  cl::desc("Watch a particular pair of callsites within AA"));
cl::opt<std::string> FirstCallee(
  "watch-callee-a", cl::init(""), cl::NotHidden,
  cl::desc("First callee name"));
cl::opt<std::string> SecondCallee(
  "watch-callee-b", cl::init(""), cl::NotHidden,
  cl::desc("Second callee name"));

cl::opt<bool> WatchStore2Callsite(
  "watch-st-callsite", cl::init(false), cl::NotHidden,
  cl::desc("Watch a particular pair of st,callsite within AA"));
cl::opt<std::string> StorePtrName(
  "watch-st-ptr", cl::init(""), cl::NotHidden,
  cl::desc("Name of pointer stored to"));
cl::opt<bool> WatchCallsite2Store(
  "watch-callsite-st", cl::init(false), cl::NotHidden,
  cl::desc("Watch a particular pair of callsite,st within AA"));

static std::vector<bool> IntrospectStack;

void enterIntrospectionRegion(bool introspect)
{
  IntrospectStack.push_back(introspect);
}

void exitIntrospectionRegion()
{
  IntrospectStack.pop_back();
}

bool isInstrospectionRegion()
{
  if( IntrospectStack.empty() )
    return false;
  else
    return IntrospectStack.back();
}

static unsigned indent = 0;

static void doIndent(bool enter)
{
  if( !enter )
    --indent;

  for(unsigned i=0; i<indent; ++i)
    errs() << "  ";

  if( enter )
    ++indent;
}

static void pptr(const Value *p, unsigned s)
{
  errs() << "ptr ";

  // global variables have a nasty habit of printing a newline!
  if( const GlobalVariable *gv = dyn_cast< GlobalVariable >(p) )
    errs() << gv->getName();
  else
    errs() << *p;

  errs() << " size " << s;
}

void pquery(StringRef who, bool enter, const Instruction *i1, LoopAA::TemporalRelation Rel, const Instruction *i2, const Loop *L, LoopAA::ModRefResult res)
{
  doIndent(enter);

  if( !enter )
    errs() << "} ";

  errs() << (enter ? "Enter " : "Exit ")
         << who << "::modref("
         << *i1 << ", " << Rel << ", " << *i2
         << ", ";
  if( L )
    errs() << L->getHeader()->getParent()->getName()
           << ':' << L->getHeader()->getName();
  else
    errs() << "noloop";

  errs() << ')';

  if( !enter )
    errs() << " => " << res;

  if( enter )
    errs() << " {";

  errs() << '\n';
}

void pquery(StringRef who, bool enter, const Instruction *i1, LoopAA::TemporalRelation Rel, const Value *p2, unsigned s2, const Loop *L, LoopAA::ModRefResult res)
{
  doIndent(enter);

  if( !enter )
    errs() << "} ";

  errs() << (enter ? "Enter " : "Exit ")
         << who << "::modref("
         << *i1 << ", " << Rel << ", ";
  pptr(p2,s2);
  errs() << ", ";
  if( L )
    errs() << L->getHeader()->getParent()->getName()
           << ':' << L->getHeader()->getName();
  else
    errs() << "noloop";

  errs() << ')';

  if( !enter )
    errs() << " => " << res;

  if( enter )
    errs() << " {";

  errs() << '\n';
}

void pquery(StringRef who, bool enter, const Value *p1, unsigned s1, LoopAA::TemporalRelation Rel, const Value *p2, unsigned s2, const Loop *L, LoopAA::AliasResult res)
{
  doIndent(enter);

  if( !enter )
    errs() << "} ";

  errs() << (enter ? "Enter " : "Exit ")
         << who << "::alias(";
  pptr(p1,s1);
  errs() << ", " << Rel << ", ";
  pptr(p2,s2);
  errs() << ", ";
  if( L )
    errs() << L->getHeader()->getParent()->getName()
           << ':' << L->getHeader()->getName();
  else
    errs() << "noloop";

  errs() << ')';

  if( !enter )
    errs() << " => " << res;

  if( enter )
    errs() << " {";

  errs() << '\n';
}

void pquery(StringRef who, bool enter, const CallSite &CS1, LoopAA::TemporalRelation Rel, const CallSite &CS2, const Loop *L, LoopAA::ModRefResult res)
{
  pquery(who,enter, CS1.getInstruction(), Rel, CS2.getInstruction(), L, res);
}

void pquery(StringRef who, bool enter, const CallSite &CS1, LoopAA::TemporalRelation Rel, const ClassicLoopAA::Pointer &P2, const Loop *L, LoopAA::ModRefResult res)
{
  pquery(who,enter, CS1.getInstruction(), Rel, P2.ptr, P2.size, L, res);
}

void pquery(StringRef who, bool enter, const ClassicLoopAA::Pointer &P1, LoopAA::TemporalRelation Rel, const ClassicLoopAA::Pointer &P2, const Loop *L, LoopAA::AliasResult res)
{
  pquery(who,enter, P1.ptr, P1.size, Rel, P2.ptr, P2.size, L, res);
}


}
