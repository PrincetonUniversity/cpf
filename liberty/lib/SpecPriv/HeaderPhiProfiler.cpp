//===- HeaderPhiProfiler.cpp - Insert HeaderPhiProfiler instrumentation -----------===//
//
// Profiles loop header values that produces same values throughput invocation
//

#define DEBUG_TYPE "header-phi-prof"

#include "liberty/SpecPriv/HeaderPhiProfiler.h"

#include "liberty/LoopProf/Targets.h"
#include "liberty/Utilities/CastUtil.h"
#include "liberty/Utilities/GlobalCtors.h"
#include "liberty/Utilities/InstInsertPt.h"
#include "Metadata.h"

#include <vector>

using namespace llvm;

namespace liberty
{

char HeaderPhiProfiler::ID = 0;
static RegisterPass<HeaderPhiProfiler> RP("insert-headerphi-profiling",
  "Insert instrumentation for header-phi profiling", false, false);

HeaderPhiProfiler::HeaderPhiProfiler() : ModulePass(ID)
{
}

HeaderPhiProfiler::~HeaderPhiProfiler()
{
}

void HeaderPhiProfiler::getAnalysisUsage(AnalysisUsage& au) const
{
  au.addRequired< ModuleLoops >();
  au.addRequired< Targets >();
  au.setPreservesAll();
}

bool HeaderPhiProfiler::runOnModule(Module& m)
{
  ModuleLoops& mloops = getAnalysis< ModuleLoops >();
  Targets&     targets = getAnalysis< Targets >();
  LLVMContext& ctxt = m.getContext();

  Type* u64Ty = Type::getInt64Ty( ctxt );
  Type* voidTy = Type::getVoidTy( ctxt );

  std::vector<Type*> formals;
  formals.push_back( u64Ty );
  formals.push_back( u64Ty );

  FunctionType* fty = FunctionType::get( voidTy, formals, false );
  Function*     invoke = cast<Function>( m.getOrInsertFunction( "__headerphi_prof_invocation", fty ) );
  Function*     iter = cast<Function>( m.getOrInsertFunction( "__headerphi_prof_iteration", fty ) );

  // instrument each target loop

  bool modified = false;
  for(Targets::iterator i=targets.begin(mloops), e=targets.end(mloops); i!=e; ++i)
  {
    modified |= true;
    instrumentLoop( *i, invoke, iter );
  }

  if (!modified)
  {
    errs() << "No instrumentation added for headerphi profiling. Is loopProf.out available?\n";
    return false;
  }

  // instrument desctructor

  Function* dtor = cast<Function>( m.getOrInsertFunction( "__headerphi_prof_dtor", voidTy) );
  BasicBlock* entry = BasicBlock::Create( ctxt, "entry", dtor, NULL );
  ReturnInst::Create( ctxt, entry );
  callAfterMain( dtor, 0 );

  // call print function

  Function* fini = cast<Function>( m.getOrInsertFunction( "__headerphi_prof_print", voidTy) );
  CallInst::Create( fini, "", entry->getTerminator() );

  return true;
}

void HeaderPhiProfiler::instrumentLoop(Loop* loop, Function* invoke, Function* iter)
{
  BasicBlock* header = loop->getHeader();
  BasicBlock* latch = loop->getLoopLatch();

  // check if loop-simplify pass executed
  assert( loop->getNumBackEdges() == 1 && "Should be only 1 back edge, loop-simplify?");
  assert( latch && "Loop latch needs to exist, loop-simplify?");

  PHINode* funcphi = PHINode::Create( invoke->getType(), 2, "funcphi", header->getFirstNonPHI() );
  for (pred_iterator pi = pred_begin(header) ; pi != pred_end(header) ; pi++)
    if ( *pi == latch)
      funcphi->addIncoming(iter, *pi);
    else
      funcphi->addIncoming(invoke, *pi);

  InstInsertPt pt = InstInsertPt::Before( header->getTerminator() );

  for (BasicBlock::iterator ii = header->begin() ; ii != header->end() ; ii++)
  {
    if ( isa<PHINode>(&*ii) && Namer::getInstrIdValue(&*ii) )
    {
      std::vector< Value* > args;
      args.push_back( castToInt64Ty( Namer::getInstrIdValue(&*ii), pt) );
      args.push_back( castToInt64Ty(&*ii, pt) );
      pt << CallInst::Create( funcphi, args );
    }
  }
}

}
