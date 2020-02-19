// De-virtualization.
// Replace indirect function calls (calls through a pointer)
// with a switch among possible callees.
// Plays nicely with control speculation.

#define DEBUG_TYPE "devirtualize-analysis"

#include "liberty/Analysis/Devirtualize.h"
#include "liberty/Analysis/NoCaptureFcn.h"
#include "liberty/Analysis/TraceData.h"

#include "llvm/IR/Operator.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"

#include "liberty/Analysis/TypeSanity.h"
#include "liberty/Utilities/FindUnderlyingObjects.h"

#include "NoEscapeFieldsAA.h"

namespace liberty
{
using namespace llvm;

// ------------------------- analysis

STATISTIC(numFlowAnalyzed,  "Num indirect calls for which flow-sensitive analysis succeeded.");
STATISTIC(savingsFromFlow,  "Num alternatives removed using flow-sensitive search.");
STATISTIC(numRecognizedLoadFromTable, "Num load-from-constant-table-by-index idioms recognized.");

STATISTIC(numTraceIntSuccess,               "Num successful invocations of traceConcreteIntegerValues()");
STATISTIC(savingsFromTraceInt,              "Num alternatives removed using traceConcreteIntegerValues.");

void DevirtualizationAnalysis::getAnalysisUsage(AnalysisUsage &au) const
{
  au.addRequired< TypeSanityAnalysis >();
  au.addRequired< NonCapturedFieldsAnalysis >();
  au.addRequired< NoCaptureFcn >();
  au.setPreservesAll();
}

bool DevirtualizationAnalysis::runOnModule(Module &mod)
{
  studyModule(mod);
  return false;
}

void DevirtualizationAnalysis::studyModule(Module &mod)
{
  // Find list of candidate callees for each callsite.
  unsigned numS = 0;
  for(Module::iterator i=mod.begin(), e=mod.end(); i!=e; ++i, ++numS)
  {
    studyFunction(&*i);
    LLVM_DEBUG(
      if( (numS % 25) == 0 )
        errs() << "Studied " << numS << " / " << mod.size() << " functions.\n";
    );
  }

  LLVM_DEBUG(errs() << "There are " << candidates.size()
               << " callsites to transform.\n");
}

void DevirtualizationAnalysis::studyFunction(Function *fcn)
{
  for(Function::iterator i=fcn->begin(), e=fcn->end(); i!=e; ++i)
  {
    BasicBlock *bb = &*i;
    for(BasicBlock::iterator j=bb->begin(), z=bb->end(); j!=z; ++j)
    {
      Instruction *inst = &*j;

      CallSite cs = getCallSite(inst);
      if( !cs.getInstruction() )
        continue; // not a call

      Value *fcn_ptr = cs.getCalledValue();
      if( isa<Constant>(fcn_ptr) )
        continue; // direct function call.

      if( CallInst *call = dyn_cast< CallInst >(inst) )
        if( call->isInlineAsm() )
          continue; // wtf!

      //sot
      const DataLayout &DL = fcn->getParent()->getDataLayout();

      // indirect function call.
      studyCallSite(cs, candidates[inst], DL);
    }
  }
}

bool DevirtualizationAnalysis::areTypesWeaklyCompatible(Function *fcn, CallSite &cs)
{
  // According to the C-spec, section 6.5.2.2
  // - calling a function with mismatched argument types is undefined (point 9).
  // - calling a function with wrong number of arguments is undefined (point 6).
  FunctionType *fty = fcn->getFunctionType();

  // Too few arguments?
  if( fty->getNumParams() > cs.arg_size() )
    return false;

  // Wrong types of args?
  bool match = true;
  CallSite::arg_iterator cs_arg_it = cs.arg_begin();
  for(FunctionType::param_iterator j=fty->param_begin(), z=fty->param_end(); j!=z && match; ++j, ++cs_arg_it)
  {
    Type *formal_type = *j;
    Value *actual_parameter = *cs_arg_it;

    if( ! areStructurallyEquivalentTransitively(formal_type, actual_parameter->getType()) )
      return false;
  }

  // If return types don't match, then
  // incorrectly interpreting the result
  // would be undefined.
  Instruction *call = cs.getInstruction();
  if( !call->use_empty() )
    if( ! areStructurallyEquivalentTransitively(fty->getReturnType(), call->getType()) )
      return false;

  return true;
}

bool DevirtualizationAnalysis::areTypesStrictlyCompatible(Function *fcn, CallSite &cs)
{
  FunctionType *fty = fcn->getFunctionType();

  if( !areTypesWeaklyCompatible(fcn,cs) )
    return false;

  // Wrong number of args?
  if( fty->getNumParams() < cs.arg_size() && ! fty->isVarArg() )
    return false;

  return true;
}

void DevirtualizationAnalysis::studyCallSite(CallSite &cs, Strategy &output,
                                            const DataLayout &DL)
{
  if( recognizeLoadFromConstantTableIdiom(cs,output) )
    return;

  output.dispatch = Strategy::CompareAndBranch;
  output.callees.clear();

  // Flow-sensitive, partially field-/element-sensitive analysis
  // to track function pointers.
  const NoCaptureFcn &nocap = getAnalysis< NoCaptureFcn >();
  const NonCapturedFieldsAnalysis &noescape = getAnalysis< NonCapturedFieldsAnalysis >();
  Tracer tracer(nocap,noescape);
  Tracer::FcnList flow;
  bool flow_valid = tracer.traceConcreteFunctionPointers(cs.getCalledValue(), flow, DL);

  if( flow_valid && flow.empty() )
  {
    LLVM_DEBUG(
      errs() << "At callsite ``" << *cs.getInstruction()
             << "'' in " << cs.getInstruction()->getParent()
                                               ->getParent()->getName()
             << '\n'
             <<  "Flow-sensitive test reports success yet returns no targets!\n");

    // The result is valid, but we are going to ignore it.
    // This can happen, for instance, if the function pointer
    // flows from the formal parameter of a function which
    // is never called.
    flow_valid = false;
  }

  if( flow_valid )
  {
    ++numFlowAnalyzed;
    LLVM_DEBUG(errs() << "--> Flow-sensitive test found "
                 << flow.size() << " candidates.\n");
  }

  // Do we need a default case?
  
  // If there's a struct, conservatively add a default a case
  bool existStructArg = false;
  for ( CallSite::arg_iterator cs_arg_it = cs.arg_begin(), z = cs.arg_end(); cs_arg_it != z; cs_arg_it++){
     if (isa<StructType>((*cs_arg_it)->getType())){
       existStructArg = true;
       break;
     }
  }
  

  const TypeSanityAnalysis &typeaa = getAnalysis< TypeSanityAnalysis >();
  output.requiresDefaultCase =
    !flow_valid && !typeaa.isSane( cs.getCalledValue()->getType() );

  // if struct exists, set to true
  output.requiresDefaultCase |= existStructArg;

  LLVM_DEBUG(errs() << "Possible targets of ``" << *cs.getInstruction()
               << "'' include:\n");

  // The flow-insensitive analysis which restricts the set of
  // potential targets according to the arity and types of functions.

  const unsigned arity = cs.arg_size();
  for(NoCaptureFcn::iterator ar=nocap.begin(arity), arz=nocap.end(arity); ar!=arz; ++ar)
  {
    const FcnList &list = ar->second;
    for(FcnList::const_iterator i=list.begin(), e=list.end(); i!=e; ++i)
    {
      Function *fcn = *i;

      if( ! areTypesStrictlyCompatible(fcn, cs) )
        continue;

      // If our flow-sensitive search was successful,
      // then exclude any function not listed those results.
      if( flow_valid )
        if( !std::binary_search(flow.begin(), flow.end(), fcn) )
        {
          ++savingsFromFlow;
          continue;
        }

      LLVM_DEBUG(errs() << " - " << fcn->getName()
                   << " : " << *fcn->getFunctionType() << '\n');

      output.callees.push_back(fcn);
    }
  }

  LLVM_DEBUG(
    if( output.requiresDefaultCase )
      errs() << " - default case is necessary.\n";
  );
}

// Recognize the load-from-constant-table-via-index idiom.
// In this pattern, an integer index is used to find a function
// pointer within a constant table.  This pattern is very common
// in 403.gcc.  Additionally, it is preferable, since it allows
// us to generate a SwitchInst, while other results require us
// to use a sequence of compare-and-branches.
bool DevirtualizationAnalysis::recognizeLoadFromConstantTableIdiom(CallSite &cs, Strategy &output)
{
  Value *fcn_ptr = cs.getCalledValue();

  // strip casts
  while( BitCastInst *cast = dyn_cast< BitCastInst >(fcn_ptr) )
    fcn_ptr = cast->getOperand(0);

  LoadInst *load = dyn_cast< LoadInst >(fcn_ptr);
  if( !load )
    return false;

  Value *table_pointer = load->getPointerOperand();

  // strip casts
  while( BitCastInst *cast = dyn_cast< BitCastInst >(table_pointer) )
    table_pointer = cast->getOperand(0);

  GetElementPtrInst *gep = dyn_cast< GetElementPtrInst >(table_pointer);
  if( !gep )
    return false;
  if( !gep->isInBounds() )
    return false;

  Constant *table = dyn_cast< Constant >( gep->getPointerOperand() );
  if( !table )
    return false;

  // strip casts
  if( ConstantExpr *cexpr = dyn_cast< ConstantExpr >(table) )
    if( cexpr->isCast() )
      table = cexpr->getOperand(0);

  GlobalVariable *gv = dyn_cast< GlobalVariable >( table );
  if( !gv )
    return false;
  if( !gv->isConstant() )
    return false;
  if( !gv->hasInitializer() )
    return false;

  Constant *initializer = gv->getInitializer();

  // The first gep index must be zero;
  GetElementPtrInst::op_iterator i0 = gep->idx_begin(), e = gep->idx_end();
  if( i0 == e )
    return false;
  ConstantInt *ci0 = dyn_cast< ConstantInt >( *i0 );
  if( !ci0 )
    return false;
  if( ci0->getSExtValue() != 0 )
    return false;

  // There must be exactly one non-constant index.

  // value of the unique, non-constant GEP index:
  Value *non_constant_index = 0;
  // position of said index among the GEP indices:
  GetElementPtrInst::op_iterator pos_non_const_idx = e;

  for(GetElementPtrInst::op_iterator i=i0+1; i!=e; ++i)
  {
    Value *operand = *i;
    if( ConstantInt *ci = dyn_cast< ConstantInt >(operand) )
    {
      if( 0 == non_constant_index )
        initializer = initializer->getAggregateElement( ci->getSExtValue() );
      continue;
    }
    else if( 0 == non_constant_index )
    {
      non_constant_index = operand;
      pos_non_const_idx = i;
    }
    else
      return false; // more than one non-constant operand.
  }
  if( !non_constant_index )
    return false;

  // strip casts
  if( const SExtInst *cast = dyn_cast< SExtInst >(non_constant_index) )
    non_constant_index = cast->getOperand(0);
  if( const ZExtInst *cast = dyn_cast< ZExtInst >(non_constant_index) )
    non_constant_index = cast->getOperand(0);

  // How many entries does this table have?
  unsigned N=0;
  Type *init_type = initializer->getType();
  if( ArrayType *arrty = dyn_cast< ArrayType >( init_type ) )
    N = arrty->getNumElements();
  else if( VectorType *vecty = dyn_cast< VectorType >( init_type ) )
    N = vecty->getNumElements();
  else if( StructType *structty = dyn_cast< StructType >( init_type ) )
    N = structty->getNumElements();

  if( N < 1 )
    return false;

  // Determine the callees, in order.
  unsigned num_non_null=0;
  for(unsigned i=0; i<N; ++i)
  {
    Constant *element = initializer->getAggregateElement(i);

    for(GetElementPtrInst::op_iterator j=pos_non_const_idx+1; j!=e; ++j)
    {
      ConstantInt *ci = cast< ConstantInt >( *j );
      element = element->getAggregateElement( ci->getSExtValue() );
    }

    // strip casts
    if( ConstantExpr *cexpr = dyn_cast< ConstantExpr >(element) )
      if( cexpr->isCast() )
        element = cexpr->getOperand(0);

    // Add it to the list: either a function or a null
    Function *fcn = dyn_cast<Function>( element );
    if( fcn && areTypesWeaklyCompatible(fcn,cs) )
    {
      // N.B.: we use weak-compatibility instead
      // of strict-compatibility.  That is because
      // some benchmarks (403.gcc) shamelessly include
      // undefined behavior by passing too many
      // actuals to a virtual call.  As much as I'd
      // prefer to punt them, SPEC must work.
      //
      // Consequence: later optimization may print
      // the message: WARNING: While resolving call to
      // function 'xxx' arguments were dropped!

      output.callees.push_back( fcn );
      ++num_non_null;
    }

    else if( fcn /* and not compatible */ )
      // Found a function, but a type mismatch indicates
      // that calling it here would result in undefined
      // behavior => compiler's choice.
      output.callees.push_back(0);

    else if( isa< ConstantPointerNull >(element) )
      // Found a null pointer.  Calling null would be
      // undefined behavior => compiler's choice.
      output.callees.push_back(0);

    else
      // Cannot determine which would be called in this case!
      return false;
  }

  if( num_non_null < 1 )
    return false;

  // Can we further restrict the cases based on possible
  // values of the index expression?
  const NoCaptureFcn &nocap = getAnalysis<NoCaptureFcn>();
  const NonCapturedFieldsAnalysis &noescape = getAnalysis< NonCapturedFieldsAnalysis >();
  Tracer tracer(nocap,noescape);
  Tracer::IntSet index_values;
  if( tracer.traceConcreteIntegerValues(non_constant_index, index_values) )
    if( !index_values.empty() )
    {
      ++numTraceIntSuccess;

      // First check to make sure we don't
      // completely ruin this opportunity.
      unsigned num_removed = 0;
      for(unsigned i=0; i<N; ++i)
        // If this is not a possible integer value
        if( !index_values.count(i) )
          // And if we have a case for this value
          if( output.callees[i] )
            ++num_removed;

      // If not, then prune the alternatives.
      if( num_removed < num_non_null )
        for(unsigned i=0; i<N; ++i)
          // If this is not a possible integer value
          if( !index_values.count(i) )
            // And if we have a case for this value
            if( output.callees[i] )
            {
              ++savingsFromTraceInt;
              --num_non_null;
              output.callees[i] = 0;
            }
    }

  // Looks good.
  output.dispatch = Strategy::LoadFromConstantTableViaIndex;
  output.requiresDefaultCase = false;
  output.index = non_constant_index;

  LLVM_DEBUG(
    errs() << "Recognized table-call idiom for ``"
           << *cs.getInstruction()
           << "'' with " << num_non_null << " callees.\n";
    for(unsigned i=0; i<N; ++i)
      if( output.callees[i] )
        errs() << "  " << i
               << " => " << output.callees[i]->getName()
               << " : " << *output.callees[i]->getType() << '\n';
  );

  ++numRecognizedLoadFromTable;
  return true;
}

bool DevirtualizationAnalysis::isWildcard(Type *ty) const
{
  // Pointer to structure with no members.
  if( PointerType *pty = dyn_cast< PointerType >(ty) )
    if( StructType *structty = dyn_cast< StructType >( pty->getElementType() ) )
      return structty->getNumElements() == 0;
  return false;
}


// Check ty1 is a subclass of ty2
bool DevirtualizationAnalysis::isPotentialSubClass(StructType *ty1, StructType *ty2){
  // Ziyang (Feb 19):
  // Step 1: check whether ty2 is equivalent (remove tail) to any element of ty1
  // Step 2: recuisively check whether any element of ty1 is a subclass of ty2
  const unsigned N = ty1->getNumElements();
  for (unsigned i = 0; i < N; ++i){
    Type *elm = ty1->getElementType(i);
    if (areStructurallyEquivalentTransitively(elm, ty2))
      return true;

    // elm is a subclass of ty2
    if (StructType *structelm = dyn_cast<StructType>(elm))
      if (isPotentialSubClass(structelm, ty2))
        return true;
  }
  return false;
}

bool DevirtualizationAnalysis::areStructurallyEquivalentTransitively(Type *ty1, Type *ty2)
{
  // Trivial case
  if( ty1 == ty2 )
    return true;

  // The types are not identical.

  // Is either a wildcard type?
  if( isWildcard(ty1) && ty2->isPointerTy() )
    return true;
  else if( isWildcard(ty2) && ty1->isPointerTy() )
    return true;

  // Equivalence is symmetric.  WNLOG, ty1<ty2
  if( ty1 > ty2 )
    std::swap(ty1,ty2);
  TyTy key(ty1,ty2);

  // Already computed?
  if( equivalentTypes.count(key) )
    return equivalentTypes[key];

  // Avoid infinite recursion.
  // 'true' is the inductive hypothesis.
  equivalentTypes[key] = true;

  // Are they structurally equivalent?

  // Ziyang: PointerType is not SequentialType any more
  // Pointer type: pointers
  if (PointerType *ptrty1 = dyn_cast< PointerType >(ty1))
    if (PointerType *ptrty2 = dyn_cast< PointerType >(ty2))
    {
      const bool eq = areStructurallyEquivalentTransitively(
          ptrty1->getElementType(),
          ptrty2->getElementType() );
      if( !eq )
        LLVM_DEBUG(errs() << "Types " << *ty1 << "\n  and " << *ty2 << " are not structurally equivalent (ptr)\n");
      return equivalentTypes[key] = eq;
    }

  // Sequential types: arrays, vectors
  if( SequentialType *seqty1 = dyn_cast< SequentialType >(ty1) )
    if( SequentialType *seqty2 = dyn_cast< SequentialType >(ty2) )
    {
      const bool eq = areStructurallyEquivalentTransitively(
          seqty1->getElementType(),
          seqty2->getElementType() );
      if( !eq )
        LLVM_DEBUG(errs() << "Types " << *ty1 << "\n  and " << *ty2 << " are not structurally equivalent (seq)\n");
      return equivalentTypes[key] = eq;
    }

  // Structures
  // Ziyang: Feb 17, 2020
  //   In C++, classes are structs;
  //   We need to guess whether two classes have inheritance relationship
  if( StructType *structty1 = dyn_cast< StructType >(ty1) )
    if( StructType *structty2 = dyn_cast< StructType >(ty2) )
    {
      LLVM_DEBUG(errs() << "Checking types " << *ty1 << " and " << *ty2<< "\n");
      // // identical layout, quick one
      // if (structty1->isLayoutIdentical(structty2))
      //   return equivalentTypes[key] = true;

      // check subclass relation
      if (isPotentialSubClass(structty1, structty2)){
        LLVM_DEBUG(errs() << "Type " << *ty1 << "is a subtype (struct) of " << *ty2 << "\n");
        return equivalentTypes[key] = true;
      }

      if (isPotentialSubClass(structty2, structty1)){
        LLVM_DEBUG(errs() << "Type " << *ty2 << "is a subtype (struct) of " << *ty1 << "\n");
        return equivalentTypes[key] = true;
      }

      // Check Only equivalence relation
      // if the same elements, cannot be alignment difference
      const unsigned N1 = structty1->getNumElements();
      const unsigned N2 = structty2->getNumElements();
      unsigned checkN;

      // the same length
      // OR one has one more, and the last element is an array of i8
      if (N1 == N2) checkN = N1;
      else {
        bool isAlignedDifference = false;
        // structty1 is an aligned one
        if (N1 == N2 + 1) {
          //the last element is an array of i8
          if (ArrayType *arrty = dyn_cast<ArrayType>(structty1->getElementType(N1 - 1))){
            IntegerType *elmty = dyn_cast<IntegerType>(arrty->getElementType());
            if (elmty->getBitWidth() == 8){
              checkN = N2;
              isAlignedDifference = true;
            }
          }
        }
        else if (N2 == N1 + 1) {
          //the last element is an array of i8
          if (ArrayType *arrty = dyn_cast<ArrayType>(structty2->getElementType(N2 - 1))){
            IntegerType *elmty = dyn_cast<IntegerType>(arrty->getElementType());
            if (elmty->getBitWidth() == 8){
              checkN = N1;
              isAlignedDifference = true;
            }
          }
        }

        // not an aligned case
        if (!isAlignedDifference){
          LLVM_DEBUG(errs() << "Types " << *ty1 << "\n  and " << *ty2 << " are not structurally equivalent (not same #elements; not just alignment difference)\n");
          return equivalentTypes[key] = false;
        }
      }

      for(unsigned i=0; i< checkN; ++i)
      {
        Type *elt1 = structty1->getElementType(i);
        Type *elt2 = structty2->getElementType(i);

        if( !areStructurallyEquivalentTransitively(elt1,elt2) )
        {
          LLVM_DEBUG(errs() << "Types " << *ty1 << "\n  and " << *ty2 << " are not structurally equivalent (struct: at least an element not the same)\n");
          return equivalentTypes[key] = false;
        }
      }

      // Two structures with same size or just alignment difference and equivalent types are equivalent.
      return equivalentTypes[key] = true;
    }

  // Functions (guh)
  if( FunctionType *fty1 = dyn_cast< FunctionType >( ty1 ) )
    if( FunctionType *fty2 = dyn_cast< FunctionType >( ty2 ) )
    {
      // Different number of parameters?
      const unsigned N = fty1->getNumParams();
      if( fty2->getNumParams() != N )
      {
        LLVM_DEBUG(errs() << "Types " << *ty1 << "\n  and " << *ty2 << " are not structurally equivalent (7)\n");
        return equivalentTypes[key] = false;
      }
      if( fty1->isVarArg() != fty2->isVarArg() )
      {
        LLVM_DEBUG(errs() << "Types " << *ty1 << "\n  and " << *ty2 << " are not structurally equivalent (8)\n");
        return equivalentTypes[key] = false;
      }

      // Different return types?
      if( !areStructurallyEquivalentTransitively(
        fty1->getReturnType(),
        fty2->getReturnType() ) )
      {
        LLVM_DEBUG(errs() << "Types " << *ty1 << "\n  and " << *ty2 << " are not structurally equivalent (9)\n");
        return equivalentTypes[key] = false;
      }

      // Different formal parameter types?
      for(unsigned i=0; i<N; ++i)
      {
        Type *pty1 = fty1->getParamType(i);
        Type *pty2 = fty2->getParamType(i);

        if( !areStructurallyEquivalentTransitively(pty1,pty2) )
        {
          LLVM_DEBUG(errs() << "Types " << *ty1 << "\n  and " << *ty2 << " are not structurally equivalent (10)\n");
          return equivalentTypes[key] = false;
        }
      }

      // Two functions with same signatures are equivalent.
      return equivalentTypes[key] = true;
    }

  // All other cases
  LLVM_DEBUG(errs() << "Types " << *ty1 << "\n  and " << *ty2 << " are not structurally equivalent (3)\n");
  return equivalentTypes[key] = false;
}

char DevirtualizationAnalysis::ID = 0;
static RegisterPass<DevirtualizationAnalysis> mp(
  "analyze-indirect-calls", "Analyze indirect function calls");
}
