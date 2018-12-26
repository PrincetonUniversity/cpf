#include <stdint.h>
#include <stdio.h>

#include <map>
#include <set>

std::map<uint64_t, uint64_t> preheader_values;
std::set<uint64_t>           candidates;
std::set<uint64_t>           excludes;

extern "C"
{

void __headerphi_prof_invocation( uint64_t instid, uint64_t value )
{
  candidates.insert( instid );
  preheader_values[ instid ] = value;
}

void __headerphi_prof_iteration( uint64_t instid, uint64_t value )
{
  if ( preheader_values[instid] != value )
    excludes.insert( instid );
}

void __headerphi_prof_print()
{
  FILE* fp = fopen("headerphi_prof.out", "w");

  for ( std::set<uint64_t>::iterator si = candidates.begin() ; si != candidates.end() ; si++)
    if ( !excludes.count( *si ) )
      fprintf(fp, "%lu\n", *si);

  fclose(fp);
}

}
