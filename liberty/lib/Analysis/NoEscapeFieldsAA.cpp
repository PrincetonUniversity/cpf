#define DEBUG_TYPE "typeaa"

#include "llvm/IR/LLVMContext.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/CallSite.h"
#include "llvm/IR/CFG.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/IR/DataLayout.h"

#include "NoEscapeFieldsAA.h"
#include "liberty/Analysis/LoopAA.h"
#include "liberty/Analysis/TypeSanity.h"
#include "liberty/Utilities/CallSiteFactory.h"
#include "liberty/Utilities/GetMemOper.h"
#include "liberty/Utilities/GetSize.h"

namespace liberty
{
  using namespace llvm;

  char NoEscapeFieldsAA::ID = 0;
  char NonCapturedFieldsAnalysis::ID = 0;
  namespace
  {
    static RegisterPass<NonCapturedFieldsAnalysis>
    Z("non-captured-fields-analysis",
      "Identify fields of sane types which are never captured");
    static RegisterPass<NoEscapeFieldsAA>
    X("no-escape-fields-aa",
      "Identify fields of sane types which are never captured",
      false, true);
    static RegisterAnalysisGroup<LoopAA> Y(X);
  }

  STATISTIC(numCapturedFields, "Number of fields which are captured");
  STATISTIC(numDefs, "Number of definitions for non-captured fields of sane types");
  STATISTIC(numTypesWithDefs, "Number of sane-types with field defs");

  void NonCapturedFieldsAnalysis::getAnalysisUsage(AnalysisUsage &au) const
  {
    au.addRequired<TypeSanityAnalysis>();
    au.setPreservesAll();
  }
  void NoEscapeFieldsAA::getAnalysisUsage(AnalysisUsage &au) const
  {
    LoopAA::getAnalysisUsage(au);
    au.addRequired<TypeSanityAnalysis>();
    au.addRequired<NonCapturedFieldsAnalysis>();
    au.setPreservesAll();
  }


  const ConstantInt *NonCapturedFieldsAnalysis::Const64(const ConstantInt *ci)
  {
    if( !ci )
      return 0;

    LLVMContext &ctx = ci->getContext();
    IntegerType *u64 = Type::getInt64Ty(ctx);
    if( ci->getType() != u64 )
      return ConstantInt::get(u64, ci->getLimitedValue());

    return ci;
  }

  void NonCapturedFieldsAnalysis::addDefinition(
    StructType *structty, const ConstantInt *fieldno, StoreInst *store)
  {
    Value *initor = store->getValueOperand();
    if( !initor->getType()->isPointerTy() )
      return;

    Def def(store, initor);
    ++numDefs;
    fieldDefinitions[structty][ Const64(fieldno) ].insert(def);
  }

  void NonCapturedFieldsAnalysis::addDefinition(
    StructType *structty, const ConstantInt *fieldno, GlobalVariable *gv, Constant *initor)
  {
    if( !initor->getType()->isPointerTy() )
      return;

    Def def(gv, initor);
    ++numDefs;
    fieldDefinitions[structty][ Const64(fieldno) ].insert(def);
  }

  void NonCapturedFieldsAnalysis::eraseDefinitions(
    StructType *structty, const ConstantInt *fieldno)
  {
    if( fieldno )
      fieldDefinitions[structty].erase( Const64(fieldno) );

    else
      fieldDefinitions.erase(structty);
  }

  void NonCapturedFieldsAnalysis::collectDefsFromGlobalVariable(GlobalVariable *gv)
  {
    PointerType *pointer_to_global = gv->getType();
    Type *ty = pointer_to_global->getElementType();

    if( gv->hasInitializer() )
      collectDefsFromGlobalVariable(gv, ty, gv->getInitializer() );
  }

  void NonCapturedFieldsAnalysis::collectDefsFromGlobalVariable(GlobalVariable *gv, Type *ty, Constant *initor)
  {
    if( StructType *structty = dyn_cast< StructType >(ty) )
    {
      LLVMContext &ctx = gv->getContext();
      IntegerType *u64 = Type::getInt64Ty(ctx);

      TypeSanityAnalysis &typeaa = getAnalysis<TypeSanityAnalysis>();
      if( typeaa.isSane(structty) )
        for(unsigned i=0, N=structty->getNumElements(); i<N; ++i)
          if( ! captured(structty,i) )
            addDefinition(structty,ConstantInt::get(u64,i),
              gv,initor->getAggregateElement(i));

      for(unsigned i=0, N=structty->getNumElements(); i<N; ++i)
        collectDefsFromGlobalVariable(
          gv, structty->getElementType(i), initor->getAggregateElement(i));
    }

    else if( ArrayType *arrty = dyn_cast< ArrayType >(ty) )
      for(unsigned i=0, N=arrty->getNumElements(); i<N; ++i)
        collectDefsFromGlobalVariable(
          gv, arrty->getElementType(), initor->getAggregateElement(i));

    else if( VectorType *vecty = dyn_cast< VectorType >(ty) )
      for(unsigned i=0, N=vecty->getNumElements(); i<N; ++i)
        collectDefsFromGlobalVariable(
          gv, vecty->getElementType(), initor->getAggregateElement(i));
  }

  bool NonCapturedFieldsAnalysis::findAllDefs(StructType *structty, const ConstantInt *fieldno, Defs &defs_out) const
  {
    if( captured(structty,fieldno) )
      return false;

    Struct2Field2Defs::const_iterator i = fieldDefinitions.find(structty);
    if( i == fieldDefinitions.end() )
      return false;
    const Field2Defs &f2d = i->second;

    if( fieldno )
    {
      uint64_t f = fieldno->getLimitedValue();
      if( ! structty->getElementType(f)->isPointerTy() )
        return false;

      Field2Defs::const_iterator j=f2d.find( Const64(fieldno) );
      if( j != f2d.end() )
        defs_out.insert(j->second.begin(), j->second.end());

      j = f2d.find(0);
      if( j != f2d.end() )
        defs_out.insert(j->second.begin(), j->second.end());
    }

    else
      for(Field2Defs::const_iterator j=f2d.begin(), z=f2d.end(); j!=z; ++j)
        defs_out.insert(j->second.begin(), j->second.end());

    return true;
  }

  // Determine if the ONLY users of this gep
  // are load and store instructions.  Said
  // another way, this ensures that all introduced
  // pointers to the recursive fields of a type
  // are immediately eliminated, and never escape
  // to other loads or stores.
  bool NonCapturedFieldsAnalysis::isSafeFieldPointer(Instruction *gep, StructType *structty, const ConstantInt *fieldno)
  {
    // This GEP computes a pointer to the
    // 'next' field of a recursive type.
    // Make sure that this pointer is
    // ONLY used by either a load or a store.
    // This way, we can identify a closed
    // set of updates to this data structure.
    for(Value::user_iterator j=gep->user_begin(), f=gep->user_end(); j!=f; ++j)
    {
      Value *use = *j;

      StoreInst *store = dyn_cast< StoreInst >(use);
      if( store && store->getValueOperand() == gep )
        return false; // don't store the pointer to memory.
      else if( store )
      {
        addDefinition(structty,fieldno, store);
        continue; // storing TO the pointer is okay
      }
      else if( isa< LoadInst >(use) )
        continue;
      // sot: handle case with gep and then bitcast
      else if ( isa< BitCastInst > (use))
        return isSafeFieldPointer(cast<Instruction>(use), structty,fieldno);
      else
        return false;
    }

    return true;
  }

  bool checkRawStructIndexing(const Value *val,
                              StructType **structBaseOut,
                              const ConstantInt **fieldnoOut) {
    if (!isa<Instruction>(val))
      return false;
    auto *I = cast<Instruction>(val);
    auto *F = I->getParent()->getParent();
    IntegerType *i64Ty = IntegerType::getInt64Ty(F->getContext());

    if (const GetElementPtrInst *gep = dyn_cast< GetElementPtrInst >(val) ) {

      // raw indexes only have one index
      if( gep->getNumIndices() != 1 )
        return false;

      // byte indexing, type should be i8*
      if ( gep->getPointerOperandType() != Type::getInt8PtrTy(F->getContext()) || gep->getType() != Type::getInt8PtrTy(F->getContext()))
        return false;

      auto* original_ptr = gep->getPointerOperand();

      for (const User *U : original_ptr->users()) {
        //Instruction *UserI = cast<Instruction>(U);
        if (const BitCastInst *bcI = dyn_cast<BitCastInst>(U)) {
          if (PointerType *dstT = dyn_cast<PointerType>(bcI->getDestTy())) {
            if (StructType *structTy =
                    dyn_cast<StructType>(dstT->getElementType())) {

              // get index. since we have i8*, will be byte offset. Check if
              // we have a field at this offset for the found struct
              User::const_op_iterator opi = gep->idx_begin();
              if (!isa<ConstantInt>(*opi))
                return false;
              uint64_t gepOffset = cast<ConstantInt>(*opi)->getZExtValue();

              auto *M = F->getParent();
              const DataLayout &DL = M->getDataLayout();
              const StructLayout *SL = DL.getStructLayout(structTy);
              unsigned elem = SL->getElementContainingOffset(gepOffset);
              // make sure the byte offset is at the start of a field
              if (SL->getElementOffset(elem) != gepOffset)
                continue;

              *structBaseOut = structTy;
              *fieldnoOut = ConstantInt::get(i64Ty, elem);
              return true;
            }
          }
        }
      }
    }
    else if (const auto *cI = dyn_cast<BitCastInst>(val) ) {
      if ( ! isa<PointerType>(cI->getDestTy()))
        return false;
      auto* original_ptr = cI->getOperand(0);
      // expects i8*
      if ( original_ptr->getType() != Type::getInt8PtrTy(F->getContext()))
        return false;

      // look if the original pointer is casted to a struct type
      // then check if the first element of the struct is the same type as the base of destTy of the bitcast
      for (User *U : original_ptr->users()) {
        //Instruction *UserI = cast<Instruction>(U);
        if (BitCastInst *bcI = dyn_cast<BitCastInst>(U)) {
          if (PointerType *dstT = dyn_cast<PointerType>(bcI->getDestTy())) {
            if (StructType *structTy =
                    dyn_cast<StructType>(dstT->getElementType())) {

              // Ziyang: make sure there is a first element
              // TODO: consider the logic here
              unsigned sz = structTy->getNumElements();
              if (sz == 0) continue;
              // check that the type of the first element of the struct matches
              // with the pointer type of the destTy of the bitcast
              if (structTy->getElementType(0) !=
                  cast<PointerType>(cI->getDestTy())->getElementType())
                continue;

              *structBaseOut = structTy;
              *fieldnoOut = ConstantInt::get(i64Ty, 0);
              return true;
            }
          }
        }
      }
    }
    return false;
  }

  bool NonCapturedFieldsAnalysis::isFieldPointer(
    const Value *value,
    StructType **structBaseOut,
    const ConstantInt **fieldnoOut,
    bool strict) const
  {
    TypeSanityAnalysis &typeaa = getAnalysis<TypeSanityAnalysis>();

    *structBaseOut = 0;
    *fieldnoOut = 0;

    // sot: check if raw indexing of struct with i8* type instead of struct
    // pointer
    if ( checkRawStructIndexing(value, structBaseOut, fieldnoOut) ) {
      if( typeaa.isSane(*structBaseOut) )
        return true;
    }

    const GetElementPtrInst *gep = dyn_cast< GetElementPtrInst >(value);
    if( !gep )
      return false;
    if( gep->getNumIndices() < 2 )
      return false;

    if( strict && gep->getNumIndices() > 2 )
      return false;

    PointerType *ptr = dyn_cast<PointerType>(gep->getPointerOperandType());
    if(!ptr) return false;

    *structBaseOut = dyn_cast< StructType >(
      ptr->getElementType());

    if( ! *structBaseOut )
      return false;

    if( ! typeaa.isSane(*structBaseOut) )
      return false;

    // Be as precise as possible when identifying
    // the field.
    User::const_op_iterator opi = gep->idx_begin();
    ++opi;
    *fieldnoOut = dyn_cast< ConstantInt >(*opi);

    return true;
  }

  void NonCapturedFieldsAnalysis::runOnFunction(Function &fcn)
  {
    for(inst_iterator i=inst_begin(fcn), e=inst_end(fcn); i!=e; ++i)
    {
      Instruction *inst = &*i;

      StructType *structBase=0;
      const ConstantInt *fieldno=0;
      if( !isFieldPointer(inst, &structBase, &fieldno) )
        continue;

      // If we already know it is captured.
      if( captured(structBase,fieldno) )
        continue;

      if( isSafeFieldPointer(inst,structBase,fieldno) )
        continue;

      // Found a case where a pointer to this field
      // is captured.  We must mark it as 'escaping'

      const unsigned sz = structBase->getNumElements();
      escapingFields[ structBase ].resize(sz);
      ++numCapturedFields;

      // Is field number known at compile time?
      if( fieldno )
      {
        // Yes, field number is a constant int.
        const uint64_t f = fieldno->getLimitedValue();
        escapingFields[ structBase ].set(f);
        eraseDefinitions(structBase,fieldno);

        DEBUG(
          errs() << "\t- Field #" << f << " captured ";
          errs() << *structBase;
        );
      }
      else
      {
        // No, field number is not analyzeable
        // We conservatively assume this is /ANY/
        // field.
        escapingFields[ structBase ].set();
        eraseDefinitions(structBase);

        DEBUG(
          errs() << "\t- All fields escape ";
          errs() << *structBase;
        );
      }
    }
  }

  bool NoEscapeFieldsAA::runOnModule(Module &mod)
  {
    const DataLayout *DL = &mod.getDataLayout();
    InitializeLoopAA(this, *DL);
    return false;
  }

  bool NonCapturedFieldsAnalysis::runOnModule(Module &mod)
  {
    DEBUG(errs() << "Begin NonCapturedFieldsAnalysis::runOnModule()\n");
    currentModule = &mod;

    typedef Module::iterator FI;
    for(FI i=mod.begin(), e=mod.end(); i!=e; ++i)
    {
      Function &fcn = *i;
      runOnFunction(fcn);
    }

    // Also look for definitions within global initializers.
    for(Module::global_iterator i=mod.global_begin(), e=mod.global_end(); i!=e; ++i)
    {
      GlobalVariable *gv = &*i;
      collectDefsFromGlobalVariable(gv);
    }
    numTypesWithDefs += fieldDefinitions.size();

    currentModule = 0;
    DEBUG(errs() << "End NonCapturedFieldsAnalysis::runOnModule()\n");
    return false;
  }

  bool NonCapturedFieldsAnalysis::captured(const GetElementPtrInst *gep) const
  {
    StructType *structBase=0;
    const ConstantInt *fieldno=0;
    if( !isFieldPointer(gep, &structBase, &fieldno) )
      assert( false && "Cannot digest this GEP");

    return captured(structBase, fieldno);
  }

  // You may pass fieldno == NULL, which means ANY field.
  bool NonCapturedFieldsAnalysis::captured(
    StructType *ty,
    const ConstantInt *fieldno) const
  {
    if( fieldno == 0 )
    {
      assert( ty
      && "This is not a gep into a STRUCTURE");

      TypeSanityAnalysis &typeaa = getAnalysis< TypeSanityAnalysis >();
      assert( typeaa.isSane(ty)
      && "This is not a sane type");

      // do ANY fields escape?
      CapturedFields::const_iterator i = escapingFields.find(ty);

      if( i == escapingFields.end() )
        return false;

      return i->second.any();
    }

    return captured(ty, fieldno->getLimitedValue());
  }

  bool NonCapturedFieldsAnalysis::captured(StructType *ty, uint64_t field) const
  {
    assert( ty
    && "This is not a gep into a STRUCTURE");

    TypeSanityAnalysis &typeaa = getAnalysis< TypeSanityAnalysis >();
    assert( typeaa.isSane(ty)
    && "This is not a sane type");

    CapturedFields::const_iterator i = escapingFields.find(ty);
    if( i == escapingFields.end() )
      return false;

    return i->second.test( field );
  }

  LoopAA::ModRefResult NoEscapeFieldsAA::callsiteTouchesNonEscapingField(
    CallSite cs,
    const Pointer &p2,
    StructType *struct2,
    const ConstantInt *field2)
  {
    LoopAA *top = getTopAA();
    NonCapturedFieldsAnalysis &ncfa = getAnalysis< NonCapturedFieldsAnalysis >();
    Function *callee = cs.getCalledFunction();

    if( !callee )
      return ModRef;

    // Don't repeat work.
    CallsiteTouches::iterator cacheline = callsiteTouches.find( cs.getInstruction() );
    if( cacheline != callsiteTouches.end() )
      return cacheline->second;

    // Avoid infinite recursion.
    // We will update this value before we return.
    callsiteTouches[ cs.getInstruction() ] = NoModRef;

    DEBUG(errs() << "callsiteTouchesNonEscapingField("
      << *cs.getInstruction()
      << ", " << *struct2 << "->" << *field2 << '\n');


    // Determine if any of the actuals at this callsite
    // may alias with the field.
    std::vector<Argument*> aliasArgs;
    Function::arg_iterator j=callee->arg_begin();
    for(CallSite::arg_iterator i=cs.arg_begin(), e=cs.arg_end(); i!=e; ++i, ++j)
    {
      StructType *actual_struct = 0;
      const ConstantInt *actual_field = 0;
      if( ncfa.isFieldPointer(*i, &actual_struct, &actual_field, true) )
      {
        if( actual_struct != struct2 )
          continue;
        if( actual_field != 0 && field2 != 0 && actual_field != field2 )
          continue;

        // This formal may alias with the pointer.
        DEBUG(errs() << "\tMay alias with formal " << *j << "(actual " << *i << ")\n");
        aliasArgs.push_back(&*j);
      }
    }

    // Scan the callee to see if it may mod-ref
    // the field or aliasing args.
    ModRefResult lowerBound = NoModRef;

    if( callee->isDeclaration() )
    {
      lowerBound = top->modref(cs.getInstruction(), Same, p2.ptr, p2.size, 0);
    }

    else
    {
      // For each operation in the callee which
      // may read or write memory.
      for(inst_iterator i=inst_begin(callee), e=inst_end(callee); i!=e; ++i)
      {
        Instruction *inst = &*i;

        if( !inst->mayReadFromMemory()
        &&  !inst->mayWriteToMemory() )
          continue;

        // This operation is either a callsite,
        // or it is another operation which reads/writes memory.

        // Case 1: callsite
        CallSite cs3 = getCallSite(inst);
        if( cs3.getInstruction() )
        {
          // recur.
          ModRefResult r = callsiteTouchesNonEscapingField(cs3,p2,struct2,field2);
          lowerBound = ModRefResult(lowerBound | r);
        }

        // Case 2: some other operation which reads/writes memory.
        else
        {
          const Value *ptr = liberty::getMemOper(inst);

          // Using NoEscapeFields separation reasoning,
          // can we say that this does not touch our field?
          StructType *struct3 = 0;
          const ConstantInt *field3 = 0;
          if( !ncfa.isFieldPointer(ptr,&struct3,&field3,true)
          ||  struct2 != struct3
          ||  (field2 != 0 && field3 != 0 && field2 != field3) )
          {
            // This does not alias with the field.

            // Maybe it aliases with the formal parameters which may (in turn) alias with the field?
            for(std::vector<Argument*>::iterator k=aliasArgs.begin(),g=aliasArgs.end(); k!=g; ++k)
            {
              unsigned size = liberty::getTargetSize(*k,getDataLayout());
              ModRefResult r = top->modref(inst, Same, *k, size, 0);
              lowerBound = ModRefResult(lowerBound | r);

              if( lowerBound == ModRef )
                break;
            }
          }

          // We can't say jack about this.
          else
          {
            if( inst->mayWriteToMemory() )
              lowerBound = ModRefResult( lowerBound | Mod );
            if( inst->mayReadFromMemory() )
              lowerBound = ModRefResult( lowerBound | Ref );
          }

        }

        // Break early if the lower bound can't get worse.
        if( lowerBound == ModRef )
          break;
      }
    }

    // update the cache.
    callsiteTouches[ cs.getInstruction() ] = lowerBound;

    DEBUG(errs() << "/callsiteTouchesNonEscapingField("
      << *cs.getInstruction()
      << ", " << *struct2 << " # " << *field2
      << " => " << lowerBound << '\n');


    return lowerBound;
  }

  LoopAA::ModRefResult NoEscapeFieldsAA::getModRefInfo(
    CallSite cs,
    TemporalRelation rel,
    CallSite cs2,
    const Loop *L)
  {
    // I don't handle this case.
    return ModRef;
  }

  LoopAA::ModRefResult NoEscapeFieldsAA::getModRefInfo(
    CallSite cs,
    TemporalRelation rel,
    const Pointer &p2,
    const Loop *L)
  {
    DEBUG_WITH_TYPE("loopaa", errs() << "NoEscapeFieldsAA\n");

    Function *callee = cs.getCalledFunction();
    if( !callee )
      return ModRef;
    else if( callee->isDeclaration() )
      return ModRef;

    StructType *struct2 = 0;
    const ConstantInt *field2 = 0;
    NonCapturedFieldsAnalysis &ncfa = getAnalysis< NonCapturedFieldsAnalysis >();
    const bool fp2 = ncfa.isFieldPointer(p2.ptr, &struct2, &field2, true);

    if( fp2 && !ncfa.captured(struct2,field2) )
    {
      // Check if the callsite or any of it's
      // transitive callees touch this field
      // of this type.

      return callsiteTouchesNonEscapingField(cs,p2,struct2,field2);
    }

    return ModRef;
  }

  LoopAA::AliasResult NoEscapeFieldsAA::aliasCheck(
    const Pointer &P1,
    TemporalRelation rel,
    const Pointer &P2,
    const Loop *L)
  {
    DEBUG_WITH_TYPE("loopaa", errs() << "NoEscapeFieldsAA\n");

    NonCapturedFieldsAnalysis &ncfa = getAnalysis< NonCapturedFieldsAnalysis >();

    StructType *struct1=0;
    const ConstantInt *field1=0;
    const bool fp1 = ncfa.isFieldPointer(P1.ptr, &struct1, &field1, true);

    StructType *struct2=0;
    const ConstantInt *field2=0;
    const bool fp2 = ncfa.isFieldPointer(P2.ptr, &struct2, &field2, true);

    if( fp1 && fp2 )
    {
      if( !ncfa.captured(struct1,field1)
      ||  !ncfa.captured(struct2,field2) )
      {
        // Different (sane) structures?
        if( struct1 != struct2 )
          // You might ask: what if struct2 is struct1:field1 (or vice-versa)
          // This is impossible; that would imply that a reference to struct1:field1
          // escaped.
          return NoAlias;
        // Same (sane) structure, different fields?
        else if( field1 != 0 && field1 != field2 && field2 != 0 )
          return NoAlias;
        // Same (sane) structure, same fields?
        else if( field1 != 0 && field1 == field2 )
        {
          // fp1==true implies isa<GEP>(V1), so cast<> is safe.
          const Value *parent1 = cast<GetElementPtrInst>(P1.ptr)->getPointerOperand(),
                      *parent2 = cast<GetElementPtrInst>(P2.ptr)->getPointerOperand();

          const unsigned s1 = getDataLayout()->getTypeSizeInBits( struct1 ) / 8,
                         s2 = getDataLayout()->getTypeSizeInBits( struct2 ) / 8;

          // The same fields-within the same type alias
          // only if the parent structures alias.  We recur into
          // alias analysis to answer this question.
          LoopAA *top = getTopAA();
          DEBUG_WITH_TYPE("loopaa", errs() << "(recur)\n");
          if( top->alias(parent1,s1, rel, parent2,s2, L) == NoAlias )
          {
            DEBUG_WITH_TYPE("loopaa", errs() << "(end recur)\n");
            return NoAlias;
          }
          DEBUG_WITH_TYPE("loopaa", errs() << "(end recur)\n");
        }
      }
    }

    else if( fp1 && !fp2 )
    {
      if( !ncfa.captured(struct1,field1) )
        return NoAlias;
    }

    else if( !fp1 && fp2 )
    {
      if( !ncfa.captured(struct2,field2) )
        return NoAlias;
    }

    // chain to lower analyses
    return MayAlias;
  }

}
