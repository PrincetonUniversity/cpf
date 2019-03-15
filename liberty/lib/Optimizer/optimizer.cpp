/*++
Optimize using Z3
--*/

#include<vector>
#include"z3++.h"
#include "BipartiteGraph.hpp"

void base_optimizer() {
    z3::context c;
    z3::optimize opt(c);
    z3::params p(c);
    p.set("priority",c.str_symbol("pareto")); // use Pareto optimal front
    opt.set(p);

    // Add remedies
    // initialize expression vector
    const unsigned N = 8;
    z3::expr_vector r(c);

    for (int i = 0; i < N; i++){
        std::stringstream r_name;
        r_name << "r_" << i;
        r.push_back(c.int_const(r_name.str().c_str()));
        opt.add(r[i] >= 0 && r[i] <=1);
    }
    
    // set criticism constraints
    opt.add(r[1] + r[2] + r[4] >= 1);
    opt.add(r[0] + r[3] + r[4] >= 1);
    opt.add(r[5] + r[6] >= 1);
    opt.add(r[2] + r[3] + r[7] >= 1);

    // optimize cost
    int price[N] = {2, 4, 3, 1, 6, 8, 10, 5};
    z3::expr total_cost = c.int_val(0);
    for (int i = 0; i < N; i++){
        z3::expr cost = price[i] * r[i];
        total_cost = total_cost + cost;
    }

    z3::optimize::handle h_tc = opt.minimize(total_cost);
    while (true) {
        if (z3::sat == opt.check()) {
            z3::model m = opt.get_model();
            std::cout << "Minimal Cost: " << opt.lower(h_tc) << "\n";
            std::cout << "Remediator selection: \n";
            for (int i = 0; i < N; i++)
                std::cout << "R_" << i << " and price: " << m.eval(r[i]) << " " << price[i]<< "\n";
        }
        else {
            break;
        }
    }
}


int main() {

    try {
        base_optimizer(); std::cout << "\n";
    }
    catch (z3::exception & ex) {
        std::cout << "unexpected error: " << ex << "\n";
    }
    return 0;
}
