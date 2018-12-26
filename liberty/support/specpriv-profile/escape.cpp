#include "escape.h"

void EscapeTable::report_escape(const AUHolder &au, const CtxHolder &ctx)
{
  Escape key(au,ctx);
  ++escapeFrequencies[key];
}

void EscapeTable::report_local(const AUHolder &au, const CtxHolder &ctx)
{
  for(CtxHolder cc=ctx; !cc.is_null(); cc=cc->parent)
  {
    Escape key(au,cc);
    ++localFrequencies[key];
  }
}
void EscapeTable::print(std::ostream &fout) const
{
  for(EscapeMap::const_iterator i=escapeFrequencies.begin(), e=escapeFrequencies.end(); i!=e; ++i)
  {
    AUHolder au = i->first.first;
    CtxHolder ctx = i->first.second;
    unsigned count = i->second;

    fout << "ESCAPE OBJECT " << au << " ESCAPES " << ctx << " COUNT " << count << " ;\n";
  }

  for(EscapeMap::const_iterator i=localFrequencies.begin(), e=localFrequencies.end(); i!=e; ++i)
  {
    Escape key = i->first;

    if( escapeFrequencies.count(key) )
      continue;

    unsigned count = i->second;

    fout << "LOCAL OBJECT " << key.first << " IS LOCAL TO " << key.second << " COUNT " << count << " ;\n";
  }


}

std::ostream &operator<<(std::ostream &fout, const EscapeTable &et)
{
  et.print(fout);
  return fout;
}


