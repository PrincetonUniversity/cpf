#include <iostream>
#include <vector>

template <class T>
class BipartiteGraph {
    private:
        unsigned c_size, r_size; //numbers of remedies(rows) and criticisms(columns)
        std::vector< std::vector<T> > graph; 

    public:
        BipartiteGraph(unsigned _c_size, unsigned _r_size){
            c_size = _c_size;
            r_size = _r_size;
            graph.resize(r_size);
            for (unsigned i = 0; i < r_size; i++){
                graph[i].resize(c_size);
            }
        }

        std::vector<T> get_row(unsigned r_num) { return graph[r_num]; }

        std::vector<T> get_col(unsigned c_num){
            std::vector<T> col(r_size, false);
            for (unsigned i =0; i < r_size; i++){
                col[i] = graph[i][c_num];
            }
            return col;
        }

        unsigned get_row_size() { return r_size; }
        unsigned get_col_size() { return c_size; }
};
