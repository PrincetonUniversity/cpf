#define DEBUG_TYPE "typeaa"

#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/CallSite.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/GetElementPtrTypeIterator.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DebugLoc.h"

#include "liberty/Analysis/LoopAA.h"
#include "liberty/Analysis/TypeSanity.h"
#include "liberty/Utilities/CallSiteFactory.h"
#include "liberty/Utilities/Count.h"
#include "liberty/Utilities/GetSize.h"


namespace liberty
{
  using namespace llvm;

  /* Adaptor between TypeSanityAnalysis and LoopAA.
   */
  class TypeAA : public ModulePass, public ClassicLoopAA {
    typedef std::vector<const Value*>   Values;
    typedef DenseSet<const Value*> ValueSet;

    static void findDefs(const Value *v, Values &defsOut, ValueSet &noInfiniteLoops);
    static void findDefs(const Value *v, Values &defsOut);


  public:
    static char ID;
    TypeAA()
      : ModulePass(ID), ClassicLoopAA() {}

    void getAnalysisUsage(AnalysisUsage &au) const
    {
      LoopAA::getAnalysisUsage(au);
      au.addRequired< TypeSanityAnalysis >();
      au.setPreservesAll();
    }

    /// getAdjustedAnalysisPointer - This method is used when a pass implements
    /// an analysis interface through multiple inheritance.  If needed, it
    /// should override this to adjust the this pointer as needed for the
    /// specified pass info.
    virtual void *getAdjustedAnalysisPointer(AnalysisID PI) {
      if (PI == &LoopAA::ID)
        return (LoopAA*)this;
      return this;
    }

    bool runOnModule(Module &);

    StringRef getPassName() const
    {
      return "Type-sanity Alias analysis";
    }

    StringRef getLoopAAName() const { return "TypeAA"; }

    virtual AliasResult aliasCheck(
      const Pointer &P1,
      TemporalRelation rel,
      const Pointer &P2,
      const Loop *L);
  };

  char TypeAA::ID = 0;
  char TypeSanityAnalysis::ID = 0;
  namespace
  {
    RegisterPass<TypeSanityAnalysis> z("type-sanity",
      "Identify sane usage of types");
    RegisterPass<TypeAA> x("type-aa",
      "Use Sane Typing Arguments for AA", false, true);
    RegisterAnalysisGroup<liberty::LoopAA> y(x);
  }

  STATISTIC(numInsane,    "Number of insane types identified.");
  STATISTIC(numNoAliases, "Number of no-alias results given because of sane typing.");
  STATISTIC(numQueries,   "Number of AA queries passed to TypeAA.");

  bool TypeAA::runOnModule(Module &mod)
  {
    const DataLayout &DL = mod.getDataLayout();
    InitializeLoopAA(this, DL);
    return false;
  }

  bool TypeSanityAnalysis::runOnModule(Module &mod)
  {
    DEBUG(errs() << "Begin TypeAA::runOnModule()\n");

    currentMod = &mod;

    // For each function in this module
    typedef Module::iterator FI;
    for(FI i=mod.begin(), e=mod.end(); i!=e; ++i)
    {
      Function &fcn = *i;

      runOnFunction(fcn);
    }

    // For each global variable in this module
    typedef Module::global_iterator GI;
    for(GI i=mod.global_begin(), e=mod.global_end(); i!=e; ++i)
    {
      GlobalVariable &gv = *i;

      runOnGlobalVariable(gv);
    }

    currentMod = 0;
    DEBUG(errs() << "End TypeAA::runOnModule()\n");
    return false;
  }

  static bool isCallToMalloc(Value *src)
  {
    CallSite cs = getCallSite(src);
    if( !cs.getInstruction() )
      return false;

    Function *f = cs.getCalledFunction();
    if( !f )
      return false;

    if(  f->returnDoesNotAlias() )
      return true;

    StringRef  name = f->getName();
    if( name == "malloc" )
      return true;

    if( name == "calloc" )
      return true;

    if( name == "realloc" )
      return true;

    return false;
  }

  static bool isCallToFree(Value *use)
  {
    CallSite cs = getCallSite(use);
    if( !cs.getInstruction() )
      return false;

    Function *f = cs.getCalledFunction();
    if( !f )
      return false;

    StringRef  name = f->getName();
    if( name == "realloc" )
      return true;

    if( name == "free" )
      return true;

    return false;
  }

  static bool isCmpInst(Value *use)
  {
    if( isa<CmpInst>(use) )
      return true;

    PHINode *phi = dyn_cast<PHINode>(use);
    if( !phi )
      return false;
    if( phi->getNumOperands() != 1)
      return false;
    if( phi->getNumUses() != 1)
      return false;

    if( !isa<CmpInst>(*phi->user_begin()) )
      return false;

    return true;
  }

  //sot
  static bool isRelaxedCmpInst(Value *use)
  {
    if( isa<CmpInst>(use) )
      return true;

    PHINode *phi = dyn_cast<PHINode>(use);
    if( !phi )
      return false;
    if( phi->getNumOperands() != 1)
      return false;
    if( phi->getNumUses() != 1)
      return false;

    if( isa<CmpInst>(*phi->user_begin()) )
      return true;

    if( PHINode *next_phi = dyn_cast<PHINode>(*phi->user_begin()) ) {
      if( next_phi->getNumUses() != 1)
        return false;
      if ( isCallToFree(*next_phi->user_begin()) )
        return true;
    }

    return false;
  }

  static bool isCastInst(Value *use)
  {
    if( isa<CastInst>(use) )
      return true;
    return false;
  }

  static bool isGEPInst(Value *use)
  {
    if( isa<GetElementPtrInst>(use) )
      return true;
    return false;
  }


  static bool isSingleCastFromMalloc(Instruction *inst)
  {
    Value *src = inst->getOperand(0);

    unsigned cmpCount =
      liberty::count(isCmpInst, src->user_begin(), src->user_end());
      //liberty::count<CmpInst>(src->user_begin(), src->user_end());

    unsigned freeCount =
      liberty::count(isCallToFree, src->user_begin(), src->user_end());

    // Comparing with NULL or other pointers should not pessimize analysis
    // results. Free a void * is also safe.
    if( src->getNumUses() - cmpCount - freeCount != 1 ) {
      DEBUG(errs() << "Use count not equal to 1\n");
      return false;
    }

    if( isCallToMalloc(src) )
      return true;

    DEBUG(errs() << "Wasn't considered a call to malloc\n");
    return false;
  }

  // TODO: maybe this is too relaxed!
  // sot: handle case with malloc and bitcast after gep
  //      and in general cases where the result of malloc is
  //      not bitcasted immediately.
  //
  //e.g.  %call2 = tail call noalias i8* @malloc(i64 24)
  //      %next = getelementptr inbounds i8, i8* %call2, i64 16
  //      %2 = bitcast i8* %next to %struct._node_t**
  // instead of having
  //      %1 = bitcast i8* %call2 to %struct._node_t*
  //      %next = getelementptr inbounds %struct._node_t, %struct._node_t* %1, ...
  static bool isCastFromMalloc(Instruction *inst)
  {
    Value *src = inst->getOperand(0);

    // handle example mentioned above
    if (GetElementPtrInst *gep = dyn_cast<GetElementPtrInst>( src )) {
      src = gep->getPointerOperand();
    }

    if( !isCallToMalloc(src) ) {
      DEBUG(errs() << "Wasn't considered a call to malloc\n");
      return false;
    }

    unsigned cmpCount =
      liberty::count(isRelaxedCmpInst, src->user_begin(), src->user_end());
      //liberty::count<CmpInst>(src->user_begin(), src->user_end());

    unsigned freeCount =
      liberty::count(isCallToFree, src->user_begin(), src->user_end());

    // in some cases the return value of malloc could be casted to two
    // different types. If you use malloc for a struct you might have one
    // casting for the struct pointer type and one cast for the type of the
    // first element of the array
    unsigned castCount =
      liberty::count(isCastInst, src->user_begin(), src->user_end());

    if (castCount > 2) {
      DEBUG(errs() << "More than 2 casts for the malloc result\n");
      return false;
    }
    --castCount; // dont count the current cast inst we are examining

    // allow gep that received uncasted results of malloc
    unsigned gepCount =
      liberty::count(isGEPInst, src->user_begin(), src->user_end());

    if (src->getNumUses() - cmpCount - freeCount - castCount - gepCount != 1) {
      DEBUG(errs() << "Use count not equal to 1\n");
      return false;
    }

    return true;
  }

  static bool isSingleCastToFree(Instruction *inst)
  {
    if( !inst->hasOneUse() )
      return false;

    Value *use = *inst->user_begin();

    // sot. begin
    if( PHINode *phi = dyn_cast<PHINode>(use) ) {
      if( phi->getNumUses() != 1)
        return false;
      if ( isCallToFree(*phi->user_begin()) )
        return true;
    }
    // sot. end

    if( isCallToFree(use) )
      return true;

    return false;
  }

  static bool isSingleCastToIntrinsic(const Instruction *inst)
  {
    if( !inst->hasOneUse() )
      return false;

    const Value *use = *inst->user_begin();

    if ( !isa<MemIntrinsic>(use) )
      return false;

    return true;
  }

  bool TypeSanityAnalysis::addInsane(Type *t)
  {
    // Don't bother with some of the most basic types
    if( t->isVoidTy() ||
        t->isFloatingPointTy() ||
        t->isLabelTy() ||
        t->isMetadataTy() ||
        t->isX86_MMXTy() ||
        t->isIntegerTy())
      return false;

    // Don't bother with the ones we have seen already.
    if( insane.count(t) )
      return false;

    DEBUG(
      errs() << "\tInsane: ";
      errs() << *t << "\n";
    );

    insane.insert(t);
    ++numInsane;

    // Is this made of other types?

    // Sequential types (array, pointer, vector)
    SequentialType *seq = dyn_cast<SequentialType>(t);
    if( seq )
      addInsane( seq->getElementType() );

    // Composite type (including struct, union)
    CompositeType *composite = dyn_cast<CompositeType>(t);
    if( composite && !seq )
      for(unsigned idx=0; composite->indexValid(idx); ++idx)
        addInsane( composite->getTypeAtIndex(idx) );

    // Function type
    FunctionType *function = dyn_cast<FunctionType>(t);
    if( function )
    {
      addInsane( function->getReturnType() );
      for(unsigned idx=0; idx<function->getNumParams(); ++idx)
        addInsane( function->getParamType(idx) );
    }

    return true;
  }

  // Determine if the global variable is private to this module.
  // In particular, I mean that code in a different module
  // will not access or call this.
  static bool isPrivate(GlobalValue &gv)
  {
    // According to C99, no one is allowed to call main!
    if( isa<Function>(gv) && gv.getName() == "main" )
      return true;

    GlobalValue::LinkageTypes lt = gv.getLinkage();
    return lt == GlobalValue::InternalLinkage
        || lt == GlobalValue::PrivateLinkage;
  }

  void TypeSanityAnalysis::runOnGlobalVariable(GlobalVariable &gv)
  {
    if( !FULL_UNIVERSAL )
      // Conservatively handle any global variable which
      // may expose a type to external code.
      if( !isPrivate( gv ) )
        if( addInsane( gv.getType() ) )
          DEBUG_WITH_TYPE("recommendation",
            errs() << "Marking the global variable " << gv.getName()
                   << " as 'static' would improve analysis\n");
  }

  static bool isStupidSimpleCase(Function &fcn)
  {
    const StringRef  name = fcn.getName();
    return name == "llvm.dbg.declare"
    ||     name == "llvm.dbg.value"
    ||     name == "llvm.lifetime.start"
    ||     name == "llvm.lifetime.start.p0i8"
    ||     name == "llvm.lifetime.end"
    ||     name == "llvm.lifetime.end.p0i8"
    ||     name == "llvm.invariant.start"
    ||     name == "llvm.invariant.start.p0i8"
    ||     name == "llvm.invariant.end"
    ||     name == "llvm.invariant.end.p0i8"
    ||     name == "llvm.var.annotation"
    ||     name == "llvm.annotation.i8"
    ||     name == "llvm.annotation.i16"
    ||     name == "llvm.annotation.i32"
    ||     name == "llvm.annotation.i64"
    ||     name == "llvm.objectsize.i32"
    ||     name == "llvm.objectsize.i64";
  }

  bool TypeSanityAnalysis::runOnFunction(Function &fcn)
  {
    // Conservatively handle external functions,
    // or functions which may be called externally.
    if( fcn.isDeclaration() || (!FULL_UNIVERSAL && !isPrivate( fcn )) )
    {
      if( !isStupidSimpleCase(fcn) )
      {
        for(Function::arg_iterator i=fcn.arg_begin(), e=fcn.arg_end(); i!=e; ++i)
        {
          DEBUG(errs() << "Conservative 1: " << fcn.getName() << " :: " <<  *fcn.getType() << '\n');
          addInsane( i->getType() );
        }
      }
    }

    // Conservatively handle external functions.
    if( fcn.isDeclaration() )
    {
      if( !isStupidSimpleCase(fcn) )
      {
        DEBUG(errs() << "Conservative 2: " << fcn.getName() << " :: " << *fcn.getType() << '\n');
        addInsane( fcn.getReturnType() );
      }
    }

    for(inst_iterator i=inst_begin(fcn), e=inst_end(fcn); i!=e; ++i)
    {
      Instruction *inst = &*i;
      CallSite cs = getCallSite(inst);

      if( isa<CastInst>( inst ) )
      {
        if( isSingleCastFromMalloc( inst ) )
        {
          // We let casting from malloc slide.
          DEBUG(errs() << "Is a single cast from malloc\n");
        }
        // sot
        else if( isCastFromMalloc ( inst ) )
        {
          // sot: We let slide late casting of someone originating from malloc
          //TODO: maybe this makes the analysis more unsound that we would like
          DEBUG(errs() << "Inst " <<  *inst << "is a cast originating from malloc\n");
        }
        else if( isSingleCastToFree( inst ) )
        {
          // We let casting to free slide.
        }
        else if( isSingleCastToIntrinsic( inst ) )
        {
          // We let casting to Intrinsics slide, since llvm introduces them.
        }
        else
        {
          // This is a cast from type ta to type tb
          Type *ta = inst->getOperand(0)->getType();
          Type *tb = inst->getType();

          DEBUG(errs() << "Cast: " << *inst
                       << " at " << fcn.getName()
                       << ':' << inst->getParent()->getName() << '\n');

          addInsane(ta);
          addInsane(tb);
        }
      }

      else if( isa<VAArgInst>( inst ) )
      {
        DEBUG(errs() << "VAArg: " << *inst << '\n');
        // There is no way to verify that the i-th
        // parameter is the right type.
        addInsane( inst->getType() );
      }

      else if( cs.getInstruction()
      &&       cs.getCalledFunction()
      &&       cs.getCalledFunction()->isVarArg() )
      {
        FunctionType *fty = cs.getCalledFunction()->getFunctionType();

        for(unsigned j=fty->getNumParams(); j<cs.arg_size(); ++j)
        {
          DEBUG(errs() << "Variadic call: " << *inst << '\n');
          Type *pty = cs.getArgument(j)->getType();
          if( addInsane(pty) )
          {
            DEBUG_WITH_TYPE("recommendation",
              errs() << "Passing argument " << *cs.getArgument(j)
                     << " to the variadic function " << cs.getCalledFunction()->getName();
              const DebugLoc &loc = inst->getDebugLoc();
              if( loc )
                errs() << " (at line " << loc.getLine() << ')';
              errs() << " is bad for analysis\n";);
          }
        }
      }

    }
    return false;
  }

  bool TypeSanityAnalysis::isSane(Type *t) const
  {
    if( t->isVoidTy() ||
        t->isFloatingPointTy() ||
        t->isLabelTy() ||
        t->isMetadataTy() ||
        t->isX86_MMXTy() ||
        t->isIntegerTy())
      return false;

    else
      return !insane.count(t);
  }

  // flatten arrays-of/pointers-to/vectors-of to the element types, recursively.
  Type *TypeSanityAnalysis::getBaseType(Type *t)
  {
    SequentialType *seq = dyn_cast<SequentialType>( t );
    if( seq )
      return getBaseType(seq->getElementType());
    else
      return t;
  }

  static bool definitelyDifferent(const Value *v1, const Value *v2)
  {
    const ConstantInt *c1 = dyn_cast<ConstantInt>(v1),
                      *c2 = dyn_cast<ConstantInt>(v2);

    if( c1 && c2 )
      if( c1->getLimitedValue() != c2->getLimitedValue() )
        return true;

    return false;
  }

  static bool definitelySame(const Value *v1, const Value *v2)
  {
    if( v1 == v2 )
      return true;

    const ConstantInt *c1 = dyn_cast<ConstantInt>(v1),
                      *c2 = dyn_cast<ConstantInt>(v2);

    if( c1 && c2 )
      if( c1->getLimitedValue() == c2->getLimitedValue() )
        return true;

    return false;
  }

  // Determine if the type 'element' could possibly
  // occupy space within an allocation unit of type
  // 'container'
  bool TypeSanityAnalysis::typeContainedWithin(Type *container, Type *element) const
  {
    if( container == element )
      return true;

    if( !isSane(container) )
      return true; // conservative

    StructType *structty = dyn_cast< StructType >(container);
    if( structty )
      for(unsigned i=0; i<structty->getNumElements(); ++i)
        if( typeContainedWithin( structty->getElementType(i), element) )
          return true;

    // Don't need to handle union types, because
    // union types are by definition not sane,
    // hence they were handled earlier.

    // Don't need to handle pointer types, because
    // following those would take us to a different
    // allocation unit.
    //  (i.e. access to type int** only accesses the
    //   pointer storage location, but doesn't access
    //   the int).

    // But, we DO need to handle array and vector
    // types, since those lay out their contents
    // in contiguous memory.
    //  (i.e. access to type int[] may access all of the
    //   elements of the array)
    SequentialType *seqty = dyn_cast< SequentialType >(container);
    if( seqty )
      if( isa<ArrayType>(seqty) || isa<VectorType>(seqty) )
        if( typeContainedWithin(seqty->getElementType(), element) )
          return true;

    return false;
  }

  void TypeAA::findDefs(const Value *v, Values &defsOut, ValueSet &noInfiniteLoops)
  {
    if( noInfiniteLoops.count(v) )
      return;
    noInfiniteLoops.insert(v);

    if( const PHINode *phi = dyn_cast<PHINode>(v) )
    {
      for(unsigned i=0; i<phi->getNumIncomingValues(); ++i)
        findDefs(phi->getIncomingValue(i), defsOut, noInfiniteLoops);
    }

    defsOut.push_back(v);
  }

  void TypeAA::findDefs(const Value *v, Values &defsOut)
  {
    ValueSet visited;
    findDefs(v,defsOut,visited);
  }



  LoopAA::AliasResult TypeAA::aliasCheck(
    const Pointer &P1,
    TemporalRelation rel,
    const Pointer &P2,
    const Loop *L)
  {
    DEBUG_WITH_TYPE("loopaa", errs() << "TypeAA\n");
    ++numQueries;

    const Value *V1 = P1.ptr,
                *V2 = P2.ptr;

    PointerType *PT1 = dyn_cast<PointerType>( V1->getType() ),
                      *PT2 = dyn_cast<PointerType>( V2->getType() );
    if( !PT1 || !PT2 )
      return NoAlias;

    Type *elt1 = PT1->getElementType(),
               *elt2 = PT2->getElementType();

    TypeSanityAnalysis &tsa = getAnalysis< TypeSanityAnalysis >();

    // This first test uses knowledge that
    // sane types may not be mistaken for
    // other types, and knowledge that
    // structures/arrays/vectors are
    // allocated as a single allocation unit.

    // Specifically, we are checking if
    // anything in an allocation unit of type elt1
    // could be interpretted as an object
    // of elt2.  We will recursively search
    // structures and arrays, stopping at
    // insane types or pointers.
    if( !tsa.typeContainedWithin(elt1, elt2)
    &&  !tsa.typeContainedWithin(elt2, elt1) )
    {
      ++numNoAliases;
      return NoAlias;
    }

    // This second test uses knowledge that
    // objects of a sane type do not overlap
    // (except, obviously, for containment).
    // I.e. if we have two structures of
    // type Sane, either they are identical
    // or they are disjoint.
    // If we have two arrays of type Sane[],
    // either they start at the same address
    // or they are disjoint.

    // Specifically, we look for pointers
    // which are GEP instructions.
    Values def1, def2;
    findDefs(V1,def1);
    findDefs(V2,def2);

    // For each possible pair (di,dj) of definition of V1, V2
    for(Values::iterator i=def1.begin(), e=def1.end(); i!=e; ++i)
    {
      const Value *di = *i;
      const GetElementPtrInst *gep_i = dyn_cast<GetElementPtrInst>(di);
      if( !gep_i )
        return MayAlias;

      const Value *parent_i = gep_i->getPointerOperand();
      Type *parentty_i =
        dyn_cast<PointerType>(parent_i->getType())->getElementType();
      if( !parentty_i || ! tsa.isSane(parentty_i) )
        return MayAlias;

      for(Values::iterator j=def2.begin(), f=def2.end(); j!=f; ++j)
      {
        const Value *dj = *j;
        const GetElementPtrInst *gep_j = dyn_cast<GetElementPtrInst>( dj );
        if( !gep_j )
          return MayAlias;

        const Value *parent_j = gep_j->getPointerOperand();
        Type *parentty_j =
          dyn_cast<PointerType>(parent_j->getType())->getElementType();
        if( !parentty_j || ! tsa.isSane( parentty_j ) )
          return MayAlias;

        // Cool, both definitions are GEPs into sane types.
        // Either they are the same type, or different type.

        if( parentty_i == parentty_j )
        {
          // The two allocation units are of the same sane type.

          // They both index into an object of the same
          // sane type.

          // Either they access different fields, or the same field.
          // We cannot decide for sure, so we approximate that
          // with definitely-different or definitely-same:
          bool definitelyDifferentIndices = false;
          bool definitelySameIndices = true;
          for(User::const_op_iterator p=gep_i->idx_begin(),
                                      q=gep_j->idx_begin(),
                                      w=gep_i->idx_end(),
                                      z=gep_j->idx_end();
            p!=w && q!=z; ++p, ++q)
          {
            if( definitelyDifferent(*p, *q) )
              definitelyDifferentIndices = true;

            if( !definitelySame(*p, *q) )
              definitelySameIndices = false;
          }

          if( definitelyDifferentIndices )
          {
            // This is a good, easy case.
            // The only way that different fields could
            // overlap is if the sane types which contain
            // them overlap.  Contraction, thus no alias.
            continue;
          }

          else if( definitelySameIndices )
          {
            unsigned sz_i = getDataLayout()->getTypeSizeInBits(parentty_i) / 8;
            unsigned sz_j = getDataLayout()->getTypeSizeInBits(parentty_j) / 8;

            // Same structure type, same field within the allocation unit.
            // Then these pointers alias IFF the allocation units alias.
            // Or, if the allocation units do not alias, then these
            // fields cannot alias.
            LoopAA *top = getTopAA();
            DEBUG_WITH_TYPE("loopaa", errs() << "(recur)\n");
            if(parent_i == V1 && parent_j == V2)
              return MayAlias;


            if( top->alias(parent_i, sz_i, rel, parent_j, sz_j, L) == NoAlias )
            {
              DEBUG_WITH_TYPE("loopaa", errs() << "(end recur)\n");
              // Good.
              continue;
            }
            else
            {
              DEBUG_WITH_TYPE("loopaa", errs() << "(end recur)\n");
              return MayAlias;
            }
          }

          else
            return MayAlias;
        }

        else
        {
          // The two allocation units are of different sane types.

          // They may only alias according to the containment
          // rule
          if( !tsa.typeContainedWithin(parentty_i, parentty_j)
          &&  !tsa.typeContainedWithin(parentty_j, parentty_i) )
          {
            // Good, the allocation units cannot alias
            // and so fields within cannot alias.
            continue;
          }
          else
            return MayAlias;
        }

        // We could not prove that def_i, def_j do not alias.
        return MayAlias;
      }
    }

    ++numNoAliases;
    return NoAlias;
  }

}
