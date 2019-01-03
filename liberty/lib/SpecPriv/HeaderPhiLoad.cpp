#include "liberty/SpecPriv/HeaderPhiLoad.h"

#include "llvm/Support/raw_ostream.h"

#include <iostream>
#include <fstream>
#include <sstream>

namespace liberty
{

using namespace llvm;

char HeaderPhiLoadProfile::ID = 0;

static RegisterPass<HeaderPhiLoadProfile> RP(
  "headerphi-load-profile",
   "(HeaderPhiLoad) Load back profile data and generate dependency information", false, false);

template <class T>
static T string_to(std::string s)
{
  T ret;
  std::stringstream ss(s);
  ss >> ret;

  if (!ss)
  {
    assert(false && "Failed to convert string to given type\n");
  }

  return ret;
}

HeaderPhiLoadProfile::HeaderPhiLoadProfile() : ModulePass(ID)
{
}

HeaderPhiLoadProfile::~HeaderPhiLoadProfile()
{
}

void HeaderPhiLoadProfile::getAnalysisUsage(AnalysisUsage& au) const
{
  au.setPreservesAll();
}

bool HeaderPhiLoadProfile::runOnModule(Module& m)
{
  std::ifstream ifs( "headerphi_prof.out" );

  if ( !ifs.is_open() )
  {
   errs() <<  "headerphi_prof.out cannot be opened\n";
   return false;
  }

  std::string line;

  while ( getline(ifs, line) )
  {
    uint64_t id = string_to<uint64_t>( line );
    predictable.insert( id );
  }

  ifs.close();
  return false;
}

bool HeaderPhiLoadProfile::isPredictable(uint64_t id)
{
  return predictable.count( id );
}

}
