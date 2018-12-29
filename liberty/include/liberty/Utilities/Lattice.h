// Lattice
// A class which lets you build a lattice
// from any type with a partial order.
// It also allows you to compute a
// topological sort of that lattice.
#ifndef LLVM_LIBERTY_LATTICE_H
#define LLVM_LIBERTY_LATTICE_H

#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/Support/raw_ostream.h"
#include "liberty/Utilities/TwoList.h"

#include <list>

namespace liberty
{

using namespace llvm;

template <class Type> class LatticeNode;

/// An iterator which visits all
/// ancestors or descendants of a node in the lattice.
template <class Type>
class NodeDescendantIterator
{
  public:
    typedef LatticeNode<Type>             Node;
    typedef std::list<Node*>              Fringe;
    typedef NodeDescendantIterator<Type>  iterator;
  private:
    Fringe                                fringe;
    Node *                                next;
    bool                                  isForward;
    bool                                  isEnd;

  public:
    /// Construct the end iterator
    NodeDescendantIterator()
      : fringe(), next(), isForward(), isEnd(true)
    {}

    /// Construct an iterator which starts from
    /// and includes 'first'.  If 'f'==true, then this
    /// iterator will visit descendants of 'first';
    /// otherwise it will visit ancestors of 'first'.
    NodeDescendantIterator(Node *first, bool f = true)
      : fringe(), next(), isForward(f), isEnd(false)
    {
      fringe.push_back( first );
      ++ *this;
    }

    /// Dereference the iterator
    Node * operator*() const { return next; }

    /// Advacne the iterator
    iterator &operator++()
    {
      if( fringe.empty() )
      {
        next = 0;
        isEnd = true;
      }
      else
      {
        next = fringe.front();
        fringe.pop_front();

        if( isForward )
          fringe.insert( fringe.begin(),
            next->succ_begin(), next->succ_end() );
        else
          fringe.insert( fringe.begin(),
            next->pred_begin(), next->pred_end() );
      }

      return *this;
    }

    /// Iterator comparison.  Only works if one
    /// of the two iterators is the end iterator.
    /// This is good enough for the common case,
    /// but not the general case.
    bool operator!=(const iterator &other) const
    {
      assert( (this->isEnd || other.isEnd)
      && "This comparison only works against an end iterator");
      return this->isEnd != other.isEnd;
    }
};

/// Represents a node in the lattice.
/// A node is two adjacency lists
/// for predecessors and successors.
template <class Type>
class LatticeNode
{
  public:
    typedef LatticeNode<Type>             Node;
    typedef NodeDescendantIterator<Type>  iterator;
    // Expected size is <= 2.
    typedef SmallVector<Node*,4>          NodePtrs;

    LatticeNode(Type &v)
      : value(v), adjacencies() {}

    /// get a forward iterator to all descendants
    /// (transitive closure of successors) of this
    /// node.
    iterator desc_begin() { return iterator(this); }

    /// The end iterator for desc_begin
    iterator desc_end() { return iterator(); }

    /// Get an iterator to all ancestors
    /// (trans closure of predecessors) of this node.
    iterator ancest_begin() { return iterator(this,false); }

    /// The end iterator for ancest_begin
    iterator ancest_end() { return iterator(); }

    /// determine the number of predecessors
    int num_preds() const { return adjacencies.size_a(); }

    /// get a forward iterator to the immediate precedessors of this node
    typename NodePtrs::const_iterator pred_begin() { return adjacencies.begin_a(); }

    /// The end iterator for pred_begin()
    typename NodePtrs::const_iterator pred_end() { return adjacencies.end_a(); }

    /// add a new precesessor
    void push_pred(Node *pred) { adjacencies.push_a(pred); }

    /// determine the number of successors
    int num_succs() const { return adjacencies.size_z(); }

    /// get a forward iterator to the immediate successors of this node
    typename NodePtrs::const_iterator succ_begin() { return adjacencies.begin_z(); }

    /// The end iterator for succ_begin().
    typename NodePtrs::const_iterator succ_end() { return adjacencies.end_z(); }

    /// add a new successor
    void push_succ(Node *succ) { adjacencies.push_z(succ); }

    Type &getValue() { return value; }

  private:
    Type value;

    // This is a packed vector.
    // Elements 0 -- (inDegree-1) are predecessors
    // Elements inDegree -- (n-1) are successors.
    TwoList<Node*,NodePtrs> adjacencies;
};


#define INFINITE_DEGREE           (~0U - 8U)

/// This is a one-directional iterator (either forward
/// or reverse)
/// that also offers a /prune/ operation.
/// It visits the elements of the lattice in
/// forward/reverse topological order.
/// This iterator is not always invalidated
/// by changes to the lattice; adding a new
/// element to the lattice will not break an
/// iterator, but the new item may not appear
/// within the results of the iterator, or may
/// not be pruned.
template <class Type>
class LatticeIterator
{
  public:
    typedef LatticeNode<Type> Node;
    typedef std::list<Node*> NodePtrs;
    typedef LatticeIterator<Type> iterator;

    /// construct the ``end'' iterator
    LatticeIterator() : isForward(), isEnd(true) {}

    /// construct a non-``end'' iterator
    template <class InputIterator>
    LatticeIterator(InputIterator roots_begin, InputIterator roots_end, bool forward = true)
      : isForward(forward), isEnd(false), next(0), fringe( roots_begin, roots_end ), degrees()
    {
      ++ *this;
    }

    /// This method communicates to the iterator
    /// that the iterator should skip /badNode/
    /// and all of its descendants.  It does not
    /// remove this from the lattice; rather, it
    /// hides parts of it from iteration.
    /// This will only work 100% for nodes which
    /// are in the lattice when prune is called;
    /// if you add vertices to the lattice after
    /// prune, they may not be pruned.
    /// Not very efficient---O(n)---but could probably
    /// be made more efficient.
    void prune( Node *badNode )
    {
      // This can be sort of ugly, because nodes
      // to-be-pruned may already be in the iterator's fringe.

      // do a dfs from this node, setting degrees[n] to -1
      // for this and all of its descendants.
      NodePtrs fringe;
      fringe.push_back(badNode);
      while( ! fringe.empty() )
      {
        Node *n = fringe.front();
        fringe.pop_front();

        if( degrees.count(n) && degrees[n] == INFINITE_DEGREE )
          continue;

        degrees[n] = INFINITE_DEGREE;

        if( isForward )
          fringe.insert( fringe.begin(), n->succ_begin(), n->succ_end() );
        else
          fringe.insert( fringe.begin(), n->pred_begin(), n->pred_end() );
      }
    }

    /// dereference the iterator
    Node *  operator*() const { return next; }

    /// advance the iterator to the next node
    /// in forward/reverse topological order
    iterator &operator++()
    {
      isEnd = true;

      do
      {
        if( fringe.empty() )
          return *this;

        next = fringe.front();
        fringe.pop_front();

        // make sure this hasn't been pruned
      } while( degrees.count(next) && degrees[next] == INFINITE_DEGREE );

      isEnd = false;

      // consider its successors
      // Yeah, this is ugly.  Thanks llvm for not
      // defining the assignment operator for SmallVector<>::iterator
      typename Node::NodePtrs::const_iterator i = (isForward) ? (next->succ_begin()) : (next->pred_begin()),
                                              e = (isForward) ? (next->succ_end())   : (next->pred_end());

      for(; i!=e; ++i)
      {
        Node *n = *i;

        if( !degrees.count(n) )
        {
          if( isForward )
            degrees[n] = n->num_preds();
          else
            degrees[n] = n->num_succs();
        }

        if( degrees[n] != INFINITE_DEGREE )
        {
          -- degrees[n];

          if( degrees[n] == 0 )
            fringe.push_front( n );
        }
      }


      return *this;
    }

    /// Compare two iterators
    /// This will only successfully compare to the /end/ iterators.
    /// This is good enough for the typical iteration pattern, but
    /// not the general case.
    bool operator!=(iterator &other) const
    {
      assert( (this->isEnd || other.isEnd)
      && "This comparison only works against an end iterator");
      return this->isEnd != other.isEnd;
    }

  private:
    const bool                 isForward;
    bool                       isEnd;
    Node *                     next;
    NodePtrs                   fringe;

    DenseMap<Node*, unsigned>  degrees;
};



template <class Type>
struct PartialOrder
{
  virtual ~PartialOrder() {}
  virtual bool compare(Type &a, Type &b) = 0;
};

template <class Type>
struct ValueWriter
{
  virtual ~ValueWriter() {}
  virtual void write(Type &a, raw_ostream &fout) = 0;
};

/// A lattice which corresponds to a set of
/// objects and their partial order.
template <class Type>
class Lattice
{
  public:
    typedef LatticeNode<Type> Node;
    typedef std::list<Node*> NodePtrs;
    typedef LatticeIterator<Type> iterator;

    Lattice(PartialOrder<Type> *po) : sz(0), sources(), sinks(), order(po) {}
    ~Lattice()
    {
      clear();
      delete order;
    }

    void clear()
    {
      for( iterator i=begin(), e=end(); i!=e; ++i )
      {
        Node *n = *i;
        delete n;
      }
      sz=0;
      sources.clear();
      sinks.clear();
    }

    /// Determine the number of values in the lattice
    unsigned size() const { return sz; }

    /// Visit the nodes in forward, depth-first topological sort order
    iterator begin() const { return iterator(sources.begin(), sources.end()); }

    /// The end iterator corresponding to begin()
    iterator end() const { return iterator(); }

    /// Visit the nodes in reverse, depth-first topological sort order
    iterator rbegin() const { return iterator(sinks.begin(), sinks.end(), false); }

    /// The end iterator corresponding to rbegin()
    iterator rend() const { return iterator(); }

    /// Visit the sources, sinks of the lattice
    typename NodePtrs::const_iterator sources_begin() const { return sources.begin(); }
    typename NodePtrs::const_iterator sources_end() const { return sources.end(); }

    typename NodePtrs::const_iterator sinks_begin() const { return sinks.begin(); }
    typename NodePtrs::const_iterator sinks_end() const { return sinks.end(); }

    /// Add a new element to the lattice
    /// Worst case is O(n); expected case is much
    /// better (assuming prune is efficient).
    /// This may or may not affect your lattice iterators.
    /// If elt will be a source of the lattice, it will
    /// not be visited by any existing forward iterators.
    /// If elt will be a sink of the lattice, it will
    /// not be visited by any existing reverse iterators.
    /// If elt is a descendant of another node A, and if
    /// A has been pruned in an iterator, elt may
    /// not be pruned.
    void insert(Type &elt)
    {
      sz++;

      Node *newNode = new Node(elt);

      // search forward
      for( iterator i=begin(), e=end(); i!=e; ++i)
      {
        Node *n = *i;
        Type v = n->getValue();

        if( order->compare(elt, v) )
        {

          newNode->push_succ(n);

          if( n->num_preds() == 0 )
            sources.remove(n);

          n->push_pred(newNode);

          // do not visit any successors of n
          i.prune(n);
        }
      }

      // search reverse
      for( iterator i=rbegin(), e=rend(); i!=e; ++i)
      {
        Node *n = *i;
        Type v = n->getValue();



        // We may find the new node inserted in the previous loop.
        if( n == newNode )
        {
          i.prune(n);
          continue;
        }

        if( order->compare(v, elt) )
        {

          // In the case of equality, we break ties
          // in this arbitrary, though consistent manner.
          if( ! order->compare(elt,v) )
          {
            newNode->push_pred(n);

            if( n->num_succs() == 0 )
              sinks.remove(n);

            n->push_succ(newNode);

            // do not visit any predecessors of n
            i.prune(n);
          }
        }
      }

      if( newNode->num_preds() == 0 )
        sources.push_back( newNode );

      if( newNode->num_succs() == 0 )
        sinks.push_back( newNode );

    }

    void dump( raw_ostream &fout, ValueWriter<Type> &vw )
    {
      fout << "digraph lattice {\n"
           << "\tsize=\"8x11\";\n"
           << "\tratio=\"fill\";\n";


      for( iterator i=begin(), e=end(); i!=e; ++i)
      {
        Node *node = *i;

        fout << "node" << (unsigned long)node
             << " [shape=box,label=\"";

        vw.write( node->getValue(), fout);

        fout << "\"];\n";
      }

      for( iterator i=begin(), e=end(); i!=e; ++i)
      {
        Node *node = *i;
        for( typename Node::NodePtrs::const_iterator i=node->succ_begin(), e=node->succ_end(); i!=e; ++i)
        {
          Node *succ = *i;
          fout << "node" << (unsigned long)node << " -> node" << (unsigned long)succ << ";\n";
        }
      }

      fout << "}\n";
    }

  private:
    unsigned              sz;
    NodePtrs              sources, sinks;
    PartialOrder<Type> *  order;
};


}

#endif // LLVM_LIBERTY_LATTICE_H
