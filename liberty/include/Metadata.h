#ifndef LLVM_LIBERTY_METADATA_MANAGER
#define LLVM_LIBERTY_METADATA_MANAGER

#include "llvm/Pass.h"

namespace liberty {
	using namespace llvm;

	class Namer: public ModulePass{
		private:
			Module *pM;
			int funcId;
			int blkId;
			int instrId;

		public:
			static char ID;
			Namer();
			~Namer();

			void reset();

			StringRef getPassName() const { return "MetadataManager"; }

			void *getAdjustedAnalysisPointer(AnalysisID PI) {
				return this;
			}

			void getAnalysisUsage(AnalysisUsage &AU) const;

			bool runOnModule(Module &M);
			bool runOnFunction(Function &F);
			static Value* getFuncIdValue(Instruction *I);
			static Value* getBlkIdValue(Instruction *I);
			static Value* getInstrIdValue(Instruction *I);
			static Value* getInstrIdValue(const Instruction *I);
			static int getFuncId(Instruction *I);
			static int getBlkId(Instruction *I);
			static int getInstrId(Instruction *I);
			static int getInstrId(const Instruction *I);
	};

}
#endif
