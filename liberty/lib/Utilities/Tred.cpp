#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Program.h"

#include "liberty/Utilities/Tred.h"
#include <string>

namespace liberty
{
using namespace llvm;

static cl::opt<bool> DontRunTred(
  "specpriv-dont-compute-tred", cl::init(true), cl::NotHidden,
  cl::desc("Do not compute transitive reductions"));

void runTred(const char *infile, const char *outfile, unsigned timeout)
{
  if( DontRunTred )
    return;

  StringRef  binary(PATH_TO_TRED);
  StringRef  input(infile);
  StringRef  output(outfile);
  StringRef  devnull;

  //sot
  //StringRef args[] = {PATH_TO_TRED, 0};
  const char *args[] = {PATH_TO_TRED, ""};
  const StringRef  *redirects[] = { &input, &output, &devnull };
  std::string errMsg;

  //sot
  sys::ExecuteAndWait(binary,args,(const char**)0,redirects,timeout,0U,&errMsg);
}
}
