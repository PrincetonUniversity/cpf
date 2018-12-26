#include "trailing_assert.h"
#include "prediction.h"

void IntSample::receive(const IntSample &other)
{
//  trailing_assert( *this == other );
  frequency += other.frequency;
}

void IntSample::print(std::ostream &fout) const
{
  fout << "INT " << value << " COUNT " << frequency;
}

void PtrSample::print(std::ostream &fout) const
{
  if( au.is_null() )
  {
    fout << "NO-SAMPLE";
    return;
  }

  fout << "OFFSET " << offset << " BASE " << au << " COUNT " << frequency;
}

std::ostream &operator<<(std::ostream &fout, const IntSample &sample)
{
  sample.print(fout);
  return fout;
}

std::ostream &operator<<(std::ostream &fout, const PtrSample &sample)
{
  sample.print(fout);
  return fout;
}

void PtrResidueSet::receive(void *sample)
{
  const uint64_t isample = (uint64_t)sample;
  const unsigned residue = isample & 0x0fu;
  const uint16_t bit_vector = 1u << residue;

  ++num_samples;
  residue_set |= bit_vector;
}

std::ostream &operator<<(std::ostream &fout, const PtrResidueSet &residues)
{
  residues.print(fout);
  return fout;
}

void PtrResidueSet::print(std::ostream &fout) const
{
  fout << "RESTRICTED ";

  unsigned pop_count = 0;
  for(unsigned i=0; i<16; ++i)
  {
    const uint16_t bit_mask = 1u << i;
    if( (residue_set & bit_mask) != 0 )
      ++pop_count;
  }

  fout << num_samples << " SAMPLES OVER " << pop_count << " MEMBERS { ";

  bool first = true;
  for(unsigned i=0; i<16; ++i)
  {
    const uint16_t bit_mask = 1u << i;
    if( (residue_set & bit_mask) != 0 )
    {
      if( !first )
        fout << " , ";
      first = false;

      fout << i;
    }
  }

  fout << " }";
}

PtrSample::PtrSample()
  : au(0), offset(0), frequency(0) {}

PtrSample::PtrSample(const AUHolder &AU, uint64_t off)
  : au(AU), offset(off), frequency(1) {}

bool PtrSample::operator==(const PtrSample &other) const
{
  return offset == other.offset && au == other.au;
}

void PtrSample::receive(const PtrSample &other)
{
//  trailing_assert( *this == other );
  frequency += other.frequency;
}

std::ostream &operator<<(std::ostream &fout, const PredictionTable &pt)
{
  pt.print(fout);
  return fout;
}

void PredictionTable::predict_int(const CtxHolder &ctx, const char *name, const IntSample &sample)
{
  CtxValue key(name,ctx);
  intPredictions[key].receive(sample);
}

void PredictionTable::predict_ptr(const CtxHolder &ctx, const char *name, const PtrSample &sample)
{
  CtxValue key(name,ctx);
  ptrPredictions[key].receive(sample);
}

void PredictionTable::find_underlying_object(const CtxHolder &ctx, const char *name, const PtrSample &sample)
{
  CtxValue key(name,ctx);
  objPredictions[key].receive(sample);
}

void PredictionTable::pointer_residue(const CtxHolder &ctx, const char *name, void *sample)
{
  CtxValue key(name,ctx);
  ptrResidues[key].receive(sample);
}

void PredictionTable::exit_ctx(const CtxHolder &ctx)
{
}

template <class SetTy>
void PredictionTable::print_samples(
  std::ostream &fout,
  const char *key,
  const SetTy &set) const
{
  fout << "# " << key << " size " << set.size() << '\n';

  for(typename SetTy::const_iterator i=set.begin(), e=set.end(); i!=e; ++i)
  {
    const typename SetTy::mapped_type &samples = i->second;
    if( !samples.is_worth_printing() )
      continue;

    const char *name = i->first.first;
    CtxHolder ctx = i->first.second;

    // Comment-out bottom samples
    if( samples.is_bottom() )
      fout << '#';

    fout << "PRED " << key
         << ' ' << name
         << " AT " << ctx
         << " AS " << samples << " ;\n";
  }
}

void PredictionTable::print_residues(std::ostream &fout) const
{
  fout << "# residue map size " << ptrResidues.size() << '\n';

  for(PtrResidueMap::const_iterator i=ptrResidues.begin(), e=ptrResidues.end(); i!=e; ++i)
  {
    const PtrResidueSet &residues = i->second;
    if( !residues.is_worth_printing() )
      continue;

    const char *name = i->first.first;
    CtxHolder ctx = i->first.second;

    fout << "PTR RESIDUES "
         << name
         << " AT " << ctx
         << " AS " << residues << " ;\n";
  }
}

void PredictionTable::print(std::ostream &fout) const
{
  print_samples(fout, "INT", intPredictions);
  print_samples(fout, "PTR", ptrPredictions);
  print_samples(fout, "OBJ", objPredictions);
  print_residues(fout);
}


