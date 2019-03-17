#include "BipartiteGraph.h"

class RemedCritBG : public BipartiteGraph<bool> {
  public:
    RemedCritBG(int r, int c) : BipartiteGraph(r, c) {}
    RemedCritBG(unsigned r, unsigned c): BipartiteGraph(r, c){}
    bool update_one_remedy(unsigned r_idx, std::vector<bool> remedy_line){
     return set_row(r_idx, remedy_line);
    }

    void print_graph(){

      llvm::errs() << "R/C" << "\t";
      for (unsigned c = 0; c < c_size; c++){
        llvm::errs() << c << "\t";
      }

      llvm::errs() << "\n";
         
      for (unsigned r = 0; r < r_size; r++){
        llvm::errs() <<  r << "\t";
        for (unsigned c = 0; c < c_size; c++){
          llvm::errs() << graph[r][c] << "\t";
        }
        llvm::errs() << "\n";
      }
    }
};
