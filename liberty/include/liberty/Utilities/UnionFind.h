#ifndef LLVM_LIBERTY_UNION_FIND_H
#define LLVM_LIBERTY_UNION_FIND_H

namespace liberty
{

  class UnionFind
  {
    UnionFind *immediate_representative;

  public:
    UnionFind() : immediate_representative(0) {}

    UnionFind *getRep()
    {
      if( immediate_representative == 0 )
        return this;

      immediate_representative = immediate_representative->getRep();

      return immediate_representative;
    }

    void merge(UnionFind *other)
    {
      UnionFind *rep1 = this->getRep();
      UnionFind *rep2 = other->getRep();

      if( rep1 != rep2 )
        rep2->immediate_representative = rep1;
    }
  };

}



#endif // LLVM_LIBERTY_UNION_FIND_H
