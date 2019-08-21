#define DEBUG_TYPE "typeaa"

#include "llvm/IR/Constants.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/Analysis/MemoryBuiltins.h"
#include "llvm/IR/CallSite.h"
#include "llvm/IR/CFG.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/raw_ostream.h"

#include "liberty/Analysis/LoopAA.h"
#include "liberty/Analysis/TypeSanity.h"
#include "liberty/Utilities/CallSiteFactory.h"
#include "liberty/Utilities/FindUnderlyingObjects.h"
#include "AcyclicAA.h"
#include "NoEscapeFieldsAA.h"


namespace liberty
{
  using namespace llvm;

  char AcyclicAA::ID = 0;
  namespace
  {
    static RegisterPass<AcyclicAA>
    X("acyclic-aa", "Identify sane, acyclic, recursive data structures", false, true);
    static RegisterAnalysisGroup<LoopAA> Y(X);
  }

  STATISTIC(numAcyclic,   "Number of acyclic, recursive, sane types identified");
  STATISTIC(numQueries,   "Number of AA queries given to AcyclicAA");
  STATISTIC(numNoAliases, "Number of no-alias results given because of acyclic data structures");


  typedef DenseMap<Value*,bool>  Val2Bool;

  // Determine if this value is always a newly allocated object.
  //
  //  (0) An undefined value.
  //  (1) A null pointer.
  //  (2) A newly allocated object from an AllocaInst.
  //  (3) A newly allocated object from malloc(), calloc() or realloc().
  static bool isAlloc(const Value *v, const DataLayout &td,
                      const TargetLibraryInfo &tli) {
    if(isa<UndefValue>(v))
      return true;

    if(isa<ConstantPointerNull>(v))
      return true;

    if(isa<AllocaInst>(v))
      return true;

    CallSite cs = getCallSite(v);
    if(!cs.getInstruction())
      return false;

    if (!cs.getCalledValue())
      return false;

    // Compensate for constant expressions casting functions to different
    // types. These are especially common in pre-ANSI C programs.
    Function *F =
      dyn_cast<Function>(GetUnderlyingObject(cs.getCalledValue(), td));
    if(!F)
      return false;

    // Check for malloc, calloc, realloc, new, et al.
    if(F->returnDoesNotAlias() || isNoAliasFn(v, &tli))
      return true;

    return false;
  }

  static bool isNewObject(Value *v, Value *container, const DataLayout &td,
                          const TargetLibraryInfo &tli) {
    UO values;
    GetUnderlyingObjects(v, values, td);

    UO containers;
    GetUnderlyingObjects(container, containers, td);

    typedef UO::iterator ObjectSetIt;
    const ObjectSetIt B = values.begin();
    const ObjectSetIt E = values.end();
    for(ObjectSetIt object = B; object != E; ++object)
    {

      if(containers.count(*object))
      {
        DEBUG(errs() << "\tNot a new object (containers.count="
            << containers.count(*object) << "): " << *v << '\n'
            << "\t\t\tobject: " << **object << "\n");
        return false;
      }

      if(!isAlloc(*object, td, tli))
      {
        DEBUG(errs() << "\tNot a new object (notAlloc): " << *v << '\n');
        return false;
      }
    }

    return true;
  }

  static bool isAcyclic(Function *f, Val2Bool &valuesAcyclic,
                        const TargetLibraryInfo &tli);

  // Determine if the value computes an acyclic structure.
  // In particular, this means either
  //  (0) It was loaded from an acyclic structure [via IH]
  //  (1) It was returned from a function that only returns acyclic structures
  //  (2) It is a PHI, select, or cast of any of these.
  //  (3) It is a new object.
  static bool isAcyclic(Value *tl, Val2Bool &valuesAcyclic,
                        const DataLayout &td, const TargetLibraryInfo &tli) {
    // break recursions
    if( valuesAcyclic.count(tl) )
      return valuesAcyclic[tl];

    // break recursion, inductive hypo
    valuesAcyclic[tl] = true;

    LoadInst *load = dyn_cast<LoadInst>( tl );
    if( load )
    {
      Value *ptr = load->getPointerOperand();
      GetElementPtrInst *gep = dyn_cast<GetElementPtrInst>(ptr);
      if( !gep )
      {
        DEBUG(errs() << "\tNot acyclic tail: " << *tl << '\n');
        valuesAcyclic[tl] = false;
        return false;
      }
      if( gep->getPointerOperandType() != tl->getType() )
      {
        DEBUG(errs() << "\tNot acyclic tail: " << *tl << '\n');
        valuesAcyclic[tl] = false;
        return false;
      }

      if( ! isAcyclic(gep->getPointerOperand(), valuesAcyclic, td, tli) )
      {
        DEBUG(errs() << "\t             via: " << *tl << '\n');
        valuesAcyclic[tl] = false;
        return false;
      }

      valuesAcyclic[tl] = true;
      return true;
    }

    CallSite cs = getCallSite(tl);
    if( cs.getInstruction() != 0 )
    {
      Function *f = cs.getCalledFunction();

      if( isAcyclic(f, valuesAcyclic, tli) )
      {
        valuesAcyclic[tl] = true;
        return true;
      }
    }

    PHINode *phi = dyn_cast<PHINode>(tl);
    if( phi )
    {
      for(unsigned i=0; i<phi->getNumIncomingValues(); ++i)
        if( ! isAcyclic( phi->getIncomingValue(i), valuesAcyclic, td, tli) )
        {
          DEBUG(errs() << "\t             via: " << *tl << '\n');
          valuesAcyclic[tl] = false;
          return false;
        }

      valuesAcyclic[tl] = true;
      return true;
    }

    SelectInst *select = dyn_cast<SelectInst>(tl);
    if( select )
    {
      if( isAcyclic( select->getTrueValue(), valuesAcyclic, td, tli)
       && isAcyclic( select->getFalseValue(), valuesAcyclic, td, tli) )
      {
        valuesAcyclic[tl] = true;
        return true;
      }

      DEBUG(errs() << "\t             via: " << *tl << '\n');
      valuesAcyclic[tl] = false;
      return false;
    }

    CastInst *cast = dyn_cast<CastInst>(tl);
    if( cast )
    {
      if( isAcyclic( cast->getOperand(0), valuesAcyclic, td, tli ) )
      {
        valuesAcyclic[tl] = true;
        return true;
      }

      DEBUG(errs() << "\t             via: " << *tl << '\n');
      valuesAcyclic[tl] = false;
      return false;
    }

    if( !isNewObject(tl, 0, td, tli) )
    {
      DEBUG(errs() << "\tNot acyclic tail: " << *tl << '\n');
      valuesAcyclic[tl] = false;
      return false;
    }

    valuesAcyclic[tl] = true;
    return true;
  }

  static bool isAcyclic(Function *f, Val2Bool &valuesAcyclic,
                        const TargetLibraryInfo &tli) {
    if( f->isDeclaration() )
    {
      DEBUG(errs() << "\tNot acyclic tail: <external function>\n");
      // save our final result.
      valuesAcyclic[f] = false;
      return false;
    }

    if( valuesAcyclic.count(f) )
      return valuesAcyclic[f];

    // The inductive hypothesis
    // also, avoid infinite recursion.
    // Assume, initially, this is true
    valuesAcyclic[f] = true;

    const DataLayout &td = (f->getParent())->getDataLayout();

    // Check EVERY RETURN statement in this function
    typedef Function::iterator BI;
    for(BI i=f->begin(), e=f->end(); i!=e; ++i)
    {
      BasicBlock *bb = &*i;
      TerminatorInst *term = bb->getTerminator();
      ReturnInst *ret = dyn_cast<ReturnInst>( term );

      if( !ret )
        continue;

      if( !isAcyclic(ret->getReturnValue(), valuesAcyclic, td, tli) )
      {
        DEBUG(errs() << "\t             via: " << *ret << '\n');
        // save our final result.
        valuesAcyclic[f] = false;
        return false;
      }
    }

    // save our final result.
    valuesAcyclic[f] = true;
    return true;
  }

  static bool proveAcyclic(Type *ty, Function &fcn, const DataLayout &td,
                           const TargetLibraryInfo &tli) {
    Val2Bool    functionsReturningNew;
    Val2Bool    valuesAcyclic;

    // Find every store operation which may change
    // the structure of this type.
    // Specifically, we are looking for
    //  T** gep = GEP T* hd, ...
    //  store T* tl, T** gep
    for(inst_iterator i=inst_begin(fcn), e=inst_end(fcn); i!=e; ++i)
    {
      Instruction *inst = &*i;

      StoreInst *store = dyn_cast<StoreInst>(inst);
      if( !store )
        continue;

      Value *tl = store->getOperand(0);

      if( tl->getType() != ty )
        continue;

      Value *ptr = store->getPointerOperand();
      GetElementPtrInst *gep = dyn_cast<GetElementPtrInst>(ptr);
      if( !gep )
        continue;
      if( gep->getPointerOperandType() != ty )
        continue;
      Value *hd = gep->getPointerOperand();

      DEBUG(errs() << "\tIn function '" <<
          inst->getParent()->getParent()->getName()
          << "' Found a mutation " << *store << ".\n");

      // The head or the tail must be a single, new object
      if( isNewObject(hd, tl, td, tli) || isNewObject(tl, hd, td, tli) )
      {
        DEBUG(errs() << "\t\tHead " << *hd << " is new.\n");

        // The tail must be acyclic
        if( tl != hd )
//        if( isAcyclic(tl, valuesAcyclic) )
        {
          DEBUG(errs() << "\t\tAnd the tail " << *tl << " is acyclic.\n");
          continue;
        }
        else
        {
          DEBUG(errs() << "\t\tBut the tail " << *tl << " may be cyclic.\n");
        }
      }
      else
      {
        DEBUG(errs() << "\t\tHead " << *hd << " may be not new.\n");
      }

            // All else fails
      DEBUG(errs() <<   "\t             via: " << *inst
                   << "\n\t              In: " << fcn.getName()
                   << "\n\t              At: " << inst->getParent()->getName()
                   << ".\n");
      return false;
    }

    return true;
  }

  // attempt to prove that every store to the
  // recursive field of objects of this type
  // is a new object.
  static bool proveAcyclic(Type *ty, Module &mod, const TargetLibraryInfo &tli)
  {
    // Here, ty is something like "BSTNode *"
    // We are looking for a store of a "BSTNode *" into a "BSTNode **"

    const DataLayout &td = mod.getDataLayout();

    typedef Module::iterator FI;
    for(FI i=mod.begin(), e=mod.end(); i!=e; ++i)
    {
      Function &fcn = *i;
      if( !proveAcyclic(ty, fcn, td, tli) )
        return false;
    }
    return true;
  }

  /// Scan this function for accesses to a
  /// recursive field.  If the structure is
  /// sane, record it in recTysOut.
  /// Also, check that whenever we take the
  /// address of the recursive field, that we
  /// immediately eliminate that pointer in
  /// a load or store, i.e. the pointer does
  /// not escape.
  void AcyclicAA::accumulateRecursiveTypes(
    Function &fcn,
    TypeSet &visited,
    Types &recTysOut) const
  {
//    DEBUG(errs() << "Scanning function " << fcn.getName()
//                 << " for recursive types.\n");
    TypeSanityAnalysis &typeaa = getAnalysis< TypeSanityAnalysis >();
    NonCapturedFieldsAnalysis &noEscape = getAnalysis< NonCapturedFieldsAnalysis >();

    // Seek out every GEP which gets a recursive type
    // i.e. of the form:
    //      A* y = something.
    //      A** x = gep(y, ...)
    for(inst_iterator i=inst_begin(fcn), e=inst_end(fcn); i!=e; ++i)
    {
      Instruction *inst = &*i;

      GetElementPtrInst *gep = dyn_cast<GetElementPtrInst>( inst );
      if( !gep )
        continue;

      // not necessarily of PointerType, it could also be of VectorType
      if (!gep->getPointerOperandType()->isPointerTy() ||
          !gep->getType()->isPointerTy())
        continue;

      PointerType *base = cast<PointerType>(gep->getPointerOperandType());

      // Result of gep is, by def, a pointer: Not true in newer LLVM versions.
      // gep could also return a vector of pointers (of type VectorTy)
      Type *result = gep->getType();
      PointerType *ptr = cast<PointerType>( result );

      // sot: handle case with bitcast after gep insted of the inverse
      //e.g.  %call2 = tail call noalias i8* @malloc(i64 24)
      //      %next = getelementptr inbounds i8, i8* %call2, i64 16
      //      %2 = bitcast i8* %next to %struct._node_t**
      // instead of having
      //      %1 = bitcast i8* %call2 to %struct._node_t*
      //      %next = getelementptr inbounds %struct._node_t, %struct._node_t* %1, ...
      // which would match the usual scheme

      if (ptr == base && ptr == Type::getInt8PtrTy(fcn.getContext())) {
        DEBUG(errs() << "Found gep with i8* as result and pointer operand: " << *gep << '\n');

        // find the user of gep and make sure it is only one, and it is a
        // bitcast to a pointer to a pointer

        // TODO: maybe the only use is not needed. Being conservative
        if (!gep->hasOneUse())
          continue;
        for (User *U : gep->users()) {
          Instruction *UserI = cast<Instruction>(U);
          // TODO: maybe this could also be CastInst
          if (BitCastInst *bcI = dyn_cast<BitCastInst>(UserI)) {
            if (PointerType *new_ptr = dyn_cast<PointerType>(bcI->getDestTy()) ) {
              if (PointerType *new_base = dyn_cast<PointerType>(new_ptr->getElementType()) ) {

                //if( gep->getNumIndices() != 1 )
                //  continue;

                // check that the pointerOperand of gep is bitcasted at some
                // point to the found base type
                auto *original_ptr = gep->getPointerOperand();
                for (User *U : original_ptr->users()) {
                  //Instruction *UserI = cast<Instruction>(U);
                  if (BitCastInst *bcI = dyn_cast<BitCastInst>(U)) {
                    if (new_base == bcI->getDestTy()) {
                      ptr = new_ptr;
                      base = new_base;
                      DEBUG(errs()
                            << "Found potentially recursive GEP with weird "
                               "bitcasting disguise. The new base is "
                            << *base << " and the new ptr is " << *ptr << '\n');
                    }
                  }
                }
              }
            }
          }
        }
      }

      // recursive GEP!
      if( ptr->getElementType() != base )
        continue;

      if( visited.count(base) )
        continue;
      visited.insert(base);

      if( ! typeaa.isSane( base ) ) {
        DEBUG(errs() << "Type " << *base << " is not sane\n");
        continue;
      }

      if( noEscape.captured(gep) )
      {
        DEBUG(errs() << "Not safe because this field is captured: " << *gep << '\n');
        continue;
      }

//      DEBUG(
//        errs() << "\tRecursive: ";
//        base->dump( fcn.getParent() );
//      );

      // Okay, we observe this sane, recursive type for the first time.
      // We have not yet determined if this type is cyclic.
      recTysOut.push_back(base);

    }

//    DEBUG(errs() << "Done scanning function "
//                 << fcn.getName() << ".\n");
  }

  bool AcyclicAA::isChildOfTransitive(const Value *v1, const Value *v2,
                                      TemporalRelation rel, const Loop *L,
                                      SmallValueSet &noInfiniteLoops) const
  {
    // Is V1 a field of V2?
    if( noInfiniteLoops.count(v1) )
      return true;
    noInfiniteLoops.insert(v1);

    if( isa< ConstantPointerNull >(v1) )
      return true;

    if( const PHINode *phi = dyn_cast<PHINode>(v1) )
    {
      for(unsigned i=0; i<phi->getNumIncomingValues(); ++i)
        if( !isChildOfTransitive(phi->getIncomingValue(i), v2, rel, L, noInfiniteLoops) )
        {
          DEBUG(errs() << "\to PHI : " << *phi << ".\n");

          // If the value came from outside the loop and this is not a Same query,
          // then it is safe.
          if(rel == LoopAA::Same || L->contains(phi->getIncomingBlock(i)))
            return false;
        }

      return true;
    }

    NonCapturedFieldsAnalysis &noEscape = getAnalysis< NonCapturedFieldsAnalysis >();

    if( const LoadInst *load = dyn_cast< LoadInst >(v1) )
      if( const GetElementPtrInst *gep = dyn_cast< GetElementPtrInst >(load->getPointerOperand() ) )
        if( const Value *base = gep->getPointerOperand() )
          if( base->getType() == v2->getType() )
            if( ! noEscape.captured(gep) )
            {
              if( base == v2 )
                return true;
              else if( isChildOfTransitive(base, v2, rel, L, noInfiniteLoops) )
                return true;

              DEBUG(errs() << "\to GEP : " << *gep << ".\n");
              DEBUG(errs() << "\to Load: " << *load << ".\n");
              return false;
            }

    DEBUG(errs() << "\to Bad : " << *v1 << ".\n");
    return false;
  }

  bool AcyclicAA::isChildOfTransitive(const Value *v1, const Value *v2,
                                      TemporalRelation rel, const Loop *L) const
  {
    DEBUG(errs() << "isChildOfTransitive(" << *v1 << ", " << *v2 << ").\n");
    SmallValueSet noInfiniteLoops;
    return isChildOfTransitive(v1,v2,rel,L,noInfiniteLoops);
  }

  LoopAA::AliasResult AcyclicAA::aliasCheck(
    const Pointer &P1,
    TemporalRelation rel,
    const Pointer &P2,
    const Loop *L)
  {
    DEBUG(errs() << "AcyclicAA\n" << " - " << *P1.ptr << "\n - " << *P2.ptr << '\n');
    ++numQueries;

    Type *t = P1.ptr->getType();
    if( t == P2.ptr->getType() && acyclic.count(t) )
    {
      // Is one accessed as a field of the other?
      if( isChildOfTransitive(P1.ptr,P2.ptr,rel,L)
      ||  isChildOfTransitive(P2.ptr,P1.ptr,rel,L) )
      {
        DEBUG(errs() << "AcyclicAA: noalias1 between " << *P1.ptr << " and " << *P2.ptr << "\n");
        ++numNoAliases;
        return NoAlias;
      }
    }

    const DataLayout &TD = currentModule->getDataLayout();

    // sot: there seems to be a bug when P1.ptr or P2.ptr is a select inst
    // it seems that GetUnderlyingObjects should be used instead.
    // Leave the checks for now to avoid seg faults
    if (isa<PHINode>(P1.ptr) || isa<SelectInst>(P1.ptr))
      return MayAlias;
    if (isa<PHINode>(P2.ptr) || isa<SelectInst>(P2.ptr))
      return MayAlias;

    const Value *UO1 = GetUnderlyingObject(P1.ptr, TD);
    const Value *UO2 = GetUnderlyingObject(P2.ptr, TD);

    if( UO1->getType() == UO2->getType() && acyclic.count(UO1->getType()) )
    {

      if( isChildOfTransitive(UO1, UO2, rel, L) ||
          isChildOfTransitive(UO2, UO1, rel, L) )
      {
        DEBUG(errs() << "AcyclicAA: noalias2 between " << *P1.ptr << " and " << *P2.ptr << "\n");
        ++numNoAliases;
        return NoAlias;
      }
    }

    return MayAlias;
  }


  void AcyclicAA::accumulateRecursiveTypes(
    Types &recTysOut) const
  {
    DEBUG(errs() << "Scanning module for recursive types.\n");
    TypeSet visited;
    typedef Module::iterator FI;
    for(FI i=currentModule->begin(), e=currentModule->end(); i!=e; ++i)
    {
      Function &fcn = *i;
      accumulateRecursiveTypes(fcn, visited, recTysOut);
    }

    DEBUG(errs() << "Done scanning module.\n");

    DEBUG(
      errs() << "Found " << recTysOut.size()
             << " sane, recursive types with restricted field access:\n";
      for(Types::iterator i=recTysOut.begin(), e=recTysOut.end(); i!=e; ++i)
      {
        errs() << '\t';
        errs() << **i;
      }
      errs() << '\n';
    );
  }



  bool AcyclicAA::runOnModule(Module &mod)
  {
    DEBUG(errs() << "Begin AcyclicAA::runOnModule()\n");
    const DataLayout &DL = mod.getDataLayout();
    InitializeLoopAA(this, DL);
    currentModule = &mod;

    const TargetLibraryInfo &tli =
        getAnalysis<TargetLibraryInfoWrapperPass>().getTLI();

    // First, find all recursive types
    Types recTys;
    accumulateRecursiveTypes(recTys);

    // Next, check if each one is indeed acyclic.
    for(Types::iterator i=recTys.begin(), e=recTys.end(); i!=e; ++i)
    {
      Type *ty = *i;

      DEBUG(
        errs() << "Checking cyclicity of ";
        errs() << *ty;
        errs() << '\n';
      );

      if( proveAcyclic(ty, mod, tli) )
        acyclic.insert(ty);
    }

    numAcyclic += acyclic.size();

    DEBUG(
      errs() << "Found " << acyclic.size() << " acyclic types:\n";
      for(TypeSet::iterator i=acyclic.begin(), e=acyclic.end(); i!=e; ++i)
      {
        errs() << '\t';
        errs() << **i;
      }
      errs() << '\n';
    );

    //currentModule = 0;
    DEBUG(errs() << "End AcyclicAA::runOnModule()\n");
    return false;
  }

  void AcyclicAA::getAnalysisUsage(AnalysisUsage &au) const
  {
    LoopAA::getAnalysisUsage(au);
    au.addRequired<TypeSanityAnalysis>();
    au.addRequired<NonCapturedFieldsAnalysis>();
    au.setPreservesAll();
  }



}
