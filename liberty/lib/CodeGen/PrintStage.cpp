#include "llvm/Support/FileSystem.h"
#include "liberty/CodeGen/PrintStage.h"

namespace liberty
{
namespace SpecPriv
{
using namespace llvm;

  static void stageCFGtoDOT(raw_ostream &fout, Loop *loop,
    const BBSet &rel, const ISet &insts,
    const PreparedStrategy::ProduceTo &prod, const PreparedStrategy::ConsumeFrom &cons,
    unsigned stageno, StringRef type)
  {
    BasicBlock *header = loop->getHeader();
    Function *fcn = header->getParent();

    fout << "digraph \"" << fcn->getName() << " :: " << header->getName()
         << ", Stage " << stageno << ' ' << type << "\" {\n";
    for(Function::iterator i=fcn->begin(), e=fcn->end(); i!=e; ++i)
    {
      BasicBlock *bb = &*i;

      if( ! rel.count(bb) )
      {
        fout << '\"' << bb->getName() << "\" [label=\"" << bb->getName() << "\",shape=box,style=filled,color=blue];\n";
      }
      else
      {
        fout << '\"' << bb->getName() << "\" [label=<" << bb->getName() << ":<BR ALIGN=\"LEFT\"/>";

        for(BasicBlock::iterator i=bb->begin(), e=bb->end(); i!=e; ++i)
        {
          Instruction *inst = &*i;
          std::string line;
          {
            raw_string_ostream lout(line);
            lout << *i;
          }

          // Color code each instruction:
          //  - (green) This instruction will appear in this stage,
          //            and will be produced to later stages.
          //  - (black) This instruction will appear in this stage,
          //            but will not be produced.
          //  - (red)   This instruction will not appear in this stage,
          //            but will be consumed in this stage.
          //  - (blue)  This instruction will not appear in this stage,
          //            and will not be consumed.

          //StringRef color = 0;
          StringRef color = "";
          if( insts.count(inst) )
          {
            // (green) or (black)
            if( prod.count(inst) )
              color = "green";
            else
              color = "black";
          }
          else
          {
            if( cons.count(inst) )
              color = "red";
            else
              color = "blue";
          }

          fout << "  <FONT COLOR=\"" << color << "\">";

          // I will take this moment to again mention
          // that the C++ standard library has the worst
          // implementation of a string that I've ever seen.
          // In a real implementation, I could type line.replace('<', "&lt;"), etc
          for(unsigned i=0, N=line.size(); i<N; ++i)
            if( line[i] == '<' )
              fout << "&lt;";
            else if( line[i] == '>' )
              fout << "&gt;";
            else if( line[i] == '&' )
              fout << "&amp;";
            else if( line[i] == '\n' )
              fout << "<BR ALIGN=\"LEFT\"/>";
            else
              fout << line[i];

          fout << "</FONT><BR ALIGN=\"LEFT\"/>";
        }

        fout << ">,shape=box];\n";
      }

      const TerminatorInst *term = bb->getTerminator();
      for(unsigned sn=0, N=term->getNumSuccessors(); sn<N; ++sn)
      {
        const BasicBlock *dest = term->getSuccessor(sn);
        fout << '\"' << bb->getName() << "\" -> \"" << dest->getName() << "\";";
      }
    }
    fout << "}\n";
  }

  void writeStageCFG(Loop *loop, unsigned stageno, StringRef type,
    const BBSet &rel, const ISet &insts,
    const PreparedStrategy::ProduceTo &prod, const PreparedStrategy::ConsumeFrom &cons)
  {
    BasicBlock *header = loop->getHeader();
    Function *fcn = header->getParent();

    std::string filename;
    raw_string_ostream fnout(filename);
    fnout << "stage." << stageno << '.' << type << "--"
          << fcn->getName() << "--" << header->getName() << ".dot";
    fnout.flush();

    //std::string error;
    std::error_code ec;
    raw_fd_ostream fout(filename.c_str(), ec, sys::fs::F_RW);
    stageCFGtoDOT(fout, loop, rel, insts, prod, cons, stageno, type);
  }

}
}
