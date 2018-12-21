#include "context.h"

static void flatten(CtxHolder ctx, std::vector<CtxHolder> &result)
{
  if( ctx.is_null() )
    return;

  result.push_back( ctx );

  flatten(ctx->parent, result);
}

CtxHolder Context::innermostFunction()
{
  Context *c = this;
  while( c->type == Loop )
    c = *c->parent;

  return c;
}

CtxHolder Context::findCommon(const CtxHolder &other)
{
  std::vector<CtxHolder> a, b;
  flatten(this,a);
  flatten(other,b);

  const unsigned N=a.size(), M=b.size();
  unsigned lastGood = 0;
  for(unsigned i=1; i<=std::min(N,M); ++i)
  {
    if( a[N-i] == b[M-i] )
      lastGood = i;
    else
      return a[N-lastGood];
  }

  return a[N-lastGood];
}

bool Context::operator==(const Context &other) const
{
  if( type != other.type )
    return false;
  else if( name != other.name )
    return false;
  else
    return parent == other.parent;
}

bool Context::operator<(const Context &other) const
{
  if( type < other.type )
    return true;
  else if( type > other.type )
    return false;

  else if( name < other.name )
    return true;
  else if( name > other.name )
    return false;

  else if( parent.is_null() && !other.parent.is_null() )
    return true;
  else if( !parent.is_null() && other.parent.is_null() )
    return false;

  else
    return parent < other.parent;
}

void Context::print(std::ostream &fout) const
{
  static const char *types[] = {"TOP", "FUNCTION", "LOOP"};

  fout << types[ type ];
  if( type != Top )
  {
    fout << ' ' << name;

    if( !parent.is_null() )
    {
      fout << " WITHIN ";
      parent->print(fout);
    }
  }
}

std::ostream &operator<<(std::ostream &fout, const CtxHolder &ctx)
{
  fout << " CONTEXT { ";
  if( !ctx.is_null() )
    ctx->print(fout);
  fout << " } ";

  return fout;
}



