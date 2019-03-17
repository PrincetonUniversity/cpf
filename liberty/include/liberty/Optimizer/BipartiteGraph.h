#include <iostream>
#include <vector>

template <class T>
class BipartiteGraph {
  protected:
    unsigned c_size, r_size; //numbers of remedies(rows) and criticisms(columns)
    std::vector< std::vector<T> > graph; 

    bool set_elem (unsigned r, unsigned c, T v){
      if (r < r_size && c < c_size){
        graph[r][c] = v;
        return true;
      }
      else
        return false;
    }

    bool set_col(unsigned c, std::vector<T> vv){
      if (vv.size() == r_size){
        for (unsigned i = 0; i < r_size; i++)
          graph[i][c] = vv;
        return true;
      }
      else
        return false;
    }

    bool set_row(unsigned r, std::vector<T> vr){
      if (vr.size() == c_size){
        graph[r] = vr;
        return true;
      }
      else 
        return false;
    }

  public:
    BipartiteGraph(int _r_size, int _c_size){
      assert(_r_size > 0);
      assert(_c_size > 0);
      r_size = (unsigned)_r_size;
      c_size = (unsigned)_c_size;
      graph.resize(r_size);
      for (unsigned i = 0; i < r_size; i++){
        graph[i].resize(c_size);
      }
    }

    BipartiteGraph(unsigned _r_size, unsigned _c_size){
      r_size = _r_size;
      c_size = _c_size;
      graph.resize(r_size);
      for (unsigned i = 0; i < r_size; i++){
        graph[i].resize(c_size);
      }
    }

    std::vector<T> get_row(unsigned r_num){
      assert(r_num < r_size);
      return graph[r_num];
    }

    std::vector<T> get_col(unsigned c_num){
      assert(c_num < c_size);

      std::vector<T> col(r_size, 0);
      for (unsigned i =0; i < r_size; i++){
        col[i] = graph[i][c_num];
      }
      return col;
    }

    T get_elem(unsigned r_num, unsigned c_num){
      return graph[r_num][c_num];
    }

    unsigned get_row_size() { return r_size; }
    unsigned get_col_size() { return c_size; }
};
