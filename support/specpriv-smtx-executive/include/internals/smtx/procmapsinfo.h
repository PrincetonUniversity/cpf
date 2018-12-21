#ifndef PROCMAPSINFO
#define PROCMAPSINFO

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>

#include <vector>
#include <set>

namespace procmaps
{

using namespace std;

template <class T>
static T string_to(string s)
{
  T ret;
  stringstream ss;
  ss << s;
  ss >> ret;

  if (!ss)
  {
    assert(false && "Failed to convert string to given type\n");
  }

  return ret;
}

static size_t split(const string s, vector<string>& tokens, char delim)
{
  tokens.clear();

  stringstream ss(s);
  string       item;

  while ( getline(ss, item, delim) )
    if ( !item.empty() )
      tokens.push_back(item);

  return tokens.size();
}

struct Region
{
  void* begin;
  void* end;

  bool  read;
  bool  write;
  bool  execute;
  bool  shared;

  uint64_t    offset;
  string dev;
  uint64_t    inode;
  string pathname;

  Region(string line)
  {
    vector<string> tokens;
    split(line, tokens, ' ');

    size_t tokens_size = tokens.size();
    assert( tokens_size == 5 || tokens_size == 6 );

    // addresses

    vector<string> addrs;
    split( tokens[0], addrs, '-' );

    this->begin = (void*)strtoul( addrs[0].c_str(), NULL, 16);
    this->end   = (void*)strtoul( addrs[1].c_str(), NULL, 16);

    // permissions

    this->read  = ( tokens[1][0] == 'r' );
    this->write = ( tokens[1][1] == 'w' );
    this->execute = ( tokens[1][2] == 'x' );
    this->shared = ( tokens[1][3] == 's' );


    // offset
    
    this->offset = string_to<uint64_t>( tokens[2] );

    // dev
 
    this->dev = tokens[3];

    // inode

    this->inode = string_to<uint64_t>( tokens[4] );
  
    // pathname

    if ( tokens_size == 6 )
      this->pathname = tokens[5];
  }
};

struct CompareRegion
{
  bool operator()(const Region& r1, const Region& r2) const
  {
    return r1.begin < r2.begin;
  }
};

class ProcMapsInfo
{
public:
  typedef set<Region, CompareRegion> RegionSet;

  ProcMapsInfo() 
  {
    regions = new RegionSet();

    string line;
    ifstream myfile ("/proc/self/maps");
    if (myfile.is_open())
    {
      while ( getline (myfile,line) )
      {
        if ( line.empty() )
          continue;
        Region r(line);
        regions->insert(r);
      }
      myfile.close();
    }
    else
      assert( false && "Cannot open /proc/self/maps" );
  }

  ~ProcMapsInfo() { delete regions; }

  bool isReadOnly(void* addr)
  {
    for ( RegionSet::iterator i=regions->begin(),e=regions->end() ; (i!=e && i->begin<=addr) ; i++ )
      if ( i->begin <= addr && addr < i->end )
        return ( i->read && !i->write );

    return false;
  }

  bool doesExist(void* addr)
  {
    for ( RegionSet::iterator i=regions->begin(),e=regions->end() ; (i!=e && i->begin<=addr) ; i++ )
      if ( i->begin <= addr && addr < i->end )
        return true;

    return false;
  }

  pair<void*,void*> getStackRegion()
  {
    for ( RegionSet::iterator i=regions->begin(),e=regions->end() ; i!=e ; i++)
      if ( i->pathname.compare("[stack]") == 0)
        return make_pair(i->begin, i->end);

    assert( false && "Cannot find stack region\n" );
  }

private:
  RegionSet* regions;
};

}

#endif
