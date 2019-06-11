#define DEBUG_TYPE "locality-remed"

#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/SmallBitVector.h"

#include "liberty/Orchestration/LocalityRemed.h"

#define DEFAULT_LOCALITY_REMED_COST 50
#define PRIVATE_ACCESS_COST 100
#define LOCAL_ACCESS_COST 5

namespace liberty {
using namespace llvm;

STATISTIC(numEligible,        "Num eligible queries");
STATISTIC(numPrivatizedPriv,  "Num privatized (Private)");
STATISTIC(numPrivatizedRedux, "Num privatized (Redux)");
STATISTIC(numPrivatizedShort, "Num privatized (Short-lived)");
STATISTIC(numSeparated,       "Num separated");
STATISTIC(numReusedPriv,      "Num avoid extra private inst");
STATISTIC(numUnclassifiedPtrs,"Num of unclassified pointers");
STATISTIC(numLocalityAA,      "Num removed via LocalityAA");
STATISTIC(numLocalityAA2,     "Num removed via LocalityAA (non-removable by LocalityRemed)");
STATISTIC(numSubSep,          "Num separated via subheaps");
//STATISTIC(numStaticReloc, "Static allocations relocated");
//STATISTIC(numDynReloc,    "Dynamic allocations relocated");
STATISTIC(numUOTests,     "UO tests inserted");

//bool LocalityRemedy::addUOChecks(const HeapAssignment &asgn, Loop *loop, BasicBlock *bb, VSet &alreadyInstrumented)
bool LocalityRemedy::addUOCheck(Value *ptr)
{
  UO objectsToInstrument;
  UO pointers;
  Indeterminate::findIndeterminateObjects(ptr, pointers, objectsToInstrument, *DL);

  if( objectsToInstrument.empty() )
    return false;

//    DEBUG(errs() << "RoI contains " << objectsToInstrument.size() << " indeterminate objects.\n");

  // Okay, we have a set of UOs.  Let's instrument them!
  bool modified = false;

  for(UO::iterator i=objectsToInstrument.begin(), e=objectsToInstrument.end(); i!=e; ++i)
  {
    const Value *obj = *i;
    if( alreadyInstrumented->count(obj) )
      continue;
    alreadyInstrumented->insert(obj);

    // What heap do we expect to find this object in?
    HeapAssignment::Type ty = selectHeap(obj,loop);
    if( ty == HeapAssignment::Unclassified )
    {
      DEBUG(errs() << "Cannot check " << *obj << "!!!\n");
      continue;
    }

    Value *cobj = const_cast< Value* >(obj);
    insertUOCheck(cobj,ty);

    modified = true;
  }

  return modified;
}

void LocalityRemedy::insertUOCheck(Value *obj, HeapAssignment::Type heap)
{
  BasicBlock *preheader = loop->getLoopPreheader();
  assert( preheader && "Loop simplify!?");

  //Preprocess &preprocess = getAnalysis< Preprocess >();

  int sh = asgn->getSubHeap(obj, loop, *read);

  // Where should we insert this check?
  InstInsertPt where;
  Instruction *inst_obj = dyn_cast< Instruction >(obj);
  Instruction *cloned_inst = nullptr;
  Instruction *cast;
  if ( inst_obj ) {
    // get cloned inst in the parallelized code if any
    cloned_inst = inst_obj;
    if (task->instructionClones.find(inst_obj) != task->instructionClones.end())
      cloned_inst = task->instructionClones[inst_obj];

//    if( roi.bbs.count(inst_obj->getParent()) )
      where = InstInsertPt::After(cloned_inst);
      //where = InstInsertPt::After(inst_obj);
//    else
//      where = InstInsertPt::End( preheader ); // TODO: assumes single parallel region

    cast = new BitCastInst(cloned_inst,voidptr);
  }
  else if( Argument *arg = dyn_cast< Argument >(obj) )
  {
    Function *f = arg->getParent();
//    if( f == preheader->getParent() )
//      where = InstInsertPt::End( preheader ); // TODO: assumes single parallel region
//    else
      where = InstInsertPt::Beginning( f );
      cast = new BitCastInst(obj,voidptr);
  }
  else
    cast = new BitCastInst(obj,voidptr);

  Twine msg = "UO violation on pointer " + obj->getName()
                  + " in " + where.getFunction()->getName()
                  + " :: " + where.getBlock()->getName()
                  + "; should be in " + Api::getNameForHeap(heap)
                  + ", sub-heap " + Twine(sh);
  Constant *message = getStringLiteralExpression(*mod, msg.str());

  //Instruction *cast = new BitCastInst(obj,voidptr);

  Constant *check = Api(mod).getUO();

  Value *code = ConstantInt::get(u8, (int) heap );
  Constant *subheap = ConstantInt::get(u8, sh);

  Value *args[] = { cast, code, subheap, message };
  Instruction *call = CallInst::Create(check, ArrayRef<Value*>(&args[0], &args[4]) );

  where << cast << call;
  //preprocess.addToLPS(cast, inst_obj);
  //preprocess.addToLPS(call, inst_obj);

  ++numUOTests;
  DEBUG(errs() << "Instrumented indet obj: " << *obj << '\n');
}

// change to bool eventually
void LocalityRemedy::apply(Task *task) {
  bool modified = false;
  this->task = task;
  if (privateI)
    modified |= replacePrivateLoadsStore(privateI);
  // modified runtime does not need replaceReduxStore anymore
  // caused big perf problem in 052.alvinn. prevented vectorization
  //else if (reduxS)
  //  modified |= replaceReduxStore(reduxS);

  if (ptr1)
    modified |= addUOCheck(ptr1);
  if (ptr2)
    modified |= addUOCheck(ptr2);
  //return modified;
}

bool LocalityRemedy::compare(const Remedy_ptr rhs) const {
  std::shared_ptr<LocalityRemedy> sepRhs =
      std::static_pointer_cast<LocalityRemedy>(rhs);

  if (this->privateI == sepRhs->privateI) {
    if (this->reduxS == sepRhs->reduxS) {
      if (this->ptr1 == sepRhs->ptr1) {
        return this->ptr2 < sepRhs->ptr2;
      }
      return this->ptr1 < sepRhs->ptr1;
    }
    return this->reduxS < sepRhs->reduxS;
  }
  return this->privateI < sepRhs->privateI;
}

Remedies LocalityRemediator::satisfy(const PDG &pdg, Loop *loop,
                                     const Criticisms &criticisms) {

  const DataLayout &DL = loop->getHeader()->getModule()->getDataLayout();
  const Ctx *ctx = read.getCtx(loop);
  localityaa = new LocalityAA(read, asgn, ctx);
  localityaa->InitializeLoopAA(&proxy, DL);

  Remedies remedies = Remediator::satisfy(pdg, loop, criticisms);

  delete localityaa;

  return remedies;
}

Remediator::RemedResp LocalityRemediator::memdep(const Instruction *A,
                                                 const Instruction *B,
                                                 bool LoopCarried, bool RAW,
                                                 const Loop *L) {
  Remediator::RemedResp remedResp;
  // conservative answer
  remedResp.depRes = DepResult::Dep;
  std::shared_ptr<LocalityRemedy> remedy =
      std::shared_ptr<LocalityRemedy>(new LocalityRemedy());
  remedy->cost = DEFAULT_LOCALITY_REMED_COST;

  remedy->privateI = nullptr;
  remedy->reduxS = nullptr;
  remedy->ptr1 = nullptr;
  remedy->ptr2 = nullptr;

  remedResp.remedy = remedy;

  if (!L || !asgn.isValidFor(L))
    return remedResp;

  const DataLayout &DL = A->getModule()->getDataLayout();
  //localityaa->InitializeLoopAA(&proxy, DL);
  // This AA stack includes static analysis and separation speculation
  LoopAA *aa = localityaa->getTopAA();
  //aa->dump();

  remedy->DL = &DL;
  remedy->loop = const_cast<Loop*>(L);

  const Value *ptr1 = liberty::getMemOper(A);
  const Value *ptr2 = liberty::getMemOper(B);
  if (!ptr1 || !ptr2) {
    bool noDep =
        (LoopCarried)
            ? noMemoryDep(A, B, LoopAA::Before, LoopAA::After, L, aa, RAW)
            : noMemoryDep(A, B, LoopAA::Same, LoopAA::Same, L, aa, RAW);
    if (noDep) {
      ++numLocalityAA;
      remedResp.depRes = DepResult::NoDep;
      remedy->type = LocalityRemedy::LocalityAA;
      remedResp.remedy = remedy;
    }
    return remedResp;
  }
  if (!isa<PointerType>(ptr1->getType()))
    return remedResp;
  if (!isa<PointerType>(ptr2->getType()))
    return remedResp;

  const Ctx *ctx = read.getCtx(L);

  ++numEligible;

  Ptrs aus1;
  HeapAssignment::Type t1 = HeapAssignment::Unclassified;
  if (read.getUnderlyingAUs(ptr1, ctx, aus1))
    t1 = asgn.classify(aus1);

  Ptrs aus2;
  HeapAssignment::Type t2 = HeapAssignment::Unclassified;
  if (read.getUnderlyingAUs(ptr2, ctx, aus2))
    t2 = asgn.classify(aus2);

  // errs() << "Instruction A: " << *A << "\n  type: " << t1 << "\nInstruction
  // B: " << *B << "\n  type: " << t2 << '\n';

  if (t1 == HeapAssignment::Unclassified) {
    if (!unclassifiedPointers.count(ptr1)) {
      unclassifiedPointers.insert(ptr1);
      ++numUnclassifiedPtrs;
      DEBUG(errs() << "Pointer to unclassified heap: " << *ptr1 << "\n");
    }
  }
  if (t2 == HeapAssignment::Unclassified) {
    if (!unclassifiedPointers.count(ptr2)) {
      unclassifiedPointers.insert(ptr2);
      ++numUnclassifiedPtrs;
      DEBUG(errs() << "Pointer to unclassified heap: " << *ptr2 << "\n");
    }
  }

  // Loop-carried queries:
  if (LoopCarried) {
    // Reduction, local and private heaps are iteration-private, thus
    // there cannot be cross-iteration flows.
    if (t1 == HeapAssignment::Redux || t1 == HeapAssignment::Local) {
      remedResp.depRes = DepResult::NoDep;
      if (t1 == HeapAssignment::Local) {
        ++numPrivatizedShort;
        remedy->cost += LOCAL_ACCESS_COST;
        remedy->type = LocalityRemedy::Local;
        //remedy->localI = A;
      } else {
        ++numPrivatizedRedux;
        if (auto sA = dyn_cast<StoreInst>(A))
          remedy->reduxS = const_cast<StoreInst *>(sA);
        remedy->type = LocalityRemedy::Redux;
      }
      remedy->ptr = const_cast<Value *>(ptr1);
      remedResp.remedy = remedy;
      return remedResp;
    }

    if (t2 == HeapAssignment::Redux || t2 == HeapAssignment::Local) {
      remedResp.depRes = DepResult::NoDep;
      if (t2 == HeapAssignment::Local) {
        ++numPrivatizedShort;
        remedy->cost += LOCAL_ACCESS_COST;
        remedy->type = LocalityRemedy::Local;
      } else {
        ++numPrivatizedRedux;
        if (auto sB = dyn_cast<StoreInst>(B))
          remedy->reduxS = const_cast<StoreInst *>(sB);
        remedy->type = LocalityRemedy::Redux;
      }
      remedy->ptr = const_cast<Value *>(ptr2);
      remedResp.remedy = remedy;
      return remedResp;
    }
  }

  // Both loop-carried and intra-iteration queries: are they assigned to
  // different heaps?
  if (t1 != t2 && t1 != HeapAssignment::Unclassified &&
      t2 != HeapAssignment::Unclassified) {
    ++numSeparated;
    remedResp.depRes = DepResult::NoDep;
    remedy->ptr1 = const_cast<Value *>(ptr1);
    remedy->ptr2 = const_cast<Value *>(ptr2);
    remedy->type = LocalityRemedy::Separated;
    remedResp.remedy = remedy;
    return remedResp;
  }

  // if one of the memory accesses is private, then there is no loop-carried.
  // Validation for private accesses is more expensive than read-only and local
  // and thus private accesses are checked last
  if (LoopCarried) {
    // if memory access in instruction B was already identified as private,
    // re-use it instead of introducing another private inst.
    if (t1 == HeapAssignment::Private && !privateInsts.count(B)) {
      ++numPrivatizedPriv;
      remedy->cost += PRIVATE_ACCESS_COST;
      remedy->privateI = const_cast<Instruction *>(A);
      remedResp.depRes = DepResult::NoDep;
      remedy->type = LocalityRemedy::Private;
      remedResp.remedy = remedy;
      privateInsts.insert(A);
      return remedResp;
    } else if (t2 == HeapAssignment::Private) {
      if (t1 == HeapAssignment::Private && privateInsts.count(B)) {
        ++numReusedPriv;
      }
      ++numPrivatizedPriv;
      remedy->cost += PRIVATE_ACCESS_COST;
      remedy->privateI = const_cast<Instruction *>(B);
      remedResp.depRes = DepResult::NoDep;
      remedy->type = LocalityRemedy::Private;
      remedResp.remedy = remedy;
      privateInsts.insert(B);
      return remedResp;
    }
  }

  // They are assigned to the same heap.
  // Are they assigned to different sub-heaps?
  if (t1 == t2 && t1 != HeapAssignment::Unclassified) {
    const int subheap1 = asgn.getSubHeap(aus1);
    if (subheap1 > 0) {
      const int subheap2 = asgn.getSubHeap(aus2);
      if (subheap2 > 0 && subheap1 != subheap2) {
        ++numSubSep;
        remedResp.depRes = DepResult::NoDep;
        remedy->ptr1 = const_cast<Value *>(ptr1);
        remedy->ptr2 = const_cast<Value *>(ptr2);
        remedy->type = LocalityRemedy::Subheaps;
        remedResp.remedy = remedy;
        return remedResp;
      }
    }
  }

  // avoid usage of localityAA since the needed checks/remedies are not produced yet for localityAA
  // check if collaboration of AA and LocalityAA achieves better accuracy
  bool noDep =
      (LoopCarried)
          ? noMemoryDep(A, B, LoopAA::Before, LoopAA::After, L, aa, RAW)
          : noMemoryDep(A, B, LoopAA::Same, LoopAA::Same, L, aa, RAW);
  if (noDep) {
    ++numLocalityAA2;
    remedResp.depRes = DepResult::NoDep;
    remedy->type = LocalityRemedy::LocalityAA;
    remedResp.remedy = remedy;
  }

  return remedResp;
}

} // namespace liberty
