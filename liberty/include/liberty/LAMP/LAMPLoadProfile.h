#ifndef LLVM_LIBERTY_LAMP_LOAD_PROFILE_H
#define LLVM_LIBERTY_LAMP_LOAD_PROFILE_H

//===----------------------------------------------------------------------===//
//
// Info...
//
//===----------------------------------------------------------------------===//

#include "llvm/Analysis/LoopPass.h"
#include <set>


namespace llvm {

  struct biikey_t
  {
    biikey_t(const BasicBlock *bb,
        int ii1,
        int ii2,
        int cross)
      : b(bb), i1(ii1), i2(ii2), cross_iter(cross)  {}
    const BasicBlock *b;
    int i1;
    int i2;
    int cross_iter;
  };

  template <> struct isPodLike< biikey_t > { static const bool value = true; };

  template <>
    struct DenseMapInfo< biikey_t >
    {
      static inline biikey_t getEmptyKey()
      {
        return biikey_t(0,0,0,0);
      }

      static inline biikey_t getTombstoneKey()
      {
        return biikey_t(0,0,0,42);
      }

      static unsigned X(unsigned a, unsigned b)
      {
        std::pair< unsigned, unsigned > pair(a,b);
        return DenseMapInfo< std::pair< unsigned, unsigned > >::getHashValue(pair);
      }

      static unsigned getHashValue(const biikey_t &value)
      {
        return
          X(value.i1,
          X(value.i2,
          X(DenseMapInfo<const BasicBlock *>::getHashValue(value.b),
            value.cross_iter)));
        ;
      }

      static bool isEqual(const biikey_t &a, const biikey_t &b)
      {
        return a.b == b.b
          &&   a.i1 == b.i1
          &&   a.i2 == b.i2
          &&   a.cross_iter == b.cross_iter;
      }
    };

  class ModulePass;
  class FunctionPass;
  class LoopPass;

  LoopPass *createLAMPBuildLoopMapPass();
  ModulePass *createLAMPLoadProfilePass();

  class LAMPBuildLoopMap : public LoopPass {
    static unsigned int loop_id;
    static bool IdInitFlag;

    public:
    static char ID;
    LAMPBuildLoopMap() : LoopPass(ID) {}

    virtual bool runOnLoop (Loop *L, LPPassManager &LPM);
    virtual void getAnalysisUsage(AnalysisUsage &AU) const;
  };

  class LAMPLoadProfile : public ModulePass {
    public:
      /* Methods */
      unsigned int numObsIntraIterDep(BasicBlock *, const Instruction *, const Instruction *);
      unsigned int numObsInterIterDep(BasicBlock *, const Instruction *, const Instruction *);
      unsigned int numObsIterDep(BasicBlock *, Instruction *, Instruction *);
      bool isValid();
      double probDep(BasicBlock *, Instruction *, Instruction *, int);

      /* Variables */
      typedef std::pair<int, int> InstPair;
      typedef std::set< InstPair > InstPairSet;
      typedef DenseMap< biikey_t, double > BIIMap;
      typedef DenseMap< biikey_t, int> BII_Count_Map;
      BIIMap biimap; /* map for relating bb, i1, i2 to probability */
      BII_Count_Map DepToCountMap;
      std::map<unsigned int, Instruction*> IdToInstMap;
      std::map<Instruction*, unsigned int> InstToIdMap;

      static unsigned int lamp_id;
      static char ID;
      LAMPLoadProfile() : ModulePass(ID), biimap() {}

      virtual bool runOnModule (Module &M);
      virtual void getAnalysisUsage(AnalysisUsage &AU) const;
  };
}

#endif // LLVM_LIBERTY_LAMP_LOAD_PROFILE_H
