#pragma once

#include "llvm/Pass.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Value.h"

#include "liberty/Talkdown/Node.h"
#include "liberty/Talkdown/Tree.h"

#include <algorithm>
#include <map>
#include <string>
#include <unordered_set>

using namespace llvm;
using namespace AutoMP;

namespace llvm
{
  // XXX Temporary to test in TalkdownAA! This should be a static function
	/* struct pair_hash */
	/* { */
	/* 	template <class T1, class T2> */
	/* 	std::size_t operator () (std::pair<T1, T2> const &pair) const */
	/* 	{ */
	/* 		std::size_t h1 = std::hash<T1>()(pair.first); */
	/* 		std::size_t h2 = std::hash<T2>()(pair.second); */

	/* 		return h1 ^ h2; */
	/* 	} */
	/* }; */
  /* std::unordered_set<std::pair<std::string, std::string>, pair_hash<std::string, std::string> > getMetadataAsStrings(Value *i); */

  struct Talkdown : public ModulePass
  {
    public:
      static char ID;

      Talkdown() : ModulePass( ID ) {}

      bool doInitialization(Module &M);
      bool runOnModule(Module &M);
      void getAnalysisUsage(AnalysisUsage &AU) const;

      /* SESENode *getInnermostRegion(Instruction *); */
      /* SESENode *getParent(SESENode *); */
      /* SESENode *getInnermostCommonAncestor(SESENode *, SESENode *); */
      /* std::map<std::string, std::string> &getMetadata(SESENode *); // common metadata */
      bool areIndependent(Instruction *i1, Instruction *i2);

    private:
      bool enabled;

      std::vector<FunctionTree> function_trees;
  };
} // namespace llvm
