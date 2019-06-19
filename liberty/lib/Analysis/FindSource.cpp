
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/Pass.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/CallSite.h"
#include "llvm/Support/raw_ostream.h"

#include "liberty/Utilities/CallSiteFactory.h"
#include "liberty/Utilities/GetMemOper.h"

#include "liberty/Analysis/FindSource.h"

using namespace llvm;

namespace liberty {

  const Value *findSource(const BinaryOperator *binop) {
    const Value *op0 = binop->getOperand(0);
    const Value *op1 = binop->getOperand(1);
    if(isa<ConstantInt>(op0)) {
      return findSource(op1);
    } else if(isa<ConstantInt>(op1)) {
      return findSource(op0);
    } else {
      return binop;
    }
  }

  const Value *findSource(const Instruction *i) {
    if(const GetElementPtrInst *gep = dyn_cast<GetElementPtrInst>(i)) {
      return findSource(gep->getPointerOperand());

    } else if(const CastInst *cast = dyn_cast<CastInst>(i)) {
      return findSource(cast->getOperand(0));

    } else if(const BinaryOperator *binop = dyn_cast<BinaryOperator>(i)) {
      return findSource(binop);

    } else if(const CallInst *call = dyn_cast<CallInst>(i)) {
      return findSource(call);

    } else {
      return i;
    }
  }

  const Value *findSource(const CallInst *call) {

    const Function *func = call->getCalledFunction();

    if(!func) return call;

    if(func->getName() != "lcuLookupGlobal") return call;

    return findSource(call->getArgOperand(0));
  }

  const Value *findSource(const ConstantExpr *expr) {
    const unsigned opcode = expr->getOpcode();
    if(opcode == Instruction::GetElementPtr) {
      return findSource(expr->getOperand(0));
    } else if(opcode == Instruction::BitCast) {
      return findSource(expr->getOperand(0));
    } else {
      return expr;
    }
  }

  const Value *findSource(const Value *v) {
    if(const Instruction *i = dyn_cast<Instruction>(v)) {
      return findSource(i);

    } else if(const ConstantExpr *expr = dyn_cast<ConstantExpr>(v)) {
      return findSource(expr);

    } else {
      return v;
    }
  }

  const Instruction *findNoAliasSource(const StoreInst *store,
                                       const TargetLibraryInfo &tli) {
    return findNoAliasSource(store->getValueOperand(), tli);
  }

  const Instruction *findNoAliasSource(const Value *v,
                                       const TargetLibraryInfo &tli) {

    const Instruction *source = dyn_cast<Instruction>(findSource(v));
    if(!source)
      return NULL;

    if(isa<AllocaInst>(source))
      return source;

    CallSite CS = getCallSite(const_cast<Instruction *>(source));
    if(!CS.getInstruction())
      return NULL;

    const Function *f = CS.getCalledFunction();
    if(!f)
      return NULL;

    if (!f->getAttributes().hasAttribute(0, Attribute::NoAlias) &&
        !isNoAliasFn(v, &tli))
      return NULL;

    return source;
  }

  const AllocaInst *findAllocaSource(const Value *v) {
    return dyn_cast<AllocaInst>(findSource(v));
  }

  // This method returns the global memory the value the pointer was loaded
  // from.  This isn't 100% const correct, but the ends justify the
  // means. Arguably, the LoadInst and StoreInst instructions should accept
  // const GlobalValue *, and GlobalValue should declare some fields mutable.
  GlobalValue *findGlobalSource(const Value *v) {

    const Value *src = findSource(v);
    if(!src)
      return NULL;

    const Instruction *inst = dyn_cast<Instruction>(src);
    if(!inst)
      return NULL;

    const Value *ptr = getMemOper(inst);
    if(!ptr)
      return NULL;

    const GlobalValue *global = dyn_cast<GlobalValue>(ptr);
    return const_cast<GlobalValue *>(global);
  }

  const Value *findActualArgumentSource(const Value *v) {

    const Argument *a = findArgumentSource(v);
    if(!a) {
      return NULL;
    }

    const Function *parent = a->getParent();
    if(parent->hasAddressTaken()) {
      return NULL;
    }

    if(parent->getNumUses() != 1) {
      return NULL;
    }

    const Instruction *inst = dyn_cast<const Instruction>(*parent->user_begin());
    if(!inst) {
      return NULL;
    }

    const CallSite call(const_cast<Instruction *>(inst));
    assert(call.getInstruction());

    return findSource(call.getArgument(a->getArgNo()));
  }

  const Argument *findArgumentSource(const Value *v) {
    if(const Argument *arg = dyn_cast<Argument>(v)) {
      return arg;
    }

    if(isa<Instruction>(v)) {
      return dyn_cast<Argument>(findSource(v));
    }

    return NULL;
  }

  const Argument *findLoadedNoCaptureArgument(const Value *v, const DataLayout &DL) {

    const Value *o = GetUnderlyingObject(v, DL);
    const LoadInst *load = dyn_cast<LoadInst>(o);
    if(!load)
      return NULL;

    const Value *pointer = load->getPointerOperand();
    const Value *src = GetUnderlyingObject(pointer, DL);
    const Argument *arg = dyn_cast<Argument>(src);
    if(!arg)
      return NULL;

    if(!arg->hasNoCaptureAttr())
      return NULL;

    return arg;
  }

  const Value *findOffsetSource(const Value *v) {

    const Value *source = findSource(v);
    const BinaryOperator *binop = dyn_cast<BinaryOperator>(source);
    if(!binop)
      return source;

    const unsigned opcode = binop->getOpcode();
    if(opcode != Instruction::Add)
      return source;

    const Value *left = binop->getOperand(0);
    const Value *right = binop->getOperand(1);

    const Value *leftSource = findOffsetSource(left);
    const Value *rightSource = findOffsetSource(right);

    const bool leftIsPHI = isa<PHINode>(leftSource);
    const bool rightIsPHI = isa<PHINode>(rightSource);

    if(leftIsPHI && rightIsPHI)
      return source;

    if(leftIsPHI)
      return leftSource;

    if(rightIsPHI)
      return rightSource;

    return source;
  }

  Type *findDestinationType(const Value *v) {


    DenseSet<Type *> destType;

    typedef Value::const_user_iterator UseIt;
    for(UseIt use = v->user_begin(); use != v->user_end(); ++use) {
      if(const CastInst *cast = dyn_cast<CastInst>(*use)) {
        destType.insert(findDestinationType(cast));
      }

      if(const LoadInst *load = dyn_cast<LoadInst>(*use)) {
        destType.insert(load->getPointerOperand()->getType());
      }

      if(const GetElementPtrInst *gep = dyn_cast<GetElementPtrInst>(*use)) {
        if(gep->getPointerOperand() == v) {
          if(gep->getNumIndices() != 1)
            destType.insert(gep->getPointerOperand()->getType());
          else
            destType.insert(findDestinationType(gep));
        }
      }
    }

    if(destType.size() == 1) {
      return *destType.begin();
    }

    return v->getType();
  }

  const Value *findDynSource(const Value *v) {

    const Value *source = findSource(v);

    const LoadInst *load = dyn_cast<LoadInst>(source);
    if(!load) return source;

    const Value *pointer = load->getPointerOperand();
    if(!isa<GlobalVariable>(pointer)) return source;

    if(!pointer->getName().startswith("dyn.")) return source;

    return pointer;
  }
}
