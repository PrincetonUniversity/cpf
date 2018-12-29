#ifndef LLVM_LIBERTY_AA_INTROSPECTION_H
#define LLVM_LIBERTY_AA_INTROSPECTION_H

#include "llvm/Support/CommandLine.h"
#include "llvm/IR/CallSite.h"
#include "liberty/Analysis/LoopAA.h"
#include "liberty/Analysis/ClassicLoopAA.h"

namespace liberty
{
using namespace llvm;


///-------------- Command line options
/// Use these to allow the user to specify things worth inspecting.
extern cl::opt<bool> WatchCallsitePair;
extern cl::opt<std::string> FirstCallee;
extern cl::opt<std::string> SecondCallee;

extern cl::opt<bool> WatchStore2Callsite;
extern cl::opt<std::string> StorePtrName;

extern cl::opt<bool> WatchCallsite2Store;

/// Called by an analysis to put the current query,
/// and any sub-queries, into inspection mode.
void enterIntrospectionRegion(bool introspect = true);
void exitIntrospectionRegion();


/// Determine if we are in introspection mode.
bool isInstrospectionRegion();

/// Used to print out a query.  Use the macros below instead.
void pquery(StringRef who, bool enter, const Instruction *i1, LoopAA::TemporalRelation Rel, const Instruction *i2, const Loop *L, LoopAA::ModRefResult res = LoopAA::ModRef);
void pquery(StringRef who, bool enter, const Instruction *i1, LoopAA::TemporalRelation Rel, const Value *p2, unsigned s2, const Loop *L, LoopAA::ModRefResult res = LoopAA::ModRef);
void pquery(StringRef who, bool enter, const Value *v1, unsigned s1, LoopAA::TemporalRelation Rel, const Value *p2, unsigned s2, const Loop *L, LoopAA::AliasResult res = LoopAA::MayAlias);
void pquery(StringRef who, bool enter, const CallSite &CS1, LoopAA::TemporalRelation Rel, const CallSite &CS2, const Loop *L, LoopAA::ModRefResult res = LoopAA::ModRef);
void pquery(StringRef who, bool enter, const CallSite &CS1, LoopAA::TemporalRelation Rel, const ClassicLoopAA::Pointer &P2, const Loop *L, LoopAA::ModRefResult res = LoopAA::ModRef);
void pquery(StringRef who, bool enter, const ClassicLoopAA::Pointer &P1, LoopAA::TemporalRelation Rel, const ClassicLoopAA::Pointer &P2, const Loop *L, LoopAA::AliasResult res = LoopAA::MayAlias);

#define ENTER(...)               do { pquery(DEBUG_TYPE,true,__VA_ARGS__); } while(0)
#define EXIT(...)                do { pquery(DEBUG_TYPE,false,__VA_ARGS__); } while(0)


/// Just like DEBUG: this will evaluate X only if in introspection mode.
#define INTROSPECT(X)            do { if( isInstrospectionRegion() ) { X; } } while(0)

}

#endif

