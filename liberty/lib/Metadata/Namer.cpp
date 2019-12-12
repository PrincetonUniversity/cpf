/***
 * Namer.cpp
 *
 * Generate ID for each instruction
 *
 * */


#define DEBUG_TYPE "mtcg"

// LLVM header files
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Constants.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/GlobalVariable.h"

#include "Metadata.h"
#include <list>

namespace liberty {
	char Namer::ID = 0;
	namespace {
          static RegisterPass<Namer> RP("metadata-namer", "Generate unique IDs in Metadata for each instruction",
                          false, false);
	}

	Namer::Namer() : ModulePass(ID) {}

	Namer::~Namer() {
		reset();
	}

	void Namer::reset() {
		pM = NULL;
		funcId = 0;
		blkId = 0;
		instrId = 0;
	}

	void Namer::getAnalysisUsage(AnalysisUsage &AU) const {
		AU.addRequired<LoopInfoWrapperPass>();
		AU.setPreservesAll();
	}

	bool Namer::runOnModule(Module &M) {
		reset();
		pM = &M;
		LLVM_DEBUG(errs() << "\n\n\nEntering Metadata-Namer.\n");

		typedef Module::FunctionListType FunList;
		typedef FunList::iterator FunListIt;

		FunList &funcs = M.getFunctionList();
    bool modified = false;

		for(FunListIt func = funcs.begin(); func != funcs.end(); func++){
			Function *f = (Function*) &*func;
			modified |= runOnFunction(*f);
			funcId++;
		}

		return modified;
	}

  const int PACKING_FACTOR = 16;

	bool Namer::runOnFunction(Function &F) {
		LLVM_DEBUG(errs() << "function:" << F.getName() << "\n");
		LLVMContext &context = F.getContext();

    bool modified = false;

		for (Function::iterator bb = F.begin(), bbe = F.end(); bb != bbe; ++bb) {

      if (bb->getName().empty()) {
        bb->setName("bbName");
        modified = true;
      }

			for (BasicBlock::iterator I = bb->begin(), E = bb->end(); I != E; ++I) {
				Instruction *inst = &*I;
				Value* instrV=ConstantInt::get(Type::getInt32Ty(context),instrId);

// Inefficient to store funcV at every instruction, since
// it's redundant with block ID.  Instead, pack function
// and block id into one integer.
// -NPJ
//        Value* funcV = ConstantInt::get(Type::getInt32Ty(context),funcId);
//        Value* blkV = ConstantInt::get(Type::getInt32Ty(context),blkId);
//				Value* valuesArray[] = {funcV, blkV, instrV};
//        ArrayRef<Value *> values(valuesArray, 3);
        int f_b = (funcId << PACKING_FACTOR) + blkId;
        Value *vFB = ConstantInt::get(Type::getInt32Ty(context), f_b);

        //sot. Metadata changed. Not more part of the Value hierarchy
        // convert values to metadata with ValueAsMetadata::get
				//Value* valuesArray[] = {vFB, instrV};
				Metadata* valuesArray[] = {ValueAsMetadata::get(vFB), ValueAsMetadata::get(instrV)};
        ArrayRef<Metadata *> values(valuesArray, 2);
				MDNode* mdNode = MDNode::get(context, values);

//  The liberty.namer metadata is just a list of every metadata we insert.
//  It is wasteful, and nobody uses it.
//  It creates *real* scalability problems down road. -NPJ
//				NamedMDNode *namedMDNode = pM->getOrInsertNamedMetadata("liberty.namer");
//				namedMDNode->addOperand(mdNode);

				char name[]="namer";
				inst->setMetadata((const char*)name, mdNode);
				instrId++;
			}
			blkId++;
		}
		LLVM_DEBUG(errs() << "function:" << F.getName() << " func: " << funcId << " blkId: " << blkId << " instrId: " << instrId << "\n");

		return modified;
	}

	Value* Namer::getFuncIdValue(Instruction *I) {
		char name[]="namer";
		MDNode* md = I->getMetadata((const char*)name);
		if(md==NULL) return NULL;
    // Unpack function from (f,b)

    //sot
    ValueAsMetadata *vsm = dyn_cast<ValueAsMetadata> (md->getOperand(0));
		ConstantInt *vFB = cast< ConstantInt >(  vsm->getValue()  );

		//ConstantInt *vFB = cast< ConstantInt >(  md->getOperand(0)  );
    const int f_v = vFB->getSExtValue();
    const int f = f_v >> PACKING_FACTOR;
    return ConstantInt::get(vFB->getType(), f);
	}

	Value* Namer::getBlkIdValue(Instruction *I) {
		char name[]="namer";
		MDNode* md = I->getMetadata((const char*)name);
		if(md==NULL) return NULL;
    // Unpack block from (f,b)

    //sot
    ValueAsMetadata *vsm = dyn_cast<ValueAsMetadata> (md->getOperand(0));
		ConstantInt *vFB = cast< ConstantInt >(  vsm->getValue()  );

		//ConstantInt *vFB = cast< ConstantInt >(  md->getOperand(0)  );
    const int f_v = vFB->getSExtValue();
    const int b = f_v & ((1<<PACKING_FACTOR)-1);
    return ConstantInt::get(vFB->getType(), b);
	}

	Value* Namer::getInstrIdValue(Instruction *I) {
		char name[]="namer";
    if (I==NULL) return NULL;
		MDNode* md = I->getMetadata((const char*)name);
		if(md==NULL) return NULL;

    //sot
    ValueAsMetadata *vsm = dyn_cast<ValueAsMetadata> (md->getOperand(1));
		//return md->getOperand(1);
		return vsm->getValue() ;
	}

  Value* Namer::getInstrIdValue(const Instruction *I) {
		char name[]="namer";
		MDNode* md = I->getMetadata((const char*)name);
		if(md==NULL) return NULL;

    //sot
    ValueAsMetadata *vsm = dyn_cast<ValueAsMetadata> (md->getOperand(1));
		//return md->getOperand(1);
		return vsm->getValue() ;
	}

	int Namer::getFuncId(Instruction *I) {
		Value* v = getFuncIdValue(I);
		if(v==NULL) return -1;
		ConstantInt* cv = (ConstantInt*) v;
		return (int) cv->getSExtValue();
	}

	int Namer::getBlkId(Instruction *I) {
		Value* v = getBlkIdValue(I);
		if(v==NULL) return -1;
		ConstantInt* cv = (ConstantInt*) v;
		return (int) cv->getSExtValue();
	}

	int Namer::getInstrId(Instruction *I) {
		Value* v = getInstrIdValue(I);
		if(v==NULL) return -1;
		ConstantInt* cv = (ConstantInt*) v;
		return (int) cv->getSExtValue();
	}

  int Namer::getInstrId(const Instruction *I) {
		Value* v = getInstrIdValue(I);
		if(v==NULL) return -1;
		ConstantInt* cv = (ConstantInt*) v;
		return (int) cv->getSExtValue();
	}
}
