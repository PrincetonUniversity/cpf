#ifndef SPECPRIV_ESCAPE_H
#define SPECPRIV_ESCAPE_H

#include "holder.h"
#include "live.h"
#include "context.h"

#include <ostream>

typedef std::pair<AUHolder, CtxHolder> Escape;

struct EscapeTable
{
  void report_escape(const AUHolder &, const CtxHolder &);
  void report_local(const AUHolder &, const CtxHolder &);

  void print(std::ostream &fout) const;

private:
  typedef std::map<Escape,unsigned> EscapeMap;
  EscapeMap escapeFrequencies, localFrequencies;
};

std::ostream &operator<<(std::ostream &fout, const EscapeTable &et);

#endif

