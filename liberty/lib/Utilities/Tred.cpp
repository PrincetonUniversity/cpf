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

  ArrayRef<StringRef> args = {PATH_TO_TRED, ""};
  ArrayRef<Optional<StringRef>> redirects = {Optional<StringRef>(input),
                                             Optional<StringRef>(output),
                                             Optional<StringRef>(devnull)};
  std::string errMsg;
  sys::ExecuteAndWait(binary, args, None, redirects, timeout, 0U, &errMsg);
}
}
