// The SpecPriv profiler.
// We are trying to answer the following questions.
//  (1) Which allocation units are private w.r.t. a given loop.
//  (2) Which pointers refer to which allocation units?
//  (3) For upward-exposed loads inside a loop, can we predict the
//      value loaded in terms of (i) a consistent integer value,
//      or (ii) a consistent offset within a consistent allocation
//      unit.
//
// To do this, we must instrument the following.
//  (a) Allocation of globals
//  (b) Allocation of stack variables
//  (c) Allocation of heap variables
//  (d) The function and loop stack
//  (e) Generation of a pointer with indeterminate underlying object.
//  (f) The value of upward-exposed loads.
//
// Our language to describe allocation units is as follows
//  Allocation Unit :== Global variable || Allocation Instruction @ Context
//  Allocation Instruction :== Alloca Inst || Call Malloc
//  Context :== TOP || Loop, Context. || Fcn, Context.
//
// Limitations:
//  - Upward exposed load instrumentation may cause loads from pointers
//    when the host program does not load them.  One consequence of this
//    is that it may load from invalid addresses, causing a segfault.
//    This has been corrected in some trivial cases (null pointer), but
//    a more general solution using a segfault handler would be preferred.

// Some things which I should handle
// __fxstat
// __xstat
// __strdup (like malloc)
// exec, execl, execvp (tell profiler to finish up)

#define DEBUG_TYPE "malloc-profiler"

#include "llvm/Pass.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/ScalarEvolutionExpressions.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/Verifier.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"

#include "liberty/Analysis/KillFlow.h"
#include "scaf/Utilities/CallSiteFactory.h"
#include "scaf/Utilities/InsertPrintf.h"
#include "scaf/Utilities/SplitEdge.h"
#include "scaf/Utilities/GlobalCtors.h"
#include "liberty/PointsToProfiler/Indeterminate.h"
#include "liberty/PointsToProfiler/Remat.h"
#include "scaf/Utilities/ModuleLoops.h"

#include <sstream>

namespace liberty
{
namespace SpecPriv
{

using namespace llvm;

static const char *recognized_external_function_list[] = {
#include "RecognizedExternalFunctions.h"
  0
};

static const char *llvm_multi_type_function_list[] = {
#include "LLVMMultiTypeFunctions.h"
  0
};

STATISTIC(numValuePredictors, "Num value-predictor instrumentations inserted");
STATISTIC(numResidue,         "Num pointer-residue instrumentations inserted");
STATISTIC(numIndeterminate,   "Num indeterminate-base instrumentations inserted");
STATISTIC(numConstants,"Num constant variables instrumented");
STATISTIC(numGlobal,   "Num global variables instrumented");
STATISTIC(numStack,    "Num stack variables instrumented");
STATISTIC(numMalloc,   "Num mallocs instrumented");
STATISTIC(numFree,     "Num frees instrumented");
STATISTIC(numRealigned, "Num alloca instructions or global variables/constants with alignment changed");
STATISTIC(lifetimeMarkersRemoved, "Num lifetime markers removed");

static cl::opt<unsigned> MaxAnalysisTimePerFunction(
    "specpriv-profile-max-analysis-per-fcn", cl::init(3*60), cl::Hidden,
    cl::desc("Max seconds spent identifying upward-exposed loads in any function."));
static cl::opt<bool> DontTrackResidues(
  "specpriv-profile-dont-track-residues", cl::init(false), cl::NotHidden,
  cl::desc("Do NOT track pointer residues"));

/*
static cl::opt<bool> NoUnsafe(
  "specpriv-profile-no-unsafe", cl::init(false), cl::Hidden,
  cl::desc("Don't do unsafe loads"));

static cl::opt<bool> TraceBeforeUnsafe(
  "specpriv-profile-trace-before-unsafe", cl::init(false), cl::Hidden,
  cl::desc("Insert a trace before a potentially unsafe load"));
*/

static cl::opt<bool> VerifyOften(
  "specpriv-profile-verify-often", cl::init(false), cl::Hidden,
  cl::desc("Verify those functions we modify often"));
static cl::opt<bool> VerifyNesting(
  "specpriv-profile-verify-nesting", cl::init(false), cl::Hidden,
  cl::desc("Verify nesting of context events"));
static cl::opt<std::string> VerifyNestingOnly(
  "specpriv-profile-verify-nesting-only", cl::init(""), cl::Hidden,
  cl::desc("Verify nesting of context evens ONLY FOR THIS FUNCTION"));

typedef std::set<const Value *> ValSet;
typedef std::set<const BasicBlock *> BBSet;
typedef std::set<std::string> StringSet;
typedef std::set<Function*> FcnSet;

// least common multiple
static unsigned lcm(unsigned a, unsigned b)
{
  unsigned gcd = GreatestCommonDivisor64(a,b);
  return a*b/gcd;
}

struct MallocProfiler : public ModulePass
{
  static char ID;
  MallocProfiler() : ModulePass(ID)
  {
    for(unsigned i=0; recognized_external_function_list[i]; ++i)
      recognizedExternalFunctions.insert( recognized_external_function_list[i] );
  }

  typedef std::vector<Instruction *> IList;

  void getAnalysisUsage(AnalysisUsage &au) const
  {
    au.addRequired< KillFlow >();
    //au.addRequired< LoopInfoWrapperPass >();
    //au.addRequired< ScalarEvolutionWrapperPass >();
    au.addRequired< ModuleLoops >();
  }

  Constant *fileptr_au_name(Module &mod)
  {
    if( !unmanaged_fopen_name )
      unmanaged_fopen_name = getStringLiteralExpression(mod, "UNMANAGED fopen");

    return unmanaged_fopen_name;
  }

  Constant *library_constant_string_au_name(Module &mod)
  {
    if( !unmanaged_library_constant_au_name )
      unmanaged_library_constant_au_name = getStringLiteralExpression(mod, "UNMANAGED libconst");

    return unmanaged_library_constant_au_name;
  }

  bool runOnModule(Module &mod)
  {
    LLVMContext &ctx = mod.getContext();
    voidty = Type::getVoidTy(ctx);
    u64 = Type::getInt64Ty(ctx);
    charptr = PointerType::getUnqual( Type::getInt8Ty(ctx) );

    u32 = Type::getInt32Ty(ctx);
    Type *charptrptr = PointerType::getUnqual( charptr );

    ModuleLoops &mloops = getAnalysis<ModuleLoops>();

    unmanaged_fopen_name = 0;
    unmanaged_library_constant_au_name = 0;

    // Capture a list of all global variables
    // before we start adding any.
    std::vector<GlobalVariable*> globals;
    for(Module::global_iterator i=mod.global_begin(), e=mod.global_end(); i!=e; ++i)
      globals.push_back( &*i );

    std::vector<Type *> formals;
    formals.push_back(charptr);
    formals.push_back(charptr);
    formals.push_back(u64);
    FunctionType *mallocty = FunctionType::get(charptr, formals, false);
    FunctionCallee wrapper_prof_malloc = mod.getOrInsertFunction("__prof_malloc", mallocty);
    prof_malloc = cast<Constant>(wrapper_prof_malloc.getCallee());

    formals.clear();
    formals.push_back(charptr);
    formals.push_back(charptr);
    FunctionType *allocstrty = FunctionType::get(charptr, formals, false);
    FunctionCallee wrapper_prof_report_constant_string = mod.getOrInsertFunction(
                                      "__prof_report_constant_string", allocstrty);
    prof_report_constant_string = cast<Constant>(wrapper_prof_report_constant_string.getCallee());


    formals.clear();
    formals.push_back(charptr);
    formals.push_back(charptr);
    formals.push_back(charptr);
    formals.push_back(u64);
    FunctionType *reallocty = FunctionType::get(charptr, formals, false);
    FunctionCallee wrapper_prof_realloc = mod.getOrInsertFunction("__prof_realloc", reallocty);
    prof_realloc = cast<Constant>(wrapper_prof_realloc.getCallee());

    formals.clear();
    formals.push_back(charptr);
    formals.push_back(charptr);
    FunctionType *freety = FunctionType::get(voidty, formals, false);
    FunctionCallee wrapper_prof_free = mod.getOrInsertFunction("__prof_free", freety);
    prof_free = cast<Constant>(wrapper_prof_free.getCallee());
    FunctionCallee wrapper_prof_free_alloca = mod.getOrInsertFunction("__prof_free_stack", freety);
    prof_free_alloca = cast<Constant>(wrapper_prof_free_alloca.getCallee());
    FunctionCallee wrapper_find_underlying = mod.getOrInsertFunction(
                                             "__prof_find_underlying_object", freety);
    find_underlying = cast<Constant>(wrapper_find_underlying.getCallee());

    formals.clear();
    formals.push_back(charptr);
    FunctionType *beginty = FunctionType::get(voidty, formals, false);
    FunctionCallee wrapper_prof_begin_iter = mod.getOrInsertFunction("__prof_begin_iter", beginty);
    prof_begin_iter = cast<Constant>(wrapper_prof_begin_iter.getCallee());

    FunctionType *endty = beginty;
    FunctionCallee wrapper_prof_end_iter = mod.getOrInsertFunction("__prof_end_iter", endty);
    prof_end_iter = cast<Constant>(wrapper_prof_end_iter.getCallee());

    formals.clear();
    formals.push_back(charptr);
    formals.push_back(charptr);
    formals.push_back(u64);
    FunctionType *reportglobalty = FunctionType::get(voidty, formals, false);
    FunctionCallee wrapper_report_constant = mod.getOrInsertFunction(
                                             "__prof_report_constant", reportglobalty);
    FunctionCallee wrapper_report_global = mod.getOrInsertFunction(
                                             "__prof_report_global", reportglobalty);
    report_constant = cast<Constant>(wrapper_report_constant.getCallee());
    report_global = cast<Constant>(wrapper_report_global.getCallee());

    formals.clear();
    formals.push_back(charptr);
    formals.push_back(charptr);
    formals.push_back(u64);
    formals.push_back(u64);
    FunctionType *reportstackty = FunctionType::get(voidty, formals, false);
    FunctionCallee wrapper_report_stack = mod.getOrInsertFunction("__prof_report_stack", reportstackty);
    report_stack = cast<Constant>(wrapper_report_stack.getCallee());

    formals.clear();
    formals.push_back(charptr);
    FunctionType *endfcnty = FunctionType::get(voidty, formals, false);
    FunctionCallee wrapper_begin_fcn = mod.getOrInsertFunction("__prof_begin_function", endfcnty);
    begin_fcn = cast<Constant>(wrapper_begin_fcn.getCallee());
    FunctionCallee wrapper_end_fcn = mod.getOrInsertFunction("__prof_end_function", endfcnty);
    end_fcn = cast<Constant>(wrapper_end_fcn.getCallee());

    formals.clear();
    formals.push_back(charptr);
    formals.push_back(u64);
    FunctionType *predictable_int_ty = FunctionType::get(voidty, formals, false);
    FunctionCallee wrapper_predictable_int_value = mod.getOrInsertFunction(
                                                  "__prof_predict_int", predictable_int_ty);
    predictable_int_value = cast<Constant>(wrapper_predictable_int_value.getCallee());

    formals.clear();
    formals.push_back(charptr);
    formals.push_back(charptr);
    formals.push_back(u32);
    FunctionType *load_and_pred_int_ty = FunctionType::get(voidty, formals, false);
    FunctionCallee wrapper_load_and_predict_int = mod.getOrInsertFunction(
                                        "__prof_predict_int_load", load_and_pred_int_ty);
    load_and_predict_int = cast<Constant>(wrapper_load_and_predict_int.getCallee());

    formals.clear();
    formals.push_back(charptr);
    formals.push_back(charptr);
    FunctionType *predictable_ptr_ty = FunctionType::get(voidty, formals, false);
    FunctionCallee wrapper_predictable_ptr_value = mod.getOrInsertFunction(
                                                    "__prof_predict_ptr", predictable_ptr_ty);
    predictable_ptr_value = cast<Constant>(wrapper_predictable_ptr_value.getCallee());
    FunctionCallee wrapper_residue_fcn = mod.getOrInsertFunction(
                                         "__prof_pointer_residue", predictable_ptr_ty);
    residue_fcn = cast<Constant>(wrapper_residue_fcn.getCallee());

    formals.clear();
    formals.push_back(charptr);
    formals.push_back(charptr);
    FunctionType *load_and_pred_ptr_ty = FunctionType::get(voidty, formals, false);
    FunctionCallee wrapper_load_and_predict_ptr = mod.getOrInsertFunction(
                                              "__prof_predict_ptr_load", load_and_pred_ptr_ty);
    load_and_predict_ptr = cast<Constant>(wrapper_load_and_predict_ptr.getCallee());


    formals.clear();
    FunctionType *startty = FunctionType::get(voidty, formals,false);
    FunctionCallee wrapper_begin_profiling = mod.getOrInsertFunction("__prof_begin", startty);
    begin_profiling = cast<Constant>(wrapper_begin_profiling.getCallee());

    formals.clear();
    formals.push_back(u32);
    formals.push_back(charptrptr);
    FunctionType *argvty = FunctionType::get(voidty, formals, false);
    FunctionCallee wrapper_manage_argv = mod.getOrInsertFunction("__prof_manage_argv", argvty);
    manage_argv = cast<Constant>(wrapper_manage_argv.getCallee());
    FunctionCallee wrapper_unmanage_argv = mod.getOrInsertFunction("__prof_unmanage_argv", argvty);
    unmanage_argv = cast<Constant>(wrapper_unmanage_argv.getCallee());

    formals.clear();
    formals.push_back(charptr);
    formals.push_back(charptr);
    formals.push_back(charptr);
    FunctionType *assert_in_bounds_ty = FunctionType::get(voidty, formals, false);
    FunctionCallee wrapper_assert_in_bounds = mod.getOrInsertFunction(
                                              "__prof_assert_in_bounds", assert_in_bounds_ty);
    assert_in_bounds = cast<Constant>(wrapper_assert_in_bounds.getCallee());

    formals.clear();
    formals.push_back(charptr);
    FunctionType *possible_leak_ty = FunctionType::get(voidty, formals, false);
    FunctionCallee wrapper_possible_leak = mod.getOrInsertFunction(
                                            "__prof_possible_allocation_leak", possible_leak_ty);
    possible_leak = cast<Constant>(wrapper_possible_leak.getCallee());

    formals.clear();
    formals.push_back(u64);
    FunctionType *malloc16_ty = FunctionType::get(charptr, formals, false);
    FunctionCallee wrapper_malloc16 = mod.getOrInsertFunction("__prof_malloc_align16", malloc16_ty);
    malloc16 = cast<Constant>(wrapper_malloc16.getCallee());

    formals.clear();
    formals.push_back(u64);
    formals.push_back(u64);
    FunctionType *calloc16_ty = FunctionType::get(charptr, formals, false);
    FunctionCallee wrapper_calloc16 = mod.getOrInsertFunction("__prof_calloc_align16", calloc16_ty);
    calloc16 = cast<Constant>(wrapper_calloc16.getCallee());

    formals.clear();
    formals.push_back(charptr);
    formals.push_back(u64);
    FunctionType *realloc16_ty = FunctionType::get(charptr, formals, false);
    FunctionCallee wrapper_realloc16 = mod.getOrInsertFunction("__prof_realloc_align16", realloc16_ty);
    realloc16 = cast<Constant>(wrapper_realloc16.getCallee());


    for(Module::iterator i=mod.begin(), e=mod.end(); i!=e; ++i)
      runOnFunction(&*i, mloops);

    LLVM_DEBUG(errs() << "Instrumented all functions\n");

    do_init(mod, globals);

    realign_globals(globals);

    return true;
  };

private:
  Type *voidty, *charptr;
  IntegerType *u32, *u64;
  Constant *prof_malloc, *prof_realloc, *prof_free, *prof_free_alloca, *prof_begin_iter, *prof_end_iter;
  Constant *report_constant, *prof_report_constant_string, *report_global, *report_stack;
  Constant *begin_fcn, *end_fcn, *begin_profiling;
  Constant *manage_argv, *unmanage_argv, *assert_in_bounds, *possible_leak;
  Constant *find_underlying, *predictable_int_value, *predictable_ptr_value, *residue_fcn;
  Constant *load_and_predict_int, *load_and_predict_ptr;
  Constant *unmanaged_fopen_name, *unmanaged_library_constant_au_name;
  Constant *malloc16, *calloc16, *realloc16;
  std::map<Instruction*, std::string> Instruction2StringName;
  std::map<Instruction*, Value *> Instruction2ValueName;
  StringSet recognizedExternalFunctions;

  template <class AllocaOrGlobalValue>
  bool realign_object(AllocaOrGlobalValue *object)
  {
    const unsigned alignment = object->getAlignment();
    const unsigned new_alignment = lcm( std::max(1u,alignment), 16 );
    if( alignment != new_alignment )
    {
      object->setAlignment(new_alignment);
      ++numRealigned;
      return true;
    }
    return false;
  }

  bool realign_globals(std::vector<GlobalVariable*> &globals)
  {
    if( DontTrackResidues )
      return false;

    bool modified = false;
    for(std::vector<GlobalVariable*>::iterator i=globals.begin(), e=globals.end(); i!=e; ++i)
      modified |= realign_object(*i);

    return modified;
  }

  bool realign_instructions(Function *fcn)
  {
    if( DontTrackResidues )
      return false;

    // Allocas and calls to malloc, calloc, realloc, ...
    bool modified = false;
    std::set<Instruction*> toBeReplacedInsts;
    for(Function::iterator i=fcn->begin(), e=fcn->end(); i!=e; ++i)
    {
      BasicBlock *bb = &*i;
      for(BasicBlock::iterator j=bb->begin(), z=bb->end(); j!=z; ++j)
      {
        Instruction *inst = &*j;

        if( AllocaInst *alloca = dyn_cast<AllocaInst>(inst) )
          modified |= realign_object(alloca);

        CallSite cs = getCallSite(inst);
        if( cs.getInstruction() )
        {
          if( Indeterminate::isMalloc(cs) )
          {
            ++numRealigned;
            if( Indeterminate::isNewNoThrow(cs) )
            {
              // this is to avoid invalidate the bb iterator
              toBeReplacedInsts.insert(inst);
              modified = true;
            }
            else {
              cs.setCalledFunction(malloc16);
            }
            modified = true;
          }
          else if( Indeterminate::isCalloc(cs) )
          {
            ++numRealigned;
            cs.setCalledFunction(calloc16);
            modified = true;
          }
          else if( Indeterminate::isRealloc(cs) )
          {
            ++numRealigned;
            cs.setCalledFunction(realloc16);
            modified = true;
          }
        }

      }
    }

    // replace all No Throw New and New[]
    for (Instruction* inst : toBeReplacedInsts){
      CallSite cs = getCallSite(inst);
      // need to ignore the second actual arg
      SmallVector<Value *, 8> Args(cs.arg_begin(), cs.arg_end() - 1); // remove the last arg
      CallInst *newCall= CallInst::Create(malloc16, Args);

      BasicBlock::iterator ii(inst);
      ReplaceInstWithInst(inst->getParent()->getInstList(), ii,
          newCall);
    }

    return modified;
  }


  bool mark_unrecognized_external_calls(ValSet &externalCalls)
  {
    bool modified = false;
    for(ValSet::iterator i=externalCalls.begin(), e=externalCalls.end(); i!=e;  ++i)
    {
      CallSite cs = getCallSite( *i );

      modified |= mark_unrecognized_external_call(cs);
    }
    return modified;
  }

  ValSet already_warned;
  bool mark_unrecognized_external_call(CallSite &cs)
  {
    Value *callee = cs.getCalledValue();
    if( isa<Function>(callee) && ! already_warned.count( callee ) )
    {
      errs() << "This profile might be incomplete: calling ";
      if( callee->hasName() )
        errs() << callee->getName();
      else
        errs() << *callee;
      errs() << " may introduce "
             << "new AUs (see lib/PointsToProfiler/RecognizedExternalFunctions.h)\n";

      already_warned.insert( callee );
    }

    Instruction *inst = cs.getInstruction();
    Value *actuals[] = { getInstName(inst) };

    InstInsertPt::Before(inst) << CallInst::Create(possible_leak, ArrayRef<Value*>( &actuals[0], &actuals[1]));

    return true;
  }

  bool do_init(Module &mod, const std::vector<GlobalVariable*> &globals)
  {
    std::vector<Type *> formals;
    FunctionType *vfv = FunctionType::get(voidty, formals, false);
    Function *initializer = Function::Create(vfv, GlobalValue::InternalLinkage, "spec_priv_startup", &mod);
    LLVMContext &ctx = mod.getContext();
    BasicBlock *entry = BasicBlock::Create(ctx, "entry", initializer);
    InstInsertPt startup = InstInsertPt::Beginning(entry);

    // Initialize the profiling library.
    startup << CallInst::Create(begin_profiling);

    // Register the (base,size) of each global variable.
    const DataLayout &td = mod.getDataLayout();
    for(std::vector<GlobalVariable*>::const_iterator i=globals.begin(), e=globals.end(); i!=e; ++i)
    {
      GlobalVariable *gv = *i;
      if( gv->getName() == "llvm.global_ctors" || gv->getName() == "llvm.global_dtors" )
      {
        // Don't do this.  Creating a 'use' for these globals will break shit.
        continue;
      }

      Value *name = getGlobalName(gv);
      PointerType *pty = cast< PointerType >( gv->getType() );
      Instruction *cast = new BitCastInst(gv, charptr);
      startup << cast;
      ConstantInt *sz = ConstantInt::get(u64, td.getTypeStoreSize( pty->getElementType() ) );

      Value *actuals[3] = { name, cast, sz };

      if( gv->isConstant() )
      {
        startup << CallInst::Create(
          report_constant, ArrayRef<Value*>(&actuals[0], &actuals[3]));
        ++numConstants;
      }

      else
      {
        startup << CallInst::Create(
          report_global, ArrayRef<Value*>(&actuals[0], &actuals[3]) );
        ++numGlobal;
      }

      // Special case: stdin, stdout, stderr
      if( gv->getName() == "stdin"
      ||  gv->getName() == "stdout"
      ||  gv->getName() == "stderr" )
      {
        LoadInst *load = new LoadInst(gv);
        CastInst *cast = new BitCastInst(load, charptr);

        PointerType *fileptr = dyn_cast< PointerType >( pty->getElementType() );
        ConstantInt *sz = ConstantInt::get(u64, td.getTypeStoreSize( fileptr->getElementType() ) );

        Value *actuals[3] = { fileptr_au_name(mod), cast, sz };
        startup << load << cast << CallInst::Create(prof_malloc, ArrayRef<Value*>(&actuals[0], &actuals[3]) );
      }
    }

    // Add a return inst.
    startup << ReturnInst::Create(ctx);

    // Ensure this runs at program startup.
    callBeforeMain(initializer);
    return true;
  }

  bool isInductionVariable(PHINode *phi, ScalarEvolution &scev, Loop *loop)
  {
    // fast test.
    PHINode *canon = loop->getCanonicalInductionVariable();
    if( phi == canon )
      return true;

    // slow test.
    assert( phi->getParent() == loop->getHeader() );

    if( !scev.isSCEVable( phi->getType() ) )
      return false;

    const SCEV *s = scev.getSCEV(phi);
    return scev.hasComputableLoopEvolution(s,loop);
  }

  bool instrumentLoopCarriedRegs(Loop *loop, ScalarEvolution &scev, ValSet &already, BBSet &interesting)
  {
    BasicBlock *header = loop->getHeader();

    // Instrument all loop-carried register values that are not
    // easily predicted as induction variables.  Copy them to
    // a temporary collection so we don't invalidate the basicblock::iterator
    std::vector<PHINode*> phis;
    for(BasicBlock::iterator i=header->begin(), e=header->end(); i!=e; ++i)
    {
      PHINode *phi = dyn_cast< PHINode >( &*i );
      if( !phi )
        break;

      // skip induction variables
      if( isInductionVariable(phi,scev,loop) )
        continue;

      phis.push_back(phi);
    }

    bool modified = false;
    InstInsertPt afterPHIs = InstInsertPt::End( header );
    while( !phis.empty() )
    {
      PHINode *phi = phis.back();
      phis.pop_back();

      if( already.count(phi) )
        continue;
      already.insert(phi);

      LLVM_DEBUG(errs() << "Instrumenting loop-carried register of loop " << header->getName() << ": " << phi->getName() << '\n');
      Value *phi_name = getInstName(phi);
      instrumentPredictableValue(phi_name, phi, afterPHIs, interesting);
      modified = true;
    }

    return modified;
  }

  bool hasInterestingStructure(Loop *loop, const BBSet &interesting)
  {
    for(Loop::block_iterator i=loop->block_begin(), e=loop->block_end(); i!=e; ++i)
    {
      BasicBlock *bb = *i;

      if( interesting.count( bb ) )
      {
//        errs() << "Loop " << loop->getHeader() << " is interesting because it contains an interesting block:\n" << *bb << "\n\n";
        return true;
      }
    }

    for(Loop::block_iterator i=loop->block_begin(), e=loop->block_end(); i!=e; ++i)
    {
      BasicBlock *bb = *i;

      for(BasicBlock::iterator j=bb->begin(), z=bb->end(); j!=z; ++j)
      {
        Instruction *inst = &*j;
        CallSite cs = getCallSite(inst);
        if( !cs.getInstruction() )
          continue;

        Function *fcn = cs.getCalledFunction();
        if( !fcn )
          return true;

        if( !fcn->isDeclaration() )
          return true;
      }
    }
    return false;
  }

  bool instrumentLoopStructure(Loop *loop, LoopInfo &li, const BBSet &interesting)
  {
    // We only need to instrument loops if they contain operations
    // which may produce instrumentation events.  Specifically,
    // if (1) we have instrumented an instruction within the loop, or
    // (2) the loop calls a defined function.

    if( !hasInterestingStructure(loop,interesting) )
    {
      LLVM_DEBUG(errs() << "Not instrumenting structure of non-interesting loop " << loop->getHeader()->getName() << '\n');
      return false;
    }

    BasicBlock *header = loop->getHeader();
    Value *name = getLoopName(loop);

    // Instrument loop exits.
    typedef SmallVector<BasicBlock*,2> SVB;
    SVB exiting;
    loop->getExitingBlocks(exiting);
    for(SVB::iterator i=exiting.begin(), e=exiting.end(); i!=e; ++i)
    {
      BasicBlock *exiter = *i;
      Instruction *term = exiter->getTerminator();
      std::set<BasicBlock*> notAnExit;
      for(unsigned sn=0; sn < term->getNumSuccessors(); ++sn)
      {
        BasicBlock *exit = term->getSuccessor(sn);
        if( !loop->contains(exit) )
        {
          if( ! notAnExit.count(exit) )
          {
            // Loop exit edge.
            BasicBlock *splitb = split(exiter,sn, "loop.exit", &li);
            notAnExit.insert(splitb);
            CallInst::Create(prof_end_iter, name, "", splitb->getTerminator() );
          }
        }
      }
    }

    // Instrument loop back-edges
    typedef std::vector<BasicBlock*> BlockList;
    BlockList preds( pred_begin(header), pred_end(header) );
    for(BlockList::iterator i=preds.begin(), e=preds.end(); i!=e; ++i)
    {
      BasicBlock *pred = *i;

      if( loop->contains(pred) )
      {
        // loop back edge.

        Instruction *term = pred->getTerminator();
        for(unsigned sn=0, N=term->getNumSuccessors(); sn<N; ++sn)
        {
          if( term->getSuccessor(sn) == header )
          {
            BasicBlock *splitb = split(pred,sn, "backedge", &li);
            CallInst::Create(prof_end_iter, name, "", splitb->getTerminator() );
          }
        }
      }
    }

    // Instrument loop header.
    CallInst::Create(prof_begin_iter, name, Twine(""), header->getFirstNonPHI());

    return true;
  }

  void loadAndPredictValue(Value *name, Value *ptr, InstInsertPt &where, BBSet &interesting)
  {
    PointerType *pty = cast<PointerType>( ptr->getType() );
    Type *elty = pty->getElementType();

    if( pty != charptr )
    {
      Instruction *cast = new BitCastInst(ptr, charptr);
      where << cast;
      ptr = cast;
    }

    if( elty->isPointerTy() )
    {
      Value *actuals[] = { name, ptr };
      where << CallInst::Create(load_and_predict_ptr, ArrayRef<Value*>(&actuals[0], &actuals[2]) );

      interesting.insert( where.getBlock() );
      ++numValuePredictors;
    }
    else if( IntegerType *intty = dyn_cast< IntegerType >(elty) )
    {
      const unsigned sz = std::max( intty->getBitWidth() / 8, 1u );
      Value *actuals[] = { name, ptr, ConstantInt::get(u32, sz) };
      where << CallInst::Create(load_and_predict_int, ArrayRef<Value*>(&actuals[0], &actuals[3]) );

      interesting.insert( where.getBlock() );
      ++numValuePredictors;
    }
  }

  // Instrument 'value' at 'where'.
  // Record these observations under the name 'name'.
  // Add the modified block to the set 'interesting'
  void instrumentPredictableValue(Value *name, Value *value, InstInsertPt &where, BBSet &interesting)
  {
    if( IntegerType *ity = dyn_cast< IntegerType >( value->getType() ) )
    {
      if( ity->getBitWidth() > 64 )
      {
        // decline to instrument ;)
        LLVM_DEBUG(errs() << "I decline to instrument a large integer value: " << *value << '\n');
        return;
      }
      else if( ity->getBitWidth() < 64 )
      {
        Instruction *cast = new ZExtInst(value, u64);
        where << cast;

        value = cast;
      }

      Value *actuals[] = { name, value };
      where << CallInst::Create(predictable_int_value, ArrayRef<Value*>(&actuals[0], &actuals[2]) );

      interesting.insert( where.getBlock() );
      ++numValuePredictors;
    }
    else if( value->getType()->isPointerTy() )
    {
      Instruction *cast = new BitCastInst(value, charptr);
      Value *actuals[] = { name, cast };
      where << cast << CallInst::Create(predictable_ptr_value, ArrayRef<Value*>(&actuals[0], &actuals[2]) );

      interesting.insert( where.getBlock() );
      ++numValuePredictors;
    }
  }

  bool runOnBlock(BasicBlock *bb, BBSet &interesting, LoopInfo &li)
  {
    bool modified = false;

    Module &mod = *( bb->getParent()->getParent() );

    IList mallocs, reallocs, frees, allocas, fopens, fcloses, conststrings;

    for(BasicBlock::iterator i=bb->begin(), e=bb->end(); i!=e; ++i)
    {
      Instruction *inst = &*i;

      if( isa< AllocaInst >(inst) )
      {
        allocas.push_back(inst);
        continue;
      }

      CallSite cs = getCallSite(inst);
      if( !cs.getInstruction() )
        continue;

      if( Indeterminate::isMallocOrCalloc(cs) )
        mallocs.push_back(inst);
      else if( Indeterminate::isRealloc(cs) )
        reallocs.push_back(inst);
      else if( Indeterminate::isFree(cs) )
        frees.push_back(inst);
      else if( Indeterminate::returnsNewFilePointer(cs) )
        fopens.push_back(inst);
      else if( Indeterminate::closesFilePointer(cs) )
        fcloses.push_back(inst);
      else if( Indeterminate::returnsLibraryConstantString(cs) )
        conststrings.push_back(inst);
    }

    for(IList::iterator i=mallocs.begin(), e=mallocs.end(); i!=e; ++i)
    {
      Instruction *inst = *i;
      CallSite cs = getCallSite(inst);

      InstInsertPt where;
      if( InvokeInst *invoke = dyn_cast< InvokeInst >(inst) )
      {
        BasicBlock *not_exception = split( invoke->getParent(), 0u, "non-exceptional-return", &li);
        where = InstInsertPt::Beginning( not_exception );
      }
      else
        where = InstInsertPt::After(inst);

      Value *sz = 0;
      if( Indeterminate::isMalloc( cs ) )
      {
        sz = cs.getArgument(0);
      }
      else // calloc
      {
        Value *n_elt = cs.getArgument(0);
        Value *s_elt = cs.getArgument(1);

        Instruction *mul = BinaryOperator::CreateNSWMul(n_elt, s_elt);
        where << mul;

        sz = mul;
      }

      Value *name = getInstName(inst);

      Instruction *cast = new BitCastInst(inst, charptr);
      where << cast;

      if( sz->getType() != u64 )
      {
        Instruction *extend = new ZExtInst(sz,u64);
        where << extend;
        sz = extend;
      }

      Value *actuals[] = { name, cast, sz };
      where << CallInst::Create(prof_malloc, ArrayRef<Value*>(&actuals[0], &actuals[3]) );

      ++numMalloc;
      modified = true;
    }

    for(IList::iterator i=reallocs.begin(), e=reallocs.end(); i!=e; ++i)
    {
      Instruction *inst = *i;
      CallSite cs = getCallSite(inst);

      Value *oldptr = cs.getArgument(0);
      Value *sz = cs.getArgument(1);
      Value *name = getInstName(inst);


      InstInsertPt where = InstInsertPt::After(inst);

      Instruction *cast_old = new BitCastInst(oldptr, charptr);
      where << cast_old;

      Instruction *cast_new = new BitCastInst(inst, charptr);
      where << cast_new;

      if( sz->getType() != u64 )
      {
        Instruction *extend = new ZExtInst(sz,u64);
        where << extend;
        sz = extend;
      }

      Value *actuals[] = { name, cast_old, cast_new, sz };
      where << CallInst::Create(prof_realloc, ArrayRef<Value*>(&actuals[0], &actuals[4]) );

      ++numMalloc;
      modified = true;
    }



    for(IList::iterator i=frees.begin(), e=frees.end(); i!=e; ++i)
    {
      Instruction *inst = *i;
      CallSite cs = getCallSite(inst);

      Value *ptr = cs.getArgument(0);

      modified |= reportPointerFreed(ptr, inst, inst, prof_free);
    }

    // Track fopen(), fclose().
    // This is not necessary for correctness.
    // But, it's nice to be able to track /every/ object used by a program.
    for(IList::iterator i=fopens.begin(), e=fopens.end(); i!=e; ++i)
    {
      Instruction *inst = *i;
      //errs() << "tracking fopens fclose at " << *inst << '\n';
      InstInsertPt where;
      if( InvokeInst *invoke = dyn_cast< InvokeInst >(inst) )
      {
        BasicBlock *not_exception = split( invoke->getParent(), 0u, "non-exceptional-return-malloc", &li);
        where = InstInsertPt::Beginning( not_exception );
      }
      else where = InstInsertPt::After(inst);

      ConstantInt *sz = ConstantInt::get(u64, 1UL);

      Value *vv = inst;
      if( vv->getType()->isIntegerTy() )
      {
        // Although fdopen() should return a FILE*,
        // I've found an example in 400.perlbmk where
        // fdopen() returns an int... weird, but we should
        // handle it anyway...
        Instruction *cast = new IntToPtrInst(inst, charptr);
        where << cast;
        vv = cast;
      }

      if( vv->getType() != charptr )
      {
        Instruction *cast = new BitCastInst(vv, charptr);
        where << cast;
        vv = cast;
      }

      Value *actuals[] = { fileptr_au_name(mod), vv, sz };
      where << CallInst::Create(prof_malloc, ArrayRef<Value*>(&actuals[0], &actuals[3]) );

      modified = true;
    }
    for(IList::iterator i=fcloses.begin(), e=fcloses.end(); i!=e; ++i)
    {
      Instruction *inst = *i;
      CallSite cs = getCallSite(inst);

      Value *ptr = cs.getArgument(0);
      Value *name = getInstName(inst);

      Instruction *cast = new BitCastInst(ptr, charptr);

      Value *actuals[] = { name, cast };
      InstInsertPt::Before(inst) << cast << CallInst::Create(prof_free, ArrayRef<Value*>(&actuals[0], &actuals[2]) );

      modified = true;
    }

    for(IList::iterator i=conststrings.begin(), e=conststrings.end(); i!=e; ++i)
    {
      Instruction *inst = *i;

      InstInsertPt where = InstInsertPt::After(inst);

      Value *ptr = inst;
      if( ptr->getType() != charptr )
      {
        Instruction *cast = new BitCastInst(ptr, charptr);
        where << cast;
        ptr = cast;
      }

      Value *name = library_constant_string_au_name(mod);
      Value *actuals[] = { name, ptr };
      where << CallInst::Create(prof_report_constant_string, ArrayRef<Value*>(&actuals[0], &actuals[2]) );
    }

    const DataLayout &td = mod.getDataLayout();
    for(IList::iterator i=allocas.begin(), e=allocas.end(); i!=e; ++i)
    {
      AllocaInst *inst = cast<AllocaInst>( *i );

      modified |= runOnAlloca(inst, td, interesting);
    }

    if( modified )
      interesting.insert(bb);

    return modified;
  }

  void findLifetimeMarkers(Value *ptr, ValSet &already, IList &starts, IList &ends)
  {
    if( already.count(ptr) )
      return;
    already.insert(ptr);

    // Find every use of this pointer, including casts.
    for(Value::user_iterator i=ptr->user_begin(), e=ptr->user_end(); i!=e; ++i)
    {
      User *user = &**i;

      if( BitCastInst *cast = dyn_cast< BitCastInst >(user) )
        findLifetimeMarkers(cast, already, starts, ends);

      else if( IntrinsicInst *intrin = dyn_cast< IntrinsicInst >(user) )
      {
        if( intrin->getIntrinsicID() == Intrinsic::lifetime_start )
          starts.push_back(intrin);
        else if( intrin->getIntrinsicID() == Intrinsic::lifetime_end )
          ends.push_back(intrin);
      }
    }
  }

  bool runOnAlloca(AllocaInst *alloca, const DataLayout &dl, BBSet &interesting)
  {
    bool modified = false;

    // An alloca may have explicitly marked lifetimes.
    ValSet avoidInfiniteRecursion;
    IList starts, ends;
    findLifetimeMarkers(alloca, avoidInfiniteRecursion, starts, ends);

    // Does it mark the start of lifetime?
    if( starts.empty() )
      starts.push_back(alloca);
    for(unsigned i=0, N=starts.size(); i<N; ++i)
      modified |= reportStartOfAllocaLifetime(alloca, dl, starts[i], interesting);

    // Does it mark the end of lifetime?
    for(unsigned i=0, N=ends.size(); i<N; ++i)
      modified |= reportEndOfAllocaLifetime(alloca, ends[i], interesting);

    return modified;
  }

  bool reportStartOfAllocaLifetime(AllocaInst *alloca, const DataLayout &dl,
                                   Instruction *start, BBSet &interesting) {
    InstInsertPt where = InstInsertPt::After(start);

    Instruction *cast = new BitCastInst(alloca, charptr);

    Value *name = getInstName(alloca);
    Value *array_sz = alloca->getArraySize();

    if( array_sz->getType() != u64 )
    {
      Instruction *c2 = new ZExtInst(array_sz,u64);
      where << c2;
      array_sz = c2;
    }

    ConstantInt *elt_sz = ConstantInt::get(u64, dl.getTypeStoreSize( alloca->getAllocatedType() ) );

    Value *actuals[4] = {name, cast, array_sz, elt_sz };
    where << cast << CallInst::Create(report_stack, ArrayRef<Value*>(&actuals[0], &actuals[4]) );
    interesting.insert( where.getBlock() );

    ++numStack;
    return true;
  }

  bool reportEndOfAllocaLifetime(AllocaInst *alloca, Instruction *end, BBSet &interesting)
  {
    if( reportPointerFreed(alloca, end, end, prof_free_alloca) )
    {
      interesting.insert( end->getParent() );
      return true;
    }
    return false;
  }

  bool reportPointerFreed(Value *ptr, Instruction *byWhom, Instruction *where, Constant *free)
  {
    Value *name = getInstName(byWhom);

    Instruction *cast = new BitCastInst(ptr, charptr);

    Value *actuals[] = { name, cast };
    InstInsertPt::Before(where) << cast << CallInst::Create(free, ArrayRef<Value*>(&actuals[0], &actuals[2]) );

    ++numFree;
    return true;
  }

  void instrumentArgumentForIndeterminateBase(const Argument *a, BBSet &interesting)
  {
    Argument *arg = const_cast< Argument* >(a);
    Function *fcn = arg->getParent();

    Instruction *cast = new BitCastInst(arg, charptr);
    Value *args[] = {getArgName(arg), cast };
    InstInsertPt::Beginning(fcn) << cast << CallInst::Create(find_underlying, ArrayRef<Value*>(&args[0], &args[2]) );

    interesting.insert( cast->getParent() );
    ++numIndeterminate;
  }

  void instrumentInstructionForIndeterminateBase(const Instruction *i, BBSet &interesting, LoopInfo &li)
  {
    Instruction *inst = const_cast< Instruction* >(i);

    Instruction *cast = new BitCastInst(inst, charptr);
    Value *args[] = {getInstName(inst), cast };

    InstInsertPt where;
    if( InvokeInst *invoke = dyn_cast< InvokeInst >(inst) )
    {
      //errs() << "indeterminate base object " << *inst << "is invoke" << "\n";
      BasicBlock *not_exception = split( invoke->getParent(), 0u, "non-exceptional-return",  &li);
      where = InstInsertPt::Beginning( not_exception );
    }
    else if( PHINode *phi = dyn_cast< PHINode >(inst) )
    {
      // Don't accidentally insert instrumentation before
      // later PHIs or landing pad instructions.
      where = InstInsertPt::Beginning( phi->getParent() );
    }
    else
      where = InstInsertPt::After(inst);

    where << cast << CallInst::Create(find_underlying, ArrayRef<Value*>(&args[0], &args[2]) );

    interesting.insert( cast->getParent() );
    ++numIndeterminate;
  }

  bool instrumentIndeterminatePointers(Function *fcn, ValSet &already, BBSet &interesting, LoopInfo &li)
  {
    // For each used pointer,
    // find the pointer's underlying object(s).
    // If indeterminate, add it to the set.
    // - we say a pointer is used if it is an operand
    //    to load or store, or if it is a pointer-typed
    //    operand to an externally defined function.
    UO indeterminate_pointers, indeterminate_objects;
    for(Function::iterator i=fcn->begin(), e=fcn->end(); i!=e; ++i)
    {
      BasicBlock &bb = *i;
      Indeterminate::findIndeterminateObjects(bb, indeterminate_pointers, indeterminate_objects);
    }

    // Instrument each of those indeterminate objects.
    // For arguments, instrument them in function entry.
    // For instructions, instrument them after their definition.
    bool modified = false;

    for(UO::iterator i=indeterminate_objects.begin(), e=indeterminate_objects.end(); i!=e; ++i)
    {
      const Value *object = *i;

      if( const Argument *arg = dyn_cast< Argument >(object) )
      {
        if( already.count(arg) )
          continue;
        already.insert(arg);

        LLVM_DEBUG(errs() << "Instrumenting indeterminate base object in function argument " << *arg << "\n");
        instrumentArgumentForIndeterminateBase(arg, interesting);
        modified = true;
      }

      else if( const Instruction *inst = dyn_cast< Instruction >(object) )
      {
        if( already.count(inst) )
          continue;
        already.insert(inst);

        if (isa<AllocaInst>(inst))
          continue;

        LLVM_DEBUG(errs() << "Instrumenting indeterminate base object: " << *inst << '\n');
        instrumentInstructionForIndeterminateBase(inst, interesting, li);
        modified = true;
      }

//      else if( const UnaryConstantExpr *cast = dyn_cast< UnaryConstantExpr >(object) )
//      {
//        assert(false && "TODO");
//      }

      else
      {
        errs() << "What is: " << *object << '\n';
        assert(false && "Unknown object type?!?!");
      }
    }

    if( SanityCheckMode )
    {
      // Emit code which verifies that pointer arithmetic does
      // not escape allocation unit bounds.

      // For each indeterminate pointer
      for(UO::iterator i=indeterminate_pointers.begin(), e=indeterminate_pointers.end(); i!=e; ++i)
      {
        Value *ptr = const_cast<Value*>( *i );
        Instruction *inst = dyn_cast<Instruction>( ptr );
        if( !inst )
          continue;

        // For each base of that pointer
        UO pointers, bases;
        const DataLayout &DL = fcn->getParent()->getDataLayout();
        Indeterminate::findIndeterminateObjects(ptr, pointers, bases, DL);
        for(UO::iterator j=bases.begin(), z=bases.end(); j!=z; ++j)
        {
          Value *base = const_cast<Value*>( *j );
          if( ptr == base )
            continue;

          // Let's assert that this remains within bounds.
          Instruction *cast1 = new BitCastInst(base, charptr);
          Instruction *cast2 = new BitCastInst(ptr,  charptr);

          Value *actuals[] = { getInstName(inst), cast1, cast2 };
          Instruction *assert = CallInst::Create(assert_in_bounds, ArrayRef<Value*>(&actuals[0], &actuals[3]));

          InstInsertPt::After(inst) << cast1 << cast2 << assert;
          modified = true;
        }
      }
    }

    return modified;
  }

  // Give a consistent, repeatable name to every instruction
  // before we instrument anything, since instrumentation will
  // perturb these names.
  void generateInstructionNames(Function *fcn)
  {
    Instruction2StringName.clear();
    Instruction2ValueName.clear();

    for(Function::iterator i=fcn->begin(), e=fcn->end(); i!=e; ++i)
    {
      BasicBlock *bb = &*i;

      unsigned offsetWithinBlock = 0;
      for(BasicBlock::iterator j=bb->begin(), z=bb->end(); j!=z; ++j, ++offsetWithinBlock)
      {
        Instruction *inst = &*j;

        std::stringstream sout;
        sout << fcn->getName().str() << ' ' << bb->getName().str() << ' ';

        if( inst->hasName() )
          sout << inst->getName().str();

        else
          sout << '$' << offsetWithinBlock;

        Instruction2StringName[inst] = sout.str();
      }
    }
  }

  bool isInstrumentable(Type *ty) const
  {
    if( ty->isIntegerTy() )
      return true;
    else if( ty->isPointerTy() )
      return true;
    else
      return false;
  }

  bool instrumentIntegerArguments(Function *fcn, ValSet &already, BBSet &interesting)
  {
    InstInsertPt where = InstInsertPt::Beginning(fcn);

    bool modified = false;
    for(Function::arg_iterator i=fcn->arg_begin(), e=fcn->arg_end(); i!=e; ++i)
    {
      Argument *arg = &*i;
      if( arg->getType()->isIntegerTy() )
      {
        Value *arg_name = getArgName(arg);
        LLVM_DEBUG(errs() << "Instrumenting integer argument: " << *arg << '\n');
        instrumentPredictableValue(arg_name, arg, where, interesting);
        modified = true;
      }
    }

    return modified;
  }

  bool instrumentPointerResidue(Value *pointer, ValSet &already, BBSet &interesting, LoopInfo &li)
  {
    for(;;)
    {
      if( CastInst *cast = dyn_cast<CastInst>(pointer) )
      {
        pointer = cast->getOperand(0);
        continue;
      }

      else if( GetElementPtrInst *gep = dyn_cast<GetElementPtrInst>(pointer) )
      {
        if( gep->hasAllConstantIndices() )
        {
          pointer = gep->getPointerOperand();
          continue;
        }
      }

      break;
    }

    if( already.count(pointer) )
      return false;

    InstInsertPt where;
    Value *actuals[2];

    if( InvokeInst *invoke = dyn_cast<InvokeInst>(pointer) )
    {
      BasicBlock *not_exception = split( invoke->getParent(), 0u, "non-exceptional-return", &li);
      where = InstInsertPt::Beginning( not_exception );
      actuals[0] = getInstName(invoke);
    }
    else if( PHINode *phi = dyn_cast<PHINode>(pointer) )
    {
      // Don't accidentally insert instrumentation before
      // later PHIs or landing pad instructions.
      where = InstInsertPt::Beginning( phi->getParent() );
      actuals[0] = getInstName(phi);
    }
    else if( Instruction *def = dyn_cast<Instruction>(pointer) )
    {
      where = InstInsertPt::After(def);
      actuals[0] = getInstName(def);
    }
    else if( Argument *arg = dyn_cast<Argument>(pointer) )
    {
      where = InstInsertPt::Beginning(arg->getParent());
      actuals[0] = getArgName(arg);
    }
    else
      return false;

    actuals[1] = pointer;
    if( pointer->getType()->isIntegerTy() )
    {
      Instruction *cast = new IntToPtrInst(pointer,charptr);
      where << cast;
      actuals[1] = cast;
    }
    else if( pointer->getType() != charptr )
    {
      Instruction *cast = new BitCastInst(pointer,charptr);
      where << cast;
      actuals[1] = cast;
    }

    where << CallInst::Create(residue_fcn, ArrayRef<Value*>(&actuals[0], &actuals[2]) );

    ++numResidue;
    already.insert(pointer);
    interesting.insert( where.getBlock() );
    return true;
  }

  bool instrumentPointerResidues(Function *fcn, const IList &loads, const IList &stores, ValSet &already, BBSet &interesting, LoopInfo &li)
  {
    if( DontTrackResidues )
      return false;

    bool modified = false;
    // Instrument the pointer-operand of all loads/stores.
    for(IList::const_iterator i=loads.begin(), e=loads.end(); i!=e; ++i)
    {
      LoadInst *load = cast< LoadInst >( *i );
      modified |= instrumentPointerResidue(load->getPointerOperand(), already, interesting, li);
    }
    for(IList::const_iterator i=stores.begin(), e=stores.end(); i!=e; ++i)
    {
      StoreInst *store = cast< StoreInst >( *i );
      modified |= instrumentPointerResidue(store->getPointerOperand(), already, interesting, li);
    }

    return modified;
  }

  typedef std::multimap< Loop*, LoadInst* > LoadAtLoopSet;
  void identifyUpwardExposedLoads(const IList &all_loads, LoopInfo &li, LoadAtLoopSet &loads_out)
  {
    // First identify the upward exposed loads.
    // Copy them to a temporary collection 'loads_out'
    KillFlow &kill = getAnalysis< KillFlow >();

    // We want /ALL/ flow-killing queries from this function
    // to be done in MaxAnalysisTimePerFunction seconds.
    time_t queryStart=0;
    time(&queryStart);

    for(IList::const_iterator i=all_loads.begin(), e=all_loads.end(); i!=e; ++i)
    {
      LoadInst *load = cast< LoadInst >( *i );
      Value *ptr = load->getPointerOperand();

      // Does there exist a loop nest for which this
      // load is upward exposed.
      for(Loop *loop=li.getLoopFor(load->getParent()); loop; loop=loop->getParentLoop())
        if( ! kill.pointerKilledBefore(loop,ptr,load,false,queryStart,MaxAnalysisTimePerFunction) )
          loads_out.insert( std::make_pair(loop, load) );

      // Is it upward exposed in the function?
      if( !kill.pointerKilledBefore(0,ptr,load,false,queryStart,MaxAnalysisTimePerFunction) )
        loads_out.insert( std::make_pair((Loop*)0, load) );
    }
  }

  bool instrumentUpwardExposedLoads(Function *fcn, LoopInfo &li, const LoadAtLoopSet &upward_exposed_loads, ValSet &already, BBSet &interesting)
  {
    // Instrument all loads within the loop which are possibly live-in
    // to a loop / the function

    bool modified = false;

    // Instrument those loads which can be rematerialized
    Remat remat;

    for(LoadAtLoopSet::const_iterator i=upward_exposed_loads.begin(), e=upward_exposed_loads.end(); i!=e; ++i)
    {
      Loop *loop = i->first;
      LoadInst *load = i->second;

      Value *ptr = load->getPointerOperand();

      if( !isInstrumentable(ptr->getType() ) )
        continue;

      BasicBlock *head = 0;
      if( loop )
      {
        if( !remat.canRematInHeader(ptr, loop) )
          continue;

        head = loop->getHeader();
      }
      else
      {
        if( !remat.canRematAtEntry(ptr, fcn) )
          continue;

        head = &fcn->getEntryBlock();
      }
      assert( head );

      // At this point, we don't actually know that it is safe to
      // load from this pointer.  It's possible, for instance, that the
      // pointer is null, and that the host program would have checked
      // non-null before performing the load.  Since we have rematerialized
      // in header/entry, we have stripped away such guards...

      // Our approach: insert a new guard which only performs the load
      // and the callback if the underlying object of the pointer is not null.
      LLVMContext &ctx = fcn->getContext();
      BasicBlock *tail = head->splitBasicBlock( head->getTerminator(), head->getName() + ".tail" );
      BasicBlock *safe = BasicBlock::Create(ctx, "safe.load", head->getParent(), tail);
      LoopInfoBase<BasicBlock, Loop> &lib = *&li;
      if( loop )
      {
        loop->addBasicBlockToLoop(tail, lib);
        loop->addBasicBlockToLoop(safe, lib);
      }

      head->getTerminator()->eraseFromParent();
      InstInsertPt H = InstInsertPt::End(head);

      Value *new_ptr = remat.rematUnsafe(H, ptr, loop);
      const DataLayout &DL = fcn->getParent()->getDataLayout();
      Value *uo = GetUnderlyingObject(new_ptr, DL);

      Value *cond_not_equal_null = 0;
      if( isa< GlobalVariable >(uo) )
      {
        // GlobalVariables cannot have a null address.
        cond_not_equal_null = ConstantInt::getTrue(ctx);
      }
      else
      {
        // This test is silly, but is good-enough in practice.
        Instruction *cast = new PtrToIntInst( uo, u64 );
        ConstantInt *limit = ConstantInt::get(u64, 0x100);
        CmpInst *cmp = new ICmpInst(ICmpInst::ICMP_UGE, cast, limit);
        H << cast << cmp;
        cond_not_equal_null = cmp;
      }
      H << BranchInst::Create(safe, tail, cond_not_equal_null);

      InstInsertPt where = InstInsertPt::End(safe);

      Value *load_name = getInstName(load);
      /*
      if( ! NoUnsafe )
      {
        if( TraceBeforeUnsafe )
          insertPrintf(where, ("About to perform a possibly-unsafe load of " + ptr->getName() + " in " + where.getBlock()->getName() + "\n").str(), true);

        Instruction *new_load = new LoadInst(new_ptr);
        new_load->setName("load-for-instrument:" + load->getName() + ":");
        where << new_load;

        if( TraceBeforeUnsafe )
          insertPrintf(where, "/done\n", true);

        LLVM_DEBUG(errs() << "Instrumenting upward-exposed load: " << load->getName() << '\n');
        instrumentPredictableValue(load_name, new_load, where, interesting);
      }
      */
      // The runtime library is responsible for performing
      // the load from 'new_ptr' in a safe fashion.
      loadAndPredictValue(load_name, new_ptr, where, interesting);

      where << BranchInst::Create(tail);

      modified = true;
    }

    return modified;
  }

  bool runOnFunction(Function *fcn, ModuleLoops &mloops)
  {
    if( fcn->isDeclaration() )
      return false;

    LLVM_DEBUG(errs() << "Instrumenting function " << fcn->getName() << '\n');

    // Capture a list of all external function declarations,
    // loads, stores, etc, before we start adding any.
    ValSet externalCalls;
    IList loads, stores;
    for(Function::iterator i=fcn->begin(), e=fcn->end(); i!=e; ++i)
    {
      BasicBlock *bb = &*i;
      for(BasicBlock::iterator j=bb->begin(), z=bb->end(); j!=z; ++j)
      {
        Instruction *inst = &*j;

        if( LoadInst *load = dyn_cast<LoadInst>(inst) )
        {
          loads.push_back(load);
          continue;
        }
        else if( StoreInst *store = dyn_cast<StoreInst>(inst) )
        {
          stores.push_back(store);
          //errs() << "SpecPriv: Inserting store at Basic Block: " << bb->getName() << '\n';
          continue;
        }

        CallSite cs = getCallSite(inst);
        if( ! cs.getInstruction() )
          continue;

        // What does this callsite call? Skip casts.
        Value *fcn_ptr = cs.getCalledValue();
        for(;;)
        {
          if( ConstantExpr *cast = dyn_cast< ConstantExpr >(fcn_ptr) )
            if( cast->isCast() )
            {
              fcn_ptr = cast->getOperand(0);
              continue;
            }
          break;
        }
        if( Function *callee = dyn_cast<Function>(fcn_ptr) )
        {
          if( !callee->isDeclaration() )
            continue;

          int contains = 0;
          for(unsigned i=0; llvm_multi_type_function_list[i]; ++i){
            StringRef fcn_name = callee->getName();
            StringRef multi_type_fcn_name = StringRef(llvm_multi_type_function_list[i]);
            if(fcn_name.contains(multi_type_fcn_name))
              contains = 1;
          }
          if (contains)
            continue;

          if( recognizedExternalFunctions.count(callee->getName()) )
            //errs() << "SpecPriv: Inserting recognized external call at Basic Block: " << bb->getName() << '\n';
            continue;

        }
        //errs() << "SpecPriv: Inserting unrecognized external call at Basic Block: " << bb->getName() << '\n';
        externalCalls.insert(inst);
      }
    }

    generateInstructionNames(fcn);

    //ScalarEvolution &scev = getAnalysis< ScalarEvolutionWrapperPass >(*fcn).getSE();
    //LoopInfo &li = getAnalysis< LoopInfoWrapperPass >(*fcn).getLoopInfo();
    ScalarEvolution &scev = mloops.getAnalysis_ScalarEvolution(fcn);
    LoopInfo &li = mloops.getAnalysis_LoopInfo(fcn);

    LoadAtLoopSet upward_exposed_loads;
    identifyUpwardExposedLoads(loads, li, upward_exposed_loads);

    bool modified = false;

/*
    // Remove explicit lifetime markers
    modified |= removeLifetimeMarkers(fcn);
*/

    // set of basic blocks which may produce instrumentation events of any kind.
    BBSet interesting;

    ValSet already;
    //insert find_underlying object call
    modified |= instrumentIndeterminatePointers(fcn,already,interesting,li);

    if( VerifyOften && modified )
      verifyFunction(*fcn);

    already.clear();
    modified |= instrumentUpwardExposedLoads(fcn, li, upward_exposed_loads, already,interesting);

    if( VerifyOften && modified )
      verifyFunction(*fcn);

//    modified |= instrumentIntegerArguments(fcn, already, interesting);
    modified |= instrumentPointerResidues(fcn, loads, stores, already, interesting, li);

    if( VerifyOften && modified )
      verifyFunction(*fcn);

    // instrument all calls to alloca/malloc/free
    for(Function::iterator i=fcn->begin(), e=fcn->end(); i!=e; ++i)
      modified |= runOnBlock(&*i, interesting, li);

    if( VerifyOften && modified )
      verifyFunction(*fcn);

    // visit all loops in pre-order (top->down)
    typedef std::list<Loop*> Fringe;
    typedef std::vector<Loop*> Loops;
    Loops loops;
    Fringe fringe( li.begin(), li.end() );
    while( !fringe.empty() )
    {
      Loop *loop = fringe.front();
      fringe.pop_front();

      loops.push_back(loop);
      fringe.insert( fringe.end(), loop->begin(), loop->end() );
    }

    for(Loops::iterator i=loops.begin(), e=loops.end(); i!=e; ++i)
      modified |= instrumentLoopCarriedRegs(*i,scev,already, interesting);

    if( VerifyOften && modified )
      verifyFunction(*fcn);

    for(Loops::iterator i=loops.begin(), e=loops.end(); i!=e; ++i)
      modified |= instrumentLoopStructure(*i, li, interesting);

    if( VerifyOften && modified )
      verifyFunction(*fcn);

//    if( modified )
    {
      // Instrument function entry
      Value *actuals[] = { getFcnName(fcn) };
      InstInsertPt begin = InstInsertPt::Beginning(fcn);
      begin << CallInst::Create(begin_fcn, ArrayRef<Value*>(&actuals[0], &actuals[1]) );

      const bool isMain = ( fcn->getName() == "main" && fcn->arg_size() == 2 );

      if( isMain )
      {
        Value *actuals[] = { fcn->arg_begin(),
                             fcn->arg_begin() + 1 };
                             //fcn->arg_end() };

        begin << CallInst::Create(manage_argv,
          //ArrayRef<Value*>(actuals));
          ArrayRef<Value*>(&actuals[0], &actuals[2]) );
      }

      // Instrument function exit (return or unwind)
      for(Function::iterator i=fcn->begin(), e=fcn->end(); i!=e; ++i)
      {
        BasicBlock *bb = &*i;
        Instruction *term = bb->getTerminator();
        if(term && (isa< ReturnInst >(term) || isa< ResumeInst >(term)) )
        {
          // Mark end of function, so that we can clear stack
          // variables from our live list.
          InstInsertPt end = InstInsertPt::Before(term);

          if( isMain )
          {
            //Value *actuals[] = { fcn->arg_begin(),
            //                     fcn->arg_end() };
            Value *actuals[] = { fcn->arg_begin(),
                                 fcn->arg_begin() + 1 };
            //errs() << "SpecPriv: instrumenting function_main and bb: " << bb->getName() << '\n';
            end << CallInst::Create(unmanage_argv,
              ArrayRef<Value*>(&actuals[0], &actuals[2]));
          }

          end << CallInst::Create(end_fcn, ArrayRef<Value*>(&actuals[0], &actuals[1]) );
        }
      }
    }

    if( VerifyOften && modified )
      verifyFunction(*fcn);

    modified |= mark_unrecognized_external_calls(externalCalls);

    if( VerifyOften && modified )
      verifyFunction(*fcn);

    modified |= realign_instructions(fcn);

    if( VerifyOften && modified )
      verifyFunction(*fcn);

    if( modified && (
       (VerifyNesting)
    || (VerifyNestingOnly == fcn->getName()) ) )
      verifyNesting(fcn);

    return modified;
  }

  void verifyNesting(Function *fcn)
  {
    // Test the __prof_begin_iter / __prof_end_iter nesting relationship.
    typedef std::vector<Constant*> ContextStack;
    typedef std::vector<BasicBlock*> Path;
    typedef std::map<Path,ContextStack> Fringe;
    typedef std::map<BasicBlock*,ContextStack> Visited;
    Fringe fringe; // holds the set of growing paths and their respective contexts.
    Visited visited; // holds the context of any path to reach the block

    Path start;
    ContextStack empty;
    start.push_back( &fcn->getEntryBlock() );
    fringe[ start ] = empty;
    while( ! fringe.empty() )
    {
      Fringe::iterator any = fringe.begin();
      Path path = any->first; // copy construct
      ContextStack ctx = any->second; // copy construct
      fringe.erase(any);

      // Is there more than one path to this block?
      BasicBlock *bb = path.back();
      Visited::iterator v = visited.find( bb );
      if( v != visited.end() )
      {
        // Yes, there is.
        // Do these two paths give consistent contexts?
        const ContextStack &other = v->second;
        if( ctx != other )
        {
          // Two paths reach this block with different contexts,
          // yet have different contexts.
          errs() << "Nesting violation in function " << fcn->getName() << '\n'
                 << "Two paths reach block '" << bb->getName() << "'.\n"
                 << "One of them yields the context:\n";
          for(unsigned j=0, N=other.size(); j<N; ++j)
            errs() << "  " << *other[j] << '\n';
          errs() << "The other yields the context:\n";
          for(unsigned j=0, N=ctx.size(); j<N; ++j)
            errs() << "  " << *ctx[j] << '\n';
          errs() << "The second was observed along this path:\n";
          for(unsigned j=0, N=path.size(); j<N; ++j)
            errs() << "  " << path[j]->getName() << '\n';
          assert(false);

        }

        // No nesting violation; avoid duplicate paths, cycles.
        continue;
      }
      visited[ bb ] = ctx;

      for(BasicBlock::iterator i=bb->begin(), e=bb->end(); i!=e; ++i)
      {
        Instruction *inst = &*i;
        CallSite cs = getCallSite(inst);
        if( cs.getInstruction() )
        {
          Function *callee = cs.getCalledFunction();
          if( callee == prof_begin_iter|| callee == begin_fcn )
          {
            Constant *enter_loop = cast<Constant>(cs.getArgument(0));
            ctx.push_back( enter_loop );
          }
          else if( callee == prof_end_iter || callee == end_fcn )
          {
            Constant *exit_loop = cast<Constant>(cs.getArgument(0));
            if( ctx.back() == exit_loop )
            {
              // Correctly nested, good
              ctx.pop_back();
            }
            else
            {
              for(unsigned j=0, N=path.size(); j<N; ++j)
                errs() << *path[j];

              // Incorrect nesting detected.
              errs() << "Nesting violation in function " << fcn->getName() << '\n'
                     << "The context:\n";
              for(unsigned j=0, N=ctx.size(); j<N; ++j)
                errs() << "  " << *ctx[j] << '\n';
              errs() << "The observed along this path:\n";
              for(unsigned j=0, N=path.size(); j<N; ++j)
                errs() << "  " << path[j]->getName() << '\n';
              errs() << "And then tries to pop context:\n"
                     << "  " << *exit_loop<< '\n';
              assert(false);

            }
          }
        }
      }

      // Ensure empty context at function exit.
      Instruction *term = bb->getTerminator();
      if( isa<ReturnInst>( term ) || isa<ResumeInst>( term ) )
      {
        if( !ctx.empty() )
        {
          // Non-empty context at function return.
          errs() << "Non-empty context at function return in function " << fcn->getName() << '\n'
                 << "The context:\n";
          for(unsigned j=0, N=ctx.size(); j<N; ++j)
            errs() << "  " << *ctx[j] << '\n';
          errs() << "The observed along this path:\n";
          for(unsigned j=0, N=path.size(); j<N; ++j)
            errs() << "  " << path[j]->getName() << '\n';
          assert(false);
        }
      }

      // Add successor blocks.
      for(unsigned j=0, N=term->getNumSuccessors(); j<N; ++j)
      {
        BasicBlock *succ = term->getSuccessor(j);

        path.push_back(succ);
        fringe[ path ] = ctx;
        path.pop_back();
      }
    }
  }

  bool removeLifetimeMarkers(Function *fcn)
  {
    IList to_delete;
    for(Function::iterator i=fcn->begin(), e=fcn->end(); i!=e; ++i)
      findLifetimeMarkers(&*i, to_delete);

    for(unsigned i=0, N=to_delete.size(); i<N; ++i)
      to_delete[i]->dropAllReferences();
    for(unsigned i=0, N=to_delete.size(); i<N; ++i)
    {
      to_delete[i]->eraseFromParent();
      ++lifetimeMarkersRemoved;
    }

    return ! to_delete.empty();
  }

  void findLifetimeMarkers(BasicBlock *bb, IList &all)
  {
    for(BasicBlock::iterator i=bb->begin(), e=bb->end(); i!=e; ++i)
      if( IntrinsicInst *intrin = dyn_cast< IntrinsicInst >( &*i ) )
        if( intrin->getIntrinsicID() == Intrinsic::lifetime_start
        ||  intrin->getIntrinsicID() == Intrinsic::lifetime_end )
          all.push_back(intrin);
  }


  Value * getFcnName(Function *fcn) const
  {
    Module *mod = fcn->getParent();

    return getStringLiteralExpression(*mod, fcn->getName());
  }

  Value * getLoopName(Loop *loop) const
  {
    BasicBlock *header = loop->getHeader();
    Function *fcn = header->getParent();
    Module *mod = fcn->getParent();
    unsigned depth = loop->getLoopDepth();

    std::stringstream sout;
    sout << fcn->getName().str() << ' ' << header->getName().str() << ' ' << depth;
    return getStringLiteralExpression( *mod, sout.str() );
  }

  Value * getGlobalName(GlobalVariable *gv) const
  {
    Module *mod = gv->getParent();
    std::string name = "global " + gv->getName().str();
    return getStringLiteralExpression( *mod, name);
  }

  Value *getArgName(Argument *arg) const
  {
    Function *fcn = arg->getParent();
    Module *mod = fcn->getParent();

    std::ostringstream name;
    name << "argument " << fcn->getName().str() << " %" << arg->getArgNo();
    return getStringLiteralExpression( *mod, name.str() );
  }

  Value * getInstName(Instruction *inst)
  {
    BasicBlock *bb = inst->getParent();
    Function *fcn = bb->getParent();
    Module *mod = fcn->getParent();

    // Maybe create a new global constant string
    if( !Instruction2ValueName.count(inst) )
    {
      if( ! Instruction2StringName.count(inst) )
      {
        errs() << "In function: " << fcn->getName() << '\n'
               << "  In BB: " << bb->getName() << '\n'
               << "    Inst: " << *inst << '\n'
               << "   (ptr): " << (uint64_t)inst << '\n';
        assert( false && "No name generated for this instruction!?");
      }

      const std::string &name = Instruction2StringName[inst];
      Instruction2ValueName[inst] = getStringLiteralExpression(*mod, name);
    }

    return Instruction2ValueName[inst];
  }

};

char MallocProfiler::ID = 0;
static RegisterPass<MallocProfiler> mp("specpriv-profiler", "Malloc profiler");

}
}
