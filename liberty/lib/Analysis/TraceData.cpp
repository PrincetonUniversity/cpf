#define DEBUG_TYPE "tracedata"

#include "liberty/Analysis/TraceData.h"
#include "liberty/Analysis/NoCaptureFcn.h"

#include "llvm/IR/Operator.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"

#include "liberty/Utilities/FindUnderlyingObjects.h"
#include "liberty/Utilities/CallSiteFactory.h"

#include "NoEscapeFieldsAA.h"

namespace liberty
{
using namespace llvm;

STATISTIC(numTraceInt,                      "Num invocations of traceConcreteIntegerValues()");
STATISTIC(numTraces,                        "Num flow-sensitive searches initiated");
STATISTIC(numTraceValueSteps,               "Num invocations of traceConcreteFunctionPointers()");
STATISTIC(numTraceLoadFromSteps,            "Num invocations of traceConcreteFunctionPointersLoadedFrom()");
STATISTIC(numTraceConstantInitializerSteps, "Num invocations of extractConcreteFunctionPointersFromConstantInitializer()");

Tracer::Tracer(const NoCaptureFcn &nc, const NonCapturedFieldsAnalysis &nx)
  : nocap(nc), noescape(nx) {}

bool Tracer::traceConcreteIntegerValues(Value *expr, IntSet &output) const
{
  ValueSet already;
  if( traceConcreteIntegerValues(expr,output,already) )
  {
    LLVM_LLVM_DEBUG(
      if( !output.empty() )
      {
        errs() << "Value " << *expr << " can only take the values: ";
        for(IntSet::const_iterator i=output.begin(), e=output.end(); i!=e; ++i)
          errs() << *i << ", ";
        errs() << '\n';
      }
    );
    return true;
  }
  return false;
}

bool Tracer::traceConcreteIntegerValues(Value *expr, IntSet &output, ValueSet &already) const
{
  if( already.count(expr) )
    return true;
  already.insert(expr);

  ++numTraceInt;
  IntegerType *intty = dyn_cast< IntegerType >( expr->getType() );

  if( isa<UndefValue>(expr) )
    return true; // compiler's choice

  else if( intty && intty->getBitWidth() > sizeof(uint64_t)*8 )
    return false; // too big to represent in a 64-bit integer

  else if( ConstantInt *ci = dyn_cast<ConstantInt>(expr) )
  {
    output.insert( ci->getZExtValue() );
    return true;
  }

  else if( SelectInst *sel = dyn_cast<SelectInst>(expr) )
    return
       traceConcreteIntegerValues(sel->getTrueValue(),output,already)
    && traceConcreteIntegerValues(sel->getFalseValue(),output,already);

/*
  else if( PHINode *phi = dyn_cast<PHINode>(expr) )
  {
    // this is incorrect for induction variables, because
    // upon re-visiting the PHI, it sees that it was already
    // visited and returns an empty set instead of potential values.

    for(unsigned i=0, N=phi->getNumIncomingValues(); i<N; ++i)
      if( ! traceConcreteIntegerValues(
        phi->getIncomingValue(i),output,already) )
        return false;
    return true;
  }
*/

  else if( Argument *arg = dyn_cast<Argument>(expr) )
  {
    // If this is an internally-linked function,
    // and if its address is never taken,
    // then we can enumerate all callsites of this function,
    // and from that extract all actual parameters.
    Function *context = arg->getParent();
    if( context->hasInternalLinkage() && ! nocap.isCaptured(context) )
    {
      if( context->use_empty() )
        LLVM_LLVM_DEBUG(errs() << "*** Integer expr is an argument to a "
                     << "function that is never called.  "
                     << "Don't be surprised if trace-int "
                     << "analysis reports no values (3)! ***\n");

      const unsigned argno = arg->getArgNo();
      //sot: replaced use with user. No reason to use Use instead of User. avoid assertion
      for(Value::user_iterator i=context->user_begin(),e=context->user_end(); i!=e; ++i)
      {
        CallSite cs = getCallSite(&**i);
        assert( cs.getInstruction() && "WTF--all uses should be callsites (2)");

        if( !traceConcreteIntegerValues(
          cs.getArgument(argno), output, already) )
          return false;
      }
      return true;
    }
  }

  // TODO: load from constant memory

  else if( ZExtInst *zext = dyn_cast<ZExtInst>(expr) )
    return traceConcreteIntegerValues(zext->getOperand(0),output,already);

  else if( SExtInst *sext = dyn_cast<SExtInst>(expr) )
  {
    IntSet vals2;
    if( !traceConcreteIntegerValues(sext->getOperand(0),vals2,already) )
      return false;

    IntegerType *smaller = cast< IntegerType >( sext->getOperand(0)->getType() );

    const uint64_t sign_bit_mask = smaller->getSignBit();
    const uint64_t sign_extend  = intty->getBitMask() & ~ smaller->getBitMask();

    for(IntSet::const_iterator i=vals2.begin(), e=vals2.end(); i!=e; ++i)
    {
      const uint64_t vi = *i;
      if( (vi & sign_bit_mask) != 0 )
        output.insert( vi | sign_extend );
      else
        output.insert( vi );
    }

    return true;
  }

  else if( TruncInst *trunc = dyn_cast<TruncInst>(expr) )
  {
    IntSet vals2;
    if( traceConcreteIntegerValues(trunc->getOperand(0),vals2,already) )
    {
      const uint64_t mask = intty->getBitMask();

      for(IntSet::const_iterator i=vals2.begin(), e=vals2.end(); i!=e; ++i)
        output.insert( mask & *i );

      return true;
    }
    // fall through.
  }

  else if( BinaryOperator *binop = dyn_cast<BinaryOperator>(expr) )
  {
    // Perform two searches
    IntSet vals1;
    if( !traceConcreteIntegerValues(binop->getOperand(0),vals1,already) )
      return false;
    if( vals1.empty() )
      return true;

    IntSet vals2;
    if( !traceConcreteIntegerValues(binop->getOperand(1),vals2,already) )
      return false;
    if( vals2.empty() )
      return true;

    for(IntSet::iterator i=vals1.begin(), N=vals1.end(); i!=N; ++i)
    {
      const uint64_t vi = *i;
      for(IntSet::iterator j=vals2.begin(), M=vals2.end(); j!=M; ++j)
      {
        const uint64_t vj = *j;

        switch( binop->getOpcode() )
        {
        case BinaryOperator::Add:
          output.insert( vi+vj );
          break;
        case BinaryOperator::Or:
          output.insert( vi|vj );
          break;
        case BinaryOperator::Shl:
          output.insert( vi<<vj );
          break;
        case BinaryOperator::Xor:
          output.insert( vi^vj );
          break;

        default:
          // TODO: Sub, Mul, UDiv, SDiv, URem, SRem, LShr, AShr, And, Xor
          errs() << "traceConcreteIntegerValues() failed on: ``"
                 << *expr << "''\n";
          assert(false && "Implement more binops");
          break;
        }
      }
    }

    return true;
  }

  // If all else fails, we can at least say that booleans are zero or one.
  if( intty && intty->getBitWidth() == 1 )
  {
    output.insert(0);
    output.insert(1);
    return true;
  }

  LLVM_LLVM_DEBUG(errs() << "traceConcreteIntegerValues() failed on: ``"
               << *expr << "''\n");
  return false;
}

bool Tracer::traceConcreteFunctionPointers(Value *fcn_ptr, FcnList &output,
                                          const DataLayout &DL) const
{
  ValueSet already;
  ++numTraces;
  const bool success = traceConcreteFunctionPointers(fcn_ptr, output, already,DL);

  // Make this list unique.
  if( success )
  {
    std::sort( output.begin(), output.end() );
    FcnList::iterator new_end = std::unique(output.begin(), output.end());
    output.resize( new_end - output.begin() );
  }

  return success;
}

bool Tracer::traceConcreteFunctionPointers(
  Value *fcn_ptr, FcnList &output, ValueSet &already,
                  const DataLayout &DL) const
{
  if( already.count(fcn_ptr) )
    return true;
  already.insert(fcn_ptr);

  ++numTraceValueSteps;

  if( Function *fcn = dyn_cast<Function>(fcn_ptr) )
  {
    output.push_back( fcn );
    return true;
  }

  else if( isa< ConstantPointerNull >(fcn_ptr) )
    return true; // calling null is undefined => compiler's choice

  else if( isa< UndefValue >(fcn_ptr) )
    return true; // calling undef is undefined => compiler's choice

  else if( LoadInst *load = dyn_cast< LoadInst >(fcn_ptr) )
    return traceConcreteFunctionPointersLoadedFrom(
      load->getPointerOperand(), output, already, DL );

  else if( Argument *arg = dyn_cast<Argument>(fcn_ptr) )
  {
    // If this is an internally-linked function,
    // and if its address is never taken,
    // then we can enumerate all callsites of this function,
    // and from that extract all actual parameters.
    Function *context = arg->getParent();
    if( context->hasInternalLinkage() && ! nocap.isCaptured(context) )
    {
      if( context->use_empty() )
        LLVM_LLVM_DEBUG(errs() << "*** Called value is an argument to a "
                     << "function that is never called.  "
                     << "Don't be surprised if flow-sensitive "
                     << "analysis reports no callees! ***\n");

      const unsigned argno = arg->getArgNo();

      //sot: replaced use with user. No reason to use Use instead of User. avoid assertion
      for(Value::user_iterator i=context->user_begin(),e=context->user_end(); i!=e; ++i)
      {
        CallSite cs = getCallSite(&**i);
        assert( cs.getInstruction() && "WTF--all uses should be callsites");

        if( !traceConcreteFunctionPointers(cs.getArgument(argno), output, already, DL) )
          return false;
      }
      return true;
    }
  }

  UO uo;
  GetUnderlyingObjects(fcn_ptr,uo, DL);
  if( !uo.empty() && fcn_ptr != *uo.begin() ) // if progress
  {
    for(UO::iterator i=uo.begin(), e=uo.end(); i!=e; ++i)
      if( !traceConcreteFunctionPointers(
        const_cast<Value*>(*i), output, already, DL) )
        return false;
    return true;
  }

  LLVM_LLVM_DEBUG(errs() << "Flow-sensitive trace failed on: ``"
               << *fcn_ptr << "''\n");
  return false;
}

bool Tracer::traceConcreteFunctionPointersLoadedFrom(
  Value *ptr, FcnList &output, ValueSet &already,
              const DataLayout &DL) const
{
  if( already.count(ptr) )
    return true;
  already.insert(ptr);

  ++numTraceLoadFromSteps;

  if( isa< UndefValue >(ptr) )
    return true; // loading undef is undefined => compiler's choice.
  else if( isa< ConstantPointerNull >(ptr) )
    return true; // loading null is undefined => compiler's choice

  else if( GlobalVariable *global = dyn_cast< GlobalVariable >( ptr ) )
  {
    if( global->isConstant() && global->hasInitializer() )
    {
      Constant *initializer = global->getInitializer();
      return extractConcreteFunctionPointersFromConstantInitializer(
        initializer,output,already);
    }
  }

  else if( Argument *arg = dyn_cast<Argument>(ptr) )
  {
    // If this is an internally-linked function,
    // and if its address is never taken,
    // then we can enumerate all callsites of this function,
    // and from that extract all potential actual parameters.
    Function *context = arg->getParent();
    if( context->hasInternalLinkage() && ! nocap.isCaptured(context) )
    {
      if( context->use_empty() )
        LLVM_LLVM_DEBUG(errs() << "*** Called value is an argument to a "
                     << "function that is never called.  "
                     << "Don't be surprised if flow-sensitive "
                     << "analysis reports no callees (2)! ***\n");

      const unsigned argno = arg->getArgNo();
      for(Value::user_iterator i=context->user_begin(),e=context->user_end(); i!=e; ++i)
      {
        CallSite cs = getCallSite(&**i);
        assert( cs.getInstruction() && "WTF--all uses should be callsites (2)");

        if( !traceConcreteFunctionPointersLoadedFrom(
          cs.getArgument(argno), output, already, DL) )
          return false;
      }
      return true;
    }
  }

  // A special case to make this analysis partially field- and element-sensitive.
  if( GEPOperator *gep = dyn_cast< GEPOperator >(ptr) )
  {
    Value *pointer = gep->getPointerOperand();
    if( ConstantExpr *cexpr = dyn_cast< ConstantExpr >(pointer) )
      if( cexpr->isCast() )
        pointer = cexpr->getOperand(0);

    if( GlobalVariable *gv = dyn_cast< GlobalVariable >( pointer ) )
      if( gv->isConstant() && gv->hasInitializer() )
      {
        Constant *initializer = gv->getInitializer();

        GEPOperator::const_op_iterator i=gep->idx_begin(), e=gep->idx_end();
        assert( i != e && "GEPOperator with no indices");

        // The first index removes the initial pointer to the
        // global constant.
        if( ConstantInt *ci0 = dyn_cast< ConstantInt >( *i ) )
        {
          ++i;
          if( ci0->getSExtValue() == 0 )
          {
            // Use as many constant-indices of the GEP as possible
            // to narrow down the portion of the constant initializer.
            for(; i!=e; ++i)
            {
              ConstantInt *ci = dyn_cast< ConstantInt >( *i );
              if( !ci )
                break;

              unsigned idx = ci->getSExtValue();
              initializer = initializer->getAggregateElement(idx);
            }

            // Extract the constants from this slice of the initializer.
            return extractConcreteFunctionPointersFromConstantInitializer(
              initializer, output, already);
          }
        }
      }
  }

  UO uo;
  GetUnderlyingObjects(ptr,uo,DL);
  if( !uo.empty() && ptr != *uo.begin() ) // if progress
  {
    bool all_good = true;
    for(UO::iterator i=uo.begin(), e=uo.end(); i!=e && all_good; ++i)
      if( !traceConcreteFunctionPointersLoadedFrom(
        const_cast<Value*>(*i), output, already, DL) )
        all_good = false;

    if( all_good )
      return true;
  }

  // In some cases, we can enumerate all definitions of
  // non-constant memory by using NonCapturedFieldsAnalysis.
  if( GetElementPtrInst *gep = dyn_cast< GetElementPtrInst >(ptr) )
  {
    StructType *structty;
    const ConstantInt *fieldno;
    if( noescape.isFieldPointer(gep,&structty,&fieldno) )
    {
      NonCapturedFieldsAnalysis::Defs defs;
      if( noescape.findAllDefs(structty,fieldno,defs) && !defs.empty() )
      {
        typedef NonCapturedFieldsAnalysis::Defs::const_iterator I;

        LLVM_LLVM_DEBUG(
          errs()
            << "Load from non-escaping field:\n"
            << " GEP: " << *gep << '\n'
            << " Structure: " << *structty << '\n'
            << " Field: " << *fieldno << '\n'
            << "Definitions:\n";
          for(I i=defs.begin(), e=defs.end(); i!=e; ++i)
            errs() << " o " << *i->second << '\n';
        );
        for(I i=defs.begin(), e=defs.end(); i!=e; ++i)
          if( !traceConcreteFunctionPointers(i->second, output, already, DL) )
            return false;
        return true;
      }
    }
  }

  LLVM_LLVM_DEBUG(errs() << "Flow-sensitive trace load-from failed on: ``"
               << *ptr << "''\n");
  return false;
}

bool Tracer::extractConcreteFunctionPointersFromConstantInitializer(
  Constant *constant, FcnList &output, ValueSet &already) const
{
  if( already.count(constant) )
    return true;
  already.insert(constant);

  ++numTraceConstantInitializerSteps;

  if( Function *fcn = dyn_cast< Function >(constant) )
  {
    output.push_back( fcn );
    return true;
  }

  // Constant array initializer
  else if( const ConstantDataSequential *cds =
    dyn_cast<ConstantDataSequential>(constant) )
  {
    const unsigned N=cds->getNumElements();
    for(unsigned i=0; i<N; ++i)
      if( !extractConcreteFunctionPointersFromConstantInitializer(
        cds->getElementAsConstant(i), output, already) )
        return false;
    return true;
  }

  // The other kind of constant array initializer?
  else if( const ConstantArray *ca =
    dyn_cast< ConstantArray >(constant) )
  {
    ArrayType *aty = ca->getType();
    const unsigned N=aty->getNumElements();
    for(unsigned i=0; i<N; ++i)
      if( !extractConcreteFunctionPointersFromConstantInitializer(
        ca->getAggregateElement(i), output, already) )
        return false;
    return true;
  }

  // Constant structure initializer
  else if( const ConstantStruct *cs = dyn_cast<ConstantStruct>(constant) )
  {
    StructType *sty = cs->getType();
    const unsigned N = sty->getNumElements();

    for(unsigned i=0; i<N; ++i)
      if( ! extractConcreteFunctionPointersFromConstantInitializer(
        cs->getAggregateElement(i), output, already ) )
        return false;
    return true;
  }

  // Other fields within a constant which cannot hold a pointer address.
  else if( isa< BlockAddress >(constant) )
    return true;
  else if( isa< ConstantAggregateZero >(constant) )
    return true;
  else if( isa< ConstantFP >(constant) )
    return true;
  else if( isa< ConstantInt >(constant) )
    return true;
  else if( isa< ConstantPointerNull >(constant) )
    return true;
  else if( isa< GlobalVariable >(constant) )
    return true;
  else if( isa< UndefValue >(constant) )
    return true;

  else if( ConstantExpr *cexpr = dyn_cast< ConstantExpr >(constant) )
  {
    if( cexpr->isCast()
    ||  cexpr->getOpcode() == Instruction::GetElementPtr )
    {
      Constant *operand = cexpr->getOperand(0);
      return extractConcreteFunctionPointersFromConstantInitializer(
        operand, output, already);
    }
  }

  LLVM_LLVM_DEBUG(errs() << "Flow-sensitive trace load-from-constant failed on: ``"
               << *constant << "''\n");
  return false;
}

bool Tracer::disjoint(const IntSet &a, const IntSet &b)
{
  for(IntSet::const_iterator i=a.begin(), e=a.end(); i!=e; ++i)
    if( b.count(*i) )
      return false;
  return  true;
}

}
