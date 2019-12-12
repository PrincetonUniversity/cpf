#define DEBUG_TYPE "lamp-load"

#include "llvm/IR/Value.h"

// line #
#include "llvm/IR/IntrinsicInst.h"
//#include "llvm/Assembly/Writer.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/Analysis/ValueTracking.h"



#include "llvm/IR/Module.h"
#include "llvm/Pass.h"
#include "llvm/Analysis/Passes.h"
#include "llvm/Analysis/LoopPass.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/Support/Debug.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/ADT/IndexedMap.h"
#include "llvm/IR/Module.h"
//#include "llvm/Support/Annotation.h"
#include <map>
#include <set>
#include <sstream>
#include <fstream>
#include <iostream>
#include <string>
#include <utility>
#include <sys/stat.h>

#include "liberty/LAMP/LAMPLoadProfile.h"
#include "liberty/LAMP/LAMPFlags.h"

#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/Debug.h"
#include "Metadata.h"


#define LCOUTFILE "lcout.out"
#define DOUTFILE "dout.out"
#define AUXFILE "auxout.out"



using namespace llvm;
using namespace liberty;

static cl::opt<std::string> ProfileFileName(
    "lamp-profile-file",
    cl::init("result.lamp.profile"),
    cl::NotHidden,
    cl::desc("Load LAMP profile from this file"));
static cl::opt<bool> AssertIfLoadFails(
    "lamp-assert",
    cl::init(false),
    cl::Hidden,
    cl::desc("Assert if fail to load LAMP profile"));

unsigned globLoop;

int needCheck = 0;

typedef struct varNames{
  std::string name;
  std::string fnName;
  std::string bbName;
  unsigned loop;
  unsigned line;
  struct varNames * next;
} varNames;
varNames * first;
varNames * second;
std::ofstream auxfile;
typedef std::set<const Instruction *> ISet;
void recurseOperands(Instruction * myI, std::string fnName, std::string bbName, int list, unsigned line, ISet &avoidInfiniteRecursion);
void addToList(std::string name, std::string fnName, std::string bbName, int list, unsigned line);
void dumpListToFile(int lc);
void purgeList();
//const DebugLoc & findStopPoint2(const Instruction * Inst);

static std::map<unsigned int, BasicBlock*> IdToLoopMap_global;
static std::map<BasicBlock*, unsigned int> LoopToIdMap_global;

inline unsigned int str_to_int(std::string& s)
{
  std::istringstream iss(s);
  unsigned int t;
  iss >> t;
  return t;
}

namespace {
  class LdStCallCounter : public ModulePass {
    public:
      static char ID;
      static bool flag;
      bool runOnModule(Module &M);
      static unsigned int num_loads;
      static unsigned int num_stores;
      static unsigned int num_calls;
      static unsigned int num_intrinsics;
      //    static unsigned int num_loops;
      LdStCallCounter(): ModulePass(ID)
    {

    }
      unsigned int getCountInsts()
      {
        return num_loads + num_stores + num_calls + num_intrinsics;
      }
  };
}

char LdStCallCounter::ID = 0;

// flag to ensure we only count once
bool LdStCallCounter::flag = false;

// only want these counted once and only the first time (not after other instrumentation)
unsigned int LdStCallCounter::num_loads = 0;
unsigned int LdStCallCounter::num_stores = 0;
unsigned int LdStCallCounter::num_calls = 0;
unsigned int LdStCallCounter::num_intrinsics = 0;
// store loops here also because loop passes cannot be required by other passes
// unsigned int LdStCallCounter::num_loops = 0;

namespace { static RegisterPass<LdStCallCounter> RP1(
    "lamp-inst-cnt",
    "Count the number of LAMP Profilable insts", false, false); }

  bool LdStCallCounter::runOnModule(Module &M) {
    if (flag == true)  // if we have counted already
      return false;
    // for all functions in module
    for (Module::iterator I = M.begin(), E = M.end(); I != E; ++I)
      if (!I->isDeclaration())
      {      // for all blocks in the function
        for (Function::iterator BBB = I->begin(), BBE = I->end(); BBB != BBE; ++BBB)
        {    // for all instructions in a block
          for (BasicBlock::iterator IB = BBB->begin(), IE = BBB->end(); IB != IE; IB++)
          {
            if (isa<LoadInst>(IB))    // count loads, stores, calls
            {
              num_loads++;
            }
            else if (isa<StoreInst>(IB))
            {
              num_stores++;    // count only external calls, ignore declarations, etc
            }
            else if( isa<MemIntrinsic>(IB) )
            {
              num_intrinsics++;
            }
//            else if (isa<CallInst>(IB) && ( (dyn_cast<CallInst>(IB)->getCalledFunction() == NULL) ||
//                  (dyn_cast<CallInst>(IB)->getCalledFunction()->isDeclaration())))
            else if( EX_CALL(IB))
            {
              num_calls++;
            }
          }
        }
      }
    LLVM_DEBUG(errs() << "Loads/Store/Intrinsics/Calls:" << num_loads << " " << num_stores
        << " " << num_intrinsics << " " << num_calls << '\n');
    flag = true;

    return false;
  }
/*
   namespace llvm {
   class LAMPAnnotation : public Annotation{
   std::map<int,Instruction> IdToInstMap;

   Function* F;
   public:
   LAMPAnnotation(AnnotationID id, Function *f) Annotation(id){ F=f;}
   void buildIDInstructionMap();
   };
   }
 */
/*
   namespace llvm {
   class LAMPBuildInstMap : public ModulePass{
   public:
   std::map<unsigned int, Instruction*> IdToInstMap;
   static char ID;
   static unsigned int instruction_id;
   LAMPBuildInstMap() : ModulePass (ID) {}
   bool runOnModule (Module &M);
   };
   }
   char LAMPBuildInstMap::ID = 0;
   unsigned int LAMPBuildInstMap::instruction_id = -1;
   static INITIALIZE_PASS(LAMPBuildInstMap>
   X("lamp-map-inst","Build the map of LAMP Id and Load/Store/call");
 */
/*
   namespace llvm {
   class LAMPBuildInstMap : public FunctionPass{
   public:
   std::map<unsigned int, Instruction*> IdToInstMap;
   static char ID;
   static unsigned int instruction_id;
   LAMPBuildInstMap() : FunctionPass (ID) {}
   bool runOnFunction (Function &F);
   };
   }
   char LAMPBuildInstMap::ID = 0;
   unsigned int LAMPBuildInstMap::instruction_id = -1;
   static INITIALIZE_PASS(LAMPBuildInstMap>
   X("lamp-map-inst","Build the map of LAMP Id and Load/Store/call");
 */

void LAMPBuildLoopMap::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.setPreservesAll();
  AU.addRequired<LdStCallCounter>();
}

char LAMPBuildLoopMap::ID = 0;
unsigned int LAMPBuildLoopMap::loop_id = 0;
bool LAMPBuildLoopMap::IdInitFlag = false;
namespace { static RegisterPass<LAMPBuildLoopMap> RP2("lamp-map-loop","Build the map of LAMP Id and Loop", false, false); }
LoopPass *llvm::createLAMPBuildLoopMapPass() { return new LAMPBuildLoopMap(); }

bool LAMPBuildLoopMap::runOnLoop(Loop* L, LPPassManager &LPM)
{
  // build the <IDs, Loop> map
  if (IdInitFlag == false){
    //loop_id = Counter.getCountInsts()-1; //instruction is assigned from 0
    IdInitFlag = true;
  }
  BasicBlock* BB = L->getHeader();
  LLVM_DEBUG(errs() << "Generating loop info\n");
  IdToLoopMap_global[++loop_id] = BB;
  LoopToIdMap_global[BB] = loop_id;
  LLVM_DEBUG(errs() << loop_id << " " << L << " " << BB << '\n');

  return true;
}


void LAMPLoadProfile::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.setPreservesAll();
  // AU.addRequired<LoopInfo>();
  // AU.addRequired<LAMPBuildLoopMap>();
  // AU.addRequired<LAMPBuildMap>();
}

char LAMPLoadProfile::ID = 0;
unsigned int LAMPLoadProfile::lamp_id = -1;
namespace { static RegisterPass<LAMPLoadProfile> RP3(
    "lamp-load-profile",
    "(LAMPLoad) Load back profile data and generate dependency information", false, false); }

bool LAMPLoadProfile::runOnModule(Module& M)
{
  std::map<BasicBlock*, InstPairSet > LoopToDepSetMap;
  //int id;

  // build the <IDs, Instrucion> map
  for (Module::iterator FB = M.begin(), FE = M.end(); FB != FE; FB++){
    // for all blocks in the function
    if (!FB->isDeclaration()) {
      for (Function::iterator BBB = FB->begin(), BBE = FB->end(); BBB != BBE; ++BBB)
      {    // for all instructions in a block
        for (BasicBlock::iterator IB = BBB->begin(), IE = BBB->end(); IB != IE; IB++)
        {
          //id = Namer::getInstrId(IB);
          if (isa<LoadInst>(IB) || isa<StoreInst>(IB))
          { // count loads, stores, calls

            IdToInstMap[++lamp_id]= &*IB;
            LLVM_DEBUG(errs() << "Adding instruction -" << *IB << "- with ID:" << lamp_id << "\n");
            InstToIdMap[&*IB]=lamp_id;
          }
          else if( isa<MemIntrinsic>(IB) )
          {
            IdToInstMap[++lamp_id]= &*IB;
            InstToIdMap[&*IB]=lamp_id;
          }
//          else if (isa<CallInst>(IB) && ( (dyn_cast<CallInst>(IB)->getCalledFunction() == NULL) ||
//                (dyn_cast<CallInst>(IB)->getCalledFunction()->isDeclaration())))
          else if( EX_CALL(IB))
          {
            IdToInstMap[++lamp_id]= &*IB;
            InstToIdMap[&*IB]=lamp_id;
          }
        }
      }
    }
  }
  /*
     for (unsigned int i = 0; i< lamp_id+1 ; i++){
     LLVM_DEBUG(errs() << i << * (IdToInstMap[i]) );
     }*/
  /*
     for (Module::iterator FB = M.begin(), FE = M.end(); FB != FE; FB++){
     if (!FB->isDeclaration()){
     LoopInfo &LInfo =  getAnalysis<LoopInfo> (*FB);
     for (LoopInfo::iterator LB = LInfo.begin(), LE = LInfo.end(); LB != LE; LB++){
     IdToLoopMap[++lamp_id] = *LB;
     Loop *L = *LB;
     LLVM_DEBUG(errs() << lamp_id << *(L->getLoopPreheader()));
     }
     }
     }*/   // ID, Loop mismatch ... missing some Loops

  /*
     std::map<unsigned int, BasicBlock*>::iterator iter2;

     int i =0;
     for (iter2=IdToLoopMap_global.begin(); iter2!=IdToLoopMap_global.end();iter2++){
     LLVM_DEBUG(errs() << (*iter2).first << " "  << (*iter2).second  << '\n');
     i++;
     }
     LLVM_DEBUG(errs() << i << '\n');
   */
  std::ifstream ifs;
  struct stat sInfo;

  if(stat(ProfileFileName.c_str(), &sInfo ) !=0){    // need to add file name
    //      std::cerr << "Could not find file result.lamp.profile\n";
    if( AssertIfLoadFails )
      assert(false && "Failed to load LAMP profile");
    return false;
  }

  ifs.open(ProfileFileName.c_str());
  LLVM_DEBUG(errs() << "Opened " << ProfileFileName << "\n");

  std::string s;


  // Get rid of the 'worthless' things in the results file
  const unsigned MAX_LOOPS = 5000;
  unsigned int itercounts[MAX_LOOPS] = {0};
  for(unsigned i=0; i<MAX_LOOPS; i++)
  {
    ifs >> s;
    // To handle dynamic numbers of loops, break out early when we hit the end
    // of the loop section, clears out the 'BEGIN Memory Profile' line
    if(s == "BEGIN")
    {
      ifs >> s;
      ifs >> s;
      break;
    }
    ifs >> s;
    itercounts[i] = str_to_int(s);
  }

  std::ofstream lcfile(LCOUTFILE);
  std::ofstream dfile(DOUTFILE);
  auxfile.open(AUXFILE);

  lcfile << "#File produced by LAMP Load Profiling pass\n";
  lcfile << "#This file contains information on loop-carried dependences\n";
  lcfile << "#[var1 @ function * bbname -- var2 @ function * bbname] line1#line2# LAMPdata Loop: LoopID LAMPdata\n";
  lcfile << "#var1 depends on var2\n";
  dfile << "#File produced by LAMP Load Profiling pass\n";
  dfile << "#This file contains information on intra-iteration dependences\n";
  dfile << "#[var1 @ function * bbname -- var2 @ function * bbname] line1#line2# LAMPdata Loop: LoopID LAMPdata\n";
  dfile << "#var1 depends on var2\n";
  auxfile << "#File produced by LAMP Load Profiling pass\n";
  auxfile << "#This file contains information on loop-caried and intra-iteration dependences where recursion worked to find variable names in the LAMP Reader\n";
  auxfile << "#[var1 @ function * bbname -- var2 @ function * bbname] line1#line2# Loop: LoopID\n";
  auxfile << "#var1 depends on var2\n";

  int i = 0, j = 0;
  unsigned int lctotal = 0;
  unsigned int dtotal = 0;
  unsigned int num_cnt = 0;
  unsigned int num, i1_id=0, i2_id=0, cross_iter=0, loop_id=0, times=0;

  BasicBlock* BB;
  std::map<BasicBlock*, InstPairSet >::iterator iter;
  while (ifs >> s)
  {
    size_t found_leftP;   // (
    size_t found_rightP;  // )

    // String Operation
    // (1) discard string with ")"
    // (2) erase "(" in the string

    found_rightP = s.find_first_of(")");
    if (found_rightP !=  std::string::npos) continue;

    found_leftP = s.find_first_of("(");
    if (found_leftP !=  std::string::npos){
      assert(found_leftP == 0);        // "(" always 1st character
      s.erase(0,1);
    }
    if (s.find("END") != std::string::npos) break;

    ++num_cnt;
    num = str_to_int(s);
    // LLVM_DEBUG(errs() << "Read: " << num);

    switch (num_cnt % 6) { //6  numbers in each line (result.lamp.profile)
      case 1:
        i1_id = num;
        break;
      case 2:
        cross_iter = num;
        break;
      case 3:
        loop_id = num;
        break;
      case 4:
        i2_id = num;
        break;
      case 5:
        times = num;
        if (cross_iter) {
          if ( IdToInstMap[i1_id] != NULL && IdToInstMap[i2_id] !=NULL  )
          {
            InstPair dep_inst_pair;
            dep_inst_pair.first  = Namer::getInstrId(IdToInstMap[i1_id]);
            dep_inst_pair.second = Namer::getInstrId(IdToInstMap[i2_id]);

            /*
               InstPair* dep_inst_pair_ptr2 = new InstPair;
               dep_inst_pair_ptr2->first  = IdToInstMap[i1_id];
               dep_inst_pair_ptr2->second = IdToInstMap[i2_id];
             */

            BB = IdToLoopMap_global[loop_id];

            // Adding this to see variable names-- TRM
            Instruction * temp1, * temp2;
            unsigned ln1, ln2;


            lcfile << "[";
            temp1 = IdToInstMap[i1_id]; temp2 = IdToInstMap[i2_id];

            if (isa<LoadInst>(temp1) || isa<StoreInst>(temp1))
            {
              Value * myV;
              if (isa<LoadInst>(temp1))
                myV = dyn_cast<LoadInst>(temp1)->getPointerOperand();
              else if (isa<StoreInst>(temp1))
                myV = dyn_cast<StoreInst>(temp1)->getPointerOperand();
              else
                myV = temp1->getOperand(0);  // try it anyway ???
              int i = 0;
              Instruction * myI = temp1;

              const DebugLoc & DSI = myI->getDebugLoc();


              if(DSI)
              {
                ln1 = DSI.getLine();
                // LLVM_DEBUG(errs() << "\n!!!! Line # is " << ln1 << " inst " << myV->getName().str() << "\n\n");
              }
              else
              {
                ln1 = 0;
                LLVM_DEBUG(errs() << "No DSI\n");
              }

              while (myV->getName().str() == "")
              {
                if (myI && (dyn_cast<User>(myI))->getNumOperands() != 0)
                {
                  if (isa<LoadInst>(myI))
                  {
                    myV = dyn_cast<LoadInst>(myI)->getPointerOperand();
                    myI = dyn_cast<Instruction>(dyn_cast<LoadInst>(myI)->getPointerOperand());
                  }
                  else if (isa<StoreInst>(myI))
                  {
                    myV = dyn_cast<StoreInst>(myI)->getPointerOperand();
                    myI = dyn_cast<Instruction>(dyn_cast<StoreInst>(myI)->getPointerOperand());
                  }
                  else
                  {
                    if ((dyn_cast<User>(myI))->getNumOperands() == 1)
                    {
                      myV = myI->getOperand(0);  // try it anyway ???
                      myI = dyn_cast<Instruction>(myI->getOperand(0));
                    }
                    else
                    {  // should recurse in multiple directions here
                      // myV = myI->getOperand(0);  // try it anyway ???
                      //myI = dyn_cast<Instruction>(myI->getOperand(0));
                      LLVM_DEBUG(errs() << "attempting zrecurse" << i << "\t" << i1_id << "\n");
                      needCheck++;
                      globLoop = loop_id;
                      ISet avoidInfiniteRecursion;
                      recurseOperands(myI, temp1->getParent()->getParent()->getName().str(),
                          temp1->getParent()->getName().str(), 1, ln1, avoidInfiniteRecursion);
                      break;
                    }
                  }

                }
                else
                {
                  //                           lcfile << "##ORIG";
                  break;
                }
                i++;

              }

              lctotal++;
              if (myV->getName().str() == "")
                lcfile << "##ORIG";
              // Printed- [
              // Print- var1
              lcfile << myV->getName().str();
              addToList(myV->getName().str(),
                  temp1->getParent()->getParent()->getName().str(),
                  temp1->getParent()->getName().str(), 1, ln1);
            }

            // Printed - [var1
            // Print - @ fun * bb --
            lcfile << " @ " << temp1->getParent()->getParent()->getName().str()
              << " * " << temp1->getParent()->getName().str() << " -- ";
            if (isa<LoadInst>(temp2) || isa<StoreInst>(temp2))
            {
              Value * myV;
              if (isa<LoadInst>(temp2))
                myV = dyn_cast<LoadInst>(temp2)->getPointerOperand();
              else if (isa<StoreInst>(temp2))
                myV = dyn_cast<StoreInst>(temp2)->getPointerOperand();
              else
                myV = temp2->getOperand(0);  // try it anyway ???

              Instruction * myI = temp2;

              const DebugLoc & DSI = myI->getDebugLoc();


              if(DSI)
              {
                ln2 = DSI.getLine();
                //  LLVM_DEBUG(errs() << "\n!!!! Line # is " << ln1 << " inst " << myV->getName().str() << "\n\n");
              }
              else
              {
                ln2 = 0;
                LLVM_DEBUG(errs() << "No DSI\n");
              }

              while (myV->getName().str() == "")
              {
                if (myI && (dyn_cast<User>(myI))->getNumOperands() != 0)
                {
                  if (isa<LoadInst>(myI))
                  {
                    myV = dyn_cast<LoadInst>(myI)->getPointerOperand();
                    myI = dyn_cast<Instruction>(dyn_cast<LoadInst>(myI)->getPointerOperand());
                  }
                  else if (isa<StoreInst>(myI))
                  {
                    myV = dyn_cast<StoreInst>(myI)->getPointerOperand();
                    myI = dyn_cast<Instruction>(dyn_cast<StoreInst>(myI)->getPointerOperand());
                  }
                  else
                  {
                    if ((dyn_cast<User>(myI))->getNumOperands() == 1)
                    {
                      myV = myI->getOperand(0);  // try it anyway ???
                      myI = dyn_cast<Instruction>(myI->getOperand(0));
                    }
                    else
                    {  // should recurse in multiple directions here
                      //myV = myI->getOperand(0);  // try it anyway ???
                      //myI = dyn_cast<Instruction>(myI->getOperand(0));
                      LLVM_DEBUG(errs() << "attempting recurse" << i++ << "\t" << i2_id << "\n");
                      globLoop = loop_id;
                      ISet avoidInfiniteRecursion;
                      recurseOperands(myI, temp2->getParent()->getParent()->getName().str(),
                          temp2->getParent()->getName().str(), 2, ln2, avoidInfiniteRecursion);
                      needCheck++;
                      break;
                    }
                  }
                }
                else
                {
                  //                           lcfile << "##ORIG";
                  break;
                }

              }
              if (myV->getName().str() == "")
                lcfile << "##ORIG";
              // Printed- [var1 @ fun * bb --
              // Print  - var2
              lcfile << myV->getName().str();
              addToList(myV->getName().str(),
                  temp1->getParent()->getParent()->getName().str(),
                  temp1->getParent()->getName().str(), 2, ln2);
            }

            // Printed- [var1 @ fun * bb -- var2
            // Print  - @ fun * bb]
            lcfile << " @ " << temp2->getParent()->getParent()->getName().str()
              << " * " << temp2->getParent()->getName().str() << "]\t";

            if (ln1 == 0)
              lcfile << "UNKNOWN";
            else
              lcfile << ln1;
            lcfile << "#";

            if (ln2 == 0)
              lcfile << "UNKNOWN";
            else
              lcfile << ln2;

            lcfile << "#\t";

            //                  lcfile << ln1 << " # " << ln2 << "\t";

            // BasicBlock *nickBB = IdToLoopMap_global[loop_id];
            lcfile << temp1 << " " << temp2 << " ID:" << i1_id << " " << i2_id
              << " Loop:" << loop_id
//              << " (" << nickBB->getParent()->getNameStr() << ':'
//              << nickBB->getNameStr() << ')'
              << " " << "(" << BB << ")" ;
//            iter = LoopToDepSetMap.find(BB);
//            if (iter == LoopToDepSetMap.end() )
//            {   // newly found loop
//              LoopToDepSetMap[BB].insert( dep_inst_pair );
//            }else{                                 // loop already in the map
              LoopToDepSetMap[BB].insert(dep_inst_pair);
//            }

            /*
               if(*dep_inst_pair_ptr == *dep_inst_pair_ptr2) {
               LLVM_DEBUG(errs() << "~I think the pair pointers are equal\n");
               } else {
               LLVM_DEBUG(errs() << "~The pointers are not equal\n");
               LLVM_DEBUG(errs() << "Ptr 1: " << dep_inst_pair_ptr->first  << " " << dep_inst_pair_ptr->second << "\n");
               LLVM_DEBUG(errs() << "Ptr 2: " << dep_inst_pair_ptr2->first << " " << dep_inst_pair_ptr2->second << "\n");
               }

               InstPairSet::iterator siter;
               siter = LoopToDepSetMap[BB].find(*dep_inst_pair_ptr);
               if(siter == LoopToDepSetMap[BB].end() ) {
               LLVM_DEBUG(errs() << "~~~did NOT find instruction pair we just inserted\n");

               } else {
               LLVM_DEBUG(errs() << "~~~FOUND instruction pair we just inserted\n");


               }
               siter = LoopToDepSetMap[BB].find(*dep_inst_pair_ptr2);
               if(siter == LoopToDepSetMap[BB].end() ) {
               LLVM_DEBUG(errs() << "~~~did NOT find duplicate pair\n");

               } else {
               LLVM_DEBUG(errs() << "~~~FOUND duplicate pair\n");


               }
             */

            // Store the values in the maps with their Namer::id so we can survive changes to IR
            biikey_t biikey(BB, Namer::getInstrId(IdToInstMap[i1_id]),
                Namer::getInstrId(IdToInstMap[i2_id]), cross_iter > 0);
            DepToCountMap[biikey] = times;

            lcfile << " T:" << times << " P:" << (double)(times) / itercounts[loop_id]
              << "  " << LoopToDepSetMap.size();

            if (needCheck != 0)
              dumpListToFile(1);
            else
              purgeList();
          }
        }
        else {  // need true dependences analyzed here as well, not cross iteration
          if (loop_id != 0)
          {
            if ( IdToInstMap[i1_id] != NULL && IdToInstMap[i2_id] !=NULL  ){
              InstPair dep_inst_pair(Namer::getInstrId(IdToInstMap[i1_id]),
                  Namer::getInstrId(IdToInstMap[i2_id]));
              BB = IdToLoopMap_global[loop_id];

              // Adding this to see variable names-- TRM
              Instruction * temp1, * temp2;
              unsigned ln1, ln2;


              dfile << "[";
              temp1 = IdToInstMap[i1_id]; temp2 = IdToInstMap[i2_id];


              if (isa<LoadInst>(temp1) || isa<StoreInst>(temp1))
              {
                Value * myV;
                if (isa<LoadInst>(temp1))
                  myV = dyn_cast<LoadInst>(temp1)->getPointerOperand();
                else if (isa<StoreInst>(temp1))
                  myV = dyn_cast<StoreInst>(temp1)->getPointerOperand();
                else
                  myV = temp1->getOperand(0);  // try it anyway ???
                int i = 0;
                Instruction * myI = temp1;

                const DebugLoc & DSI = myI->getDebugLoc();


                if(DSI)
                {
                  //                           std::string file;
                  ln1 = DSI.getLine();
                  //                           GetConstantStringInfo(DSI->getFileName(), file);
                  //                           LLVM_DEBUG(errs() << "\n!!!! Line # is " << ln1 << " file " << file << "\n\n");
                }
                else
                {
                  ln1 = 0;
                  LLVM_DEBUG(errs() << "No DSI\n");
                }

                while (myV->getName().str() == "")
                {
                  if (myI && (dyn_cast<User>(myI))->getNumOperands() != 0)
                  {
                    if (isa<LoadInst>(myI))
                    {
                      myV = dyn_cast<LoadInst>(myI)->getPointerOperand();
                      myI = dyn_cast<Instruction>(dyn_cast<LoadInst>(myI)->getPointerOperand());
                    }
                    else if (isa<StoreInst>(myI))
                    {
                      myV = dyn_cast<StoreInst>(myI)->getPointerOperand();
                      myI = dyn_cast<Instruction>(dyn_cast<StoreInst>(myI)->getPointerOperand());
                    }
                    else
                    {

                      if ((dyn_cast<User>(myI))->getNumOperands() == 1)
                      {


                        myV = myI->getOperand(0);  // try it anyway ???
                        myI = dyn_cast<Instruction>(myI->getOperand(0));



                        LLVM_DEBUG(errs() << "DDOK" << i << "\t" << i1_id << "\n");
                      }
                      else
                      {  // should recurse in multiple directions here
                        //myV = myI->getOperand(0);  // try it anyway ???
                        //myI = dyn_cast<Instruction>(myI->getOperand(0));
                        //LLVM_DEBUG(errs() << "DDfail" << i << "\t" << i1_id << "\n");

                        LLVM_DEBUG(errs() << "DDattempting recurse" << j++ << "\t" << i1_id << "\n");
                        globLoop = loop_id;
                        ISet avoidInfiniteRecursion;
                        recurseOperands(myI, temp1->getParent()->getParent()->getName().str(),
                            temp1->getParent()->getName().str(), 1, ln1,avoidInfiniteRecursion);
                        needCheck++;
                        break;
                      }
                    }

                  }
                  else
                  {
                    //                              dfile << "##ORIG";
                    break;
                  }


                }
                dtotal++;
                if (myV->getName().str() == "")
                  dfile << "##ORIG";
                dfile << myV->getName().str();
                addToList(myV->getName().str(),
                    temp1->getParent()->getParent()->getName().str(),
                    temp1->getParent()->getName().str(), 1, ln1);
              }

              dfile << " @ " << temp1->getParent()->getParent()->getName().str()
                << " * " << temp1->getParent()->getName().str() << " -- ";
              if (isa<LoadInst>(temp2) || isa<StoreInst>(temp2))
              {
                Value * myV;
                if (isa<LoadInst>(temp2))
                  myV = dyn_cast<LoadInst>(temp2)->getPointerOperand();
                else if (isa<StoreInst>(temp2))
                  myV = dyn_cast<StoreInst>(temp2)->getPointerOperand();
                else
                  myV = temp2->getOperand(0);  // try it anyway ???

                Instruction * myI = temp2;

                const DebugLoc & DSI = myI->getDebugLoc();


                if(DSI)
                {
                  ln2 = DSI.getLine();
                  // LLVM_DEBUG(errs() << "\n!!!! Line # is " << ln1 << " inst " << myV->getName().str() << "\n\n");
                }
                else
                {
                  ln2 = 0;
                  LLVM_DEBUG(errs() << "No DSI\n");
                }

                int i = 0;
                while (myV->getName().str() == "")
                {
                  if (myI && (dyn_cast<User>(myI))->getNumOperands() != 0)
                  {
                    if (isa<LoadInst>(myI))
                    {
                      myV = dyn_cast<LoadInst>(myI)->getPointerOperand();
                      myI = dyn_cast<Instruction>(dyn_cast<LoadInst>(myI)->getPointerOperand());
                    }
                    else if (isa<StoreInst>(myI))
                    {
                      myV = dyn_cast<StoreInst>(myI)->getPointerOperand();
                      myI = dyn_cast<Instruction>(dyn_cast<StoreInst>(myI)->getPointerOperand());
                    }
                    else
                    {
                      if ((dyn_cast<User>(myI))->getNumOperands() == 1)
                      {
                        myV = myI->getOperand(0);  // try it anyway ???
                        myI = dyn_cast<Instruction>(myI->getOperand(0));
                        LLVM_DEBUG(errs() << "DDOK" << i << "\t" << i2_id << "\n");
                      }
                      else
                      {  // should recurse in multiple directions here
                        //myV = myI->getOperand(0);  // try it anyway ???
                        //myI = dyn_cast<Instruction>(myI->getOperand(0));
                        //LLVM_DEBUG(errs() << "DDfail" << i << "\t" << i2_id << "\n");

                        LLVM_DEBUG(errs() << "attempting recurse" << j++ << "\t" << i2_id << "\n");
                        globLoop = loop_id;
                        ISet avoidInfiniteRecursion;
                        recurseOperands(myI, temp2->getParent()->getParent()->getName().str(),
                            temp2->getParent()->getName().str(), 2, ln2,avoidInfiniteRecursion);
                        needCheck++;
                        break;
                      }
                    }
                  }
                  else
                  {
                    //                              dfile << "##ORIG";
                    break;
                  }
                  i++;
                }
                if (myV->getName().str() == "")
                  dfile << "##ORIG";
                dfile << myV->getName().str();
                addToList(myV->getName().str(),
                    temp1->getParent()->getParent()->getName().str(),
                    temp1->getParent()->getName().str(), 2, ln2);
              }


              dfile << " @ " << temp2->getParent()->getParent()->getName().str()
                << " * " << temp2->getParent()->getName().str() << "]\t";

              // TRM
              if (ln1 == 0)
                dfile << "UNKNOWN";
              else
                dfile << ln1;
              dfile << "#";

              if (ln2 == 0)
                dfile << "UNKNOWN";
              else
                dfile << ln2;

              dfile << "#\t";

              dfile << temp1 << " " << temp2 << " ID:" << i1_id << " " << i2_id
                << " Loop:" << loop_id << " " << "(" << BB << ")" ;

              // Add this dep to the map
//              iter = LoopToDepSetMap.find(BB);
//              if (iter == LoopToDepSetMap.end() ) {   // newly found loop
//                InstPairSet* dep_pair_set_ptr = new InstPairSet;
//                dep_pair_set_ptr->insert(dep_inst_pair);
//                LoopToDepSetMap[BB] = *dep_pair_set_ptr;
//              } else {                                 // loop already in the map
                LoopToDepSetMap[BB].insert(dep_inst_pair);
//              }


              // Store the values in the maps with their Namer::id so we can survive changes to IR
              biikey_t biikey(BB, Namer::getInstrId(IdToInstMap[i1_id]),
                Namer::getInstrId(IdToInstMap[i2_id]), cross_iter > 0);
              DepToCountMap[biikey] = times;

              dfile << " T:" << times << " P:" << (double)times/itercounts[loop_id]
                << "  " << LoopToDepSetMap.size();

              if (needCheck != 0)
                dumpListToFile(0);
              else
                purgeList();
            }
          }

        }


        break;
      case 0: /* Number of iterations of the loop that the dependence manifested */
        {
          biikey_t biikey(BB, Namer::getInstrId(IdToInstMap[i1_id]),
              Namer::getInstrId(IdToInstMap[i2_id]), cross_iter);
          if(cross_iter)
          {
            if( num != 0)
            {
              double pl = (double)(num) / itercounts[loop_id];
              lcfile << " Tl:" << num << " Pl:" << pl;
              biimap[biikey] = pl;
            }
            lcfile << '\n';
          } else {
            if( num != 0)
            {
              double pl = (double)(num) / itercounts[loop_id];
              dfile << " Tl:" << num << " Pl:" << pl;
              biimap[biikey] = pl;
            }
            dfile << '\n';
          }
        }
        break;
      default:
        break;
    }
  } // end of while(ifs >> s)


  lcfile.close();
  dfile.close();

  std::map<BasicBlock*, InstPairSet > :: iterator  Liter;
  InstPairSet :: iterator  Siter;

  //for (int i = lamp_id+1; i< ;i ++ )
  LLVM_DEBUG(errs() << "Num of cross-dep Loops: "<< LoopToDepSetMap.size() << '\n');
  /*
  // What is the point of all this?
  // Figuring out the max deps in the program and printing the two instruction #s?
  for (Liter = LoopToDepSetMap.begin();Liter != LoopToDepSetMap.end(); Liter++){
  max_times = 0;
  for (Siter = Liter->second.begin();Siter != Liter->second.end(); Siter++){
  times = DepToTimesMap[Siter];
  if (times > max_times){
  max_times = times;
  Id1 = InstToIdMap[(Siter)->first];
  Id2 = InstToIdMap[(Siter)->second];
  }
  }
  LoopToMaxDepTimesMap[Liter->first] = max_times;
  LLVM_DEBUG(errs() << "LoopToIDMap_global[Liter->frist]   max_times   (Id1, Id2)\n");
  LLVM_DEBUG(errs() << LoopToIdMap_global[Liter->first] << " " << max_times
               << " ("  << Id1 << "," << Id2 << ")"<< '\n');

  }
   */

  // intentionally skip loop 0, since that is the global contex.
  for(unsigned i=1; i<MAX_LOOPS; i++)
  {
    if( itercounts[i] > 0 )
    {
      BasicBlock *header = IdToLoopMap_global[i];
      if( header )
      {
        // Function *fcn = header->getParent();
//        errs() << "NONZERO " << fcn->getName() << ' ' << header->getName() << '\n';
      }
    }
  }

  return true;
}

// WHAT DOES THIS DO!?
void recurseOperands(Instruction * myI, std::string fnName, std::string bbName, int list, unsigned line, ISet &avoidInfiniteRecursion)
{
  if (!myI)
  {
    LLVM_DEBUG(errs() << "T@NI\n");
    return; // terminated at null instruction
  }
  if( avoidInfiniteRecursion.count(myI) )
    return;
  avoidInfiniteRecursion.insert(myI);


  Value * myV;
  if (isa<LoadInst>(myI))
  {
    myV = dyn_cast<LoadInst>(myI)->getPointerOperand();
    if (myV->getName().str() == "")
      recurseOperands(dyn_cast<Instruction>(dyn_cast<LoadInst>(myI)->getPointerOperand()), fnName, bbName, list, line,avoidInfiniteRecursion);
    else
      addToList(myV->getName().str(), fnName, bbName, list, line);
  }
  else if (isa<StoreInst>(myI))
  {
    myV = dyn_cast<StoreInst>(myI)->getPointerOperand();
    if (myV->getName().str() == "")
      recurseOperands(dyn_cast<Instruction>(dyn_cast<StoreInst>(myI)->getPointerOperand()), fnName, bbName, list, line,avoidInfiniteRecursion);
    else
      addToList(myV->getName().str(), fnName, bbName, list, line);
  }
  else if ((dyn_cast<User>(myI))->getNumOperands() == 1)
  {
    myV = myI->getOperand(0);
    if (myV->getName().str() == "")
      recurseOperands(dyn_cast<Instruction>(myI->getOperand(0)), fnName, bbName, list, line,avoidInfiniteRecursion);
    else
      addToList(myV->getName().str(), fnName, bbName, list, line);
  }
  else //if ((dyn_cast<User>(myI))->getNumOperands() == 2)
  {
    LLVM_DEBUG(errs() << "Crazy Recursion attempt " << (dyn_cast<User>(myI))->getNumOperands() << "\n\n");
    for (unsigned int i = 0; i < (dyn_cast<User>(myI))->getNumOperands(); i++)
    {
      myV = myI->getOperand(i);
      if (myV->getName().str() == "")
        recurseOperands(dyn_cast<Instruction>(myI->getOperand(i)), fnName, bbName, list, line, avoidInfiniteRecursion);
      else
        addToList(myV->getName().str(), fnName, bbName, list, line);
    }

  }
  //   else
  //  {
  //     LLVM_DEBUG(errs() << "Recursion abortion\n\n");
  // }
}


unsigned int LAMPLoadProfile::numObsIntraIterDep(BasicBlock *BB, const Instruction *i1, const Instruction *i2)
{
  biikey_t key(BB, Namer::getInstrId(i1), Namer::getInstrId(i2), 0);
  if(DepToCountMap.count(key))
  {
    return DepToCountMap[key];
  }
  return 0;
}

unsigned int LAMPLoadProfile::numObsInterIterDep(BasicBlock *BB, const Instruction *i1, const Instruction *i2)
{
  biikey_t key(BB, Namer::getInstrId(i1), Namer::getInstrId(i2), 1);
  if(DepToCountMap.count(key))
  {
    return DepToCountMap[key];
  }
  return 0;
}

unsigned int LAMPLoadProfile::numObsIterDep(BasicBlock *BB, Instruction *i1, Instruction *i2)
{
  unsigned int ret = 0;

  biikey_t key(BB, Namer::getInstrId(i1), Namer::getInstrId(i2), 1);
  if(DepToCountMap.count(key))
  {
    ret = DepToCountMap[key];
  }

  biikey_t key2(BB, Namer::getInstrId(i1), Namer::getInstrId(i2), 0);
  if(DepToCountMap.count(key2))
  {
    ret += DepToCountMap[key2];
  }

  return ret;
}


/**
 * Return the probability of this dependence occuring for the loop BB
 */
double LAMPLoadProfile::probDep(BasicBlock *BB, Instruction *i1, Instruction *i2, int cross)
{
  biikey_t key(BB, Namer::getInstrId(i1), Namer::getInstrId(i2), cross);
  if(biimap.count(key))
  {
    return biimap[key];
  }
  return 0;
}


bool LAMPLoadProfile::isValid() {
  std::ifstream ifs;
  struct stat sInfo;

  if(stat(ProfileFileName.c_str(), &sInfo ) !=0){    // need to add file name
    //      std::cerr << "Could not find file result.lamp.profile\n";
    return false;
  }

  return true;
}


void ps(std::string printed)
{
  errs() << printed;
}

void addToList(std::string name, std::string fnName, std::string bbName, int list, unsigned line)
{
  varNames * newnode = new varNames;

  newnode->name = name;
  newnode->fnName = fnName;
  newnode->bbName = bbName;
  newnode->line = line;
  newnode->loop = globLoop;

  if (list == 1)
  {
    newnode->next = first;
    first = newnode;
  }
  else
  {
    newnode->next = second;
    second = newnode;
  }
}

void dumpListToFile(int lc)
{
  varNames * temp;
  varNames * walk1 = first;
  varNames * walk2;

  while (walk1)
  {
    walk2 = second;
    if (walk1->name != "")
      while(walk2)
      {
        if (walk2->name != "")
        {
          auxfile << "[" << walk1->name << " @ " << walk1->fnName << " * " << walk1->bbName
            << " -- " << walk2->name << " @ " << walk2->fnName << " * " << walk2->bbName
            << "] " << lc << " ";
          if (walk1->line == 0)
            auxfile << "UNKNOWN";
          else
            auxfile << walk1->line;
          auxfile << "#";

          if (walk2->line == 0)
            auxfile << "UNKNOWN";
          else
            auxfile << walk2->line;

          auxfile << "# " << "Loop: " << walk2->loop << " $$\n";


          // auxfile << walk1->line << " # " << walk2->line << " # $$\n";
          LLVM_DEBUG(errs() << "FOUND: " << walk1->name << " -- " << walk2->name << "\n");
        }

        walk2 = walk2->next;
      }

    temp = walk1;
    walk1 = walk1->next;
    delete temp;
  }

  walk2 = second;
  while (walk2)
  {
    temp = walk2;
    walk2 = walk2->next;
    delete temp;
  }

  first = NULL;
  second = NULL;
  needCheck = 0;

}

void purgeList()
{
  varNames * temp;
  varNames * walk2;

  walk2 = first;
  while (walk2)
  {
    temp = walk2;
    walk2 = walk2->next;
    delete temp;
  }

  walk2 = second;
  while (walk2)
  {
    temp = walk2;
    walk2 = walk2->next;
    delete temp;
  }

  first = NULL;
  second = NULL;
}


