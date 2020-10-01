#define DEBUG_TYPE "ExternICallPromote"

#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/CallSite.h"

#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/IndirectCallVisitor.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Module.h"
#include "llvm/Pass.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"

#include "llvm/Analysis/LoopPass.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Analysis/Passes.h"

#include "liberty/Utilities/InstInsertPt.h"

#include <iostream>
#include <fstream>
#include <set>
#include <sstream>
#include <list>

#define PROF_FILE "extern_icall_prof.out"

namespace liberty
{
using namespace llvm;
using namespace std;
using namespace liberty;


  class ExternICallPromote: public ModulePass {
    public:

      static char ID;
      ExternICallPromote() : ModulePass(ID), valid(false) {}

      virtual bool runOnModule (Module &M);
      virtual void getAnalysisUsage(AnalysisUsage &AU) const;


    private:
      ifstream inFile;
      unsigned NumICall;
      unsigned MaxNumTarget;
      /** Is this profile valid? */
      bool valid;

      typedef std::vector<string> SymbolList;
      typedef std::vector<BasicBlock*> BlockList;
      bool promoteExternalCallsite(Instruction *inst, SymbolList &symbols, Module &M);
      Value* makeNewCall(const CallSite &cs, string *callee, BasicBlock *where,
        BasicBlock *after, PHINode *return_value_phi, Module &M);

  }; STATISTIC(numPromoted, "Num of callsites promoted");

char ExternICallPromote::ID = 0;

static RegisterPass<ExternICallPromote> RP10("extern-icall-prom",
    "(ExternICallPromote) External indirect call promotion", false, false);


void ExternICallPromote::getAnalysisUsage(AnalysisUsage &AU) const
{
  AU.setPreservesAll();
}

/*
 * promote the instruction with profiled symbols
 * for each instruction, get the callsite, and create N compare and call BBs
 */
bool ExternICallPromote::promoteExternalCallsite(Instruction *inst, SymbolList &symbols, Module &M) {
  CallSite cs(inst);
  const unsigned NumDirectCalls = symbols.size();
  const unsigned N_alternatives = NumDirectCalls + 1; 

  LLVM_DEBUG(errs() << "Transforming callsite (" << N_alternatives
      << ") ``" << *inst << "'':\n");

  if( NumDirectCalls == 0 ){
    LLVM_DEBUG(errs() << "-- No candidates found :(\n");
    return false;
  }

  // Split the basic block BEFORE the indirect function
  // call into two basic blocks: 'before' and 'after'
  BasicBlock *before = inst->getParent();
  BasicBlock *after = before->splitBasicBlock(inst, "after_indirect_call");

  // If the function has a return value, we will need to
  // PHI over each alternative.  This PHI will appear
  // at the top of 'after'
  PHINode *return_value_phi = 0;
  if( ! inst->use_empty() ){
    return_value_phi = PHINode::Create(
        inst->getType(), N_alternatives, "result_of_indirect_call");
    InstInsertPt::Beginning(after) << return_value_phi;
  }

  Value *fcn_ptr = cs.getCalledValue();
  Function *fcn = before->getParent();
  LLVMContext &ctx = fcn->getContext();

  // We will create a separate basic block 'alternatives[i]' for each of
  // the possible callees 'callees[i]'.  We populate those blocks with
  // a direct call, and then a branch to 'after'
  BasicBlock *default_case = 0;
  BlockList alternatives(NumDirectCalls);

  vector<Value *> callees(NumDirectCalls);
  for(unsigned i=0; i<NumDirectCalls; ++i) {
    string &symbol = symbols[i];

    alternatives[i] = BasicBlock::Create(
        ctx, "direct_call_to_" + symbol, fcn, after);

    callees[i] = makeNewCall(cs, &symbol, alternatives[i], after, return_value_phi, M);
  }

  // In all cases for external indirect call, we must emit a default case, which
  // falls-back to an indirect call.
  default_case = BasicBlock::Create(ctx, "default_indirect", fcn, after);

  makeNewCall(cs, nullptr, default_case, after, return_value_phi, M);

  // Add instructions to dispatch to the appropriate direct call.
  // a sequence of compare and branch instructions.
  BlockList compare_and_branch(NumDirectCalls);
  for(unsigned i=0; i<NumDirectCalls; ++i)
  {
    compare_and_branch[i] = BasicBlock::Create(
        ctx, "compare_to_" + symbols[i], fcn, after);
    compare_and_branch[i]->moveBefore( alternatives[i] );
  }

  // Stitch the compare-and-branch sequences together
  before->getTerminator()->setSuccessor(0, compare_and_branch[0]);
  for(unsigned i=0; i<NumDirectCalls; ++i)
  {
    BasicBlock *next = (i == NumDirectCalls-1) ? default_case : compare_and_branch[i+1];

    Constant *callee = dyn_cast<Constant>(callees[i]);
    if (!callee) {
      errs() << "Cast from Function to Constant  not success?\n";
      return false;
    }
    Instruction *cmp = new ICmpInst(ICmpInst::ICMP_EQ, fcn_ptr, callee);
    InstInsertPt::Beginning(compare_and_branch[i])
      << cmp << BranchInst::Create(alternatives[i], next, cmp);
  }

  // Replace all uses with the PHI.
  if( return_value_phi )
    inst->replaceAllUsesWith( return_value_phi );

  // Remove the old call.
  if( InvokeInst *invoke = dyn_cast< InvokeInst >(inst) )
  {
    // if it was an Invoke, then we need to put
    // a new terminator instruction in 'after'.
    BasicBlock *normal = invoke->getNormalDest(),
               *exceptional = invoke->getUnwindDest();
    exceptional->removePredecessor(after);
    invoke->eraseFromParent();
    InstInsertPt::End(after) << BranchInst::Create(normal);
  }
  else
    inst->eraseFromParent();

  return true;
}

// Create a new call or invoke instruction modeled after the callsite
// 'cs'.  Place this new call in 'where'.  Branches to 'after', and
// update 'return_value_phi' if supplied.  If 'callee' is supplied,
// redirect the call to 'callee'.
Value* makeNewCall(const CallSite &cs, string *callee, BasicBlock *where,
    BasicBlock *after, PHINode *return_value_phi, Module &M)
{
  Value *fcn_ptr = cs.getCalledValue();
  Instruction *inst = cs.getInstruction();

  // Copy actual parameters.
  const SmallVector<Value*,4> actuals(cs.arg_begin(), cs.arg_end());

  // What are we going to call?
  Value *target = fcn_ptr;
  if( callee )
  {
    // Declare this function and get callee
    target = M.getOrInsertFunction(*callee, fcn_ptr->getType()).getCallee();
  }
  else {
    target = fcn_ptr; // default case
  }

  // If this callsite never unwinds
  Value *return_value = 0;
  if( isa< CallInst >(inst) )
  {
    // Create and insert new call instruction
    Instruction *new_call = CallInst::Create(
        target, ArrayRef<Value*>(actuals) );

    new_call->setDebugLoc(inst->getDebugLoc());

    return_value = new_call;

    // Branch to 'after'
    InstInsertPt::End( where )
      << new_call << BranchInst::Create( after );
  }

  // Otherwise, if this callsite may unwind.
  else if( InvokeInst *invoke = dyn_cast< InvokeInst >(inst) )
  {
    BasicBlock *exceptional = invoke->getUnwindDest();

    // Create a new invoke
    InvokeInst *new_invoke = InvokeInst::Create(
        target, after, exceptional, ArrayRef<Value*>(actuals) );

    new_invoke->setDebugLoc(inst->getDebugLoc());

    // Update PHIs in the exceptional destination
    for(BasicBlock::iterator j=exceptional->begin(), z=exceptional->end(); j!=z; ++j)
    {
      PHINode *ephi = dyn_cast< PHINode >( &*j );
      if( !ephi )
        break;

      unsigned idx = ephi->getBasicBlockIndex( invoke->getParent() );
      Value *incoming = ephi->getIncomingValue(idx);

      ephi->addIncoming( incoming, where );
    }

    return_value = new_invoke;

    // Add to basic block.
    InstInsertPt::End( where ) << new_invoke;
  }

  // Update the PHI with this return value.
  if( return_value_phi )
    return_value_phi->addIncoming( return_value, where );

  return target;
}

bool ExternICallPromote::runOnModule(Module& M)
{
  LLVM_DEBUG(errs() << "Starting ExternICallPromote\n");
  valid = false;

  inFile.open(PROF_FILE);

  if( !inFile.is_open() ){
    errs() << "File" <<  PROF_FILE << " does not exist\n";
    return false;
  }

  // Read in the first line, should be the total ICall and Maximum number of target
  string line;
  if (getline(inFile, line)){
    istringstream iss(line);
    iss >> NumICall >> MaxNumTarget;
  }
  else {
    errs() << "File is empty\n";
    return false;
  }

  vector<Instruction *> icalls;

  // extend vector v with vector vn
  auto extend_vector = [](auto &v, auto &vn) -> auto & {
    v.reserve(v.size() + distance(vn.begin(), vn.end()));
    v.insert(v.end(), vn.begin(), vn.end());
    return v;
  };

  for (Module::iterator IF = M.begin(), E = M.end(); IF != E; ++IF) {
    Function &F = *IF;
    if(F.isDeclaration())
      continue;

    auto new_icalls = findIndirectCalls(F);
    icalls = extend_vector(icalls, new_icalls);
  }

  if (icalls.size() != NumICall) {
    errs() << "Number of indirect calls doesn't match!\n";
    return false;
  }


  int current_id = 0;
  int file_id;
  int total_cnt;
  bool continue_read = false;
  for (auto &icall : icalls){

    // skip reading the next line if the previous one carries a line over
    if (!continue_read && !getline(inFile, line)){
      errs() << "Profile doesn't match\n";
      return false;
    }
    continue_read = false;

    // parse the id and cnt
    istringstream iss(line);
    iss >> file_id >> total_cnt;

    // the profile id does not match the curren tid
    if (file_id != current_id){
      errs() << "ID doesn't match\n";
      return false;
    }
    ++current_id;

    // don't have profile results
    if (total_cnt == 0)
      continue;

    vector<string> symbols;
    symbols.reserve(MaxNumTarget);

    size_t fn_ptr;
    unsigned cnt;
    string library, symbol;
    // consume all profiled records
    while (getline(inFile, line)){
      // start with "  ", meaning a record
      if (line.find("  ") == 0) {
        istringstream iss(line);
        iss >> hex >> fn_ptr;
        iss >> cnt >> library >> symbol;
        // TODO: more sophisticated check that it is an external symbol
        if (library.find(".so") != string::npos){
          symbols.push_back(symbol);
        }
      }
      else {
        continue_read = true;
        break;
      }
    }

    // found all possible symbols in order
    if (symbols.size() > 0){
      if (promoteExternalCallsite(icall, symbols, M))
        numPromoted++;
    }
  }

  valid = true;
  return true;
}

}
#undef DEBUG_TYPE
