/*++
  Optimize using Z3
  --*/

#include <vector>
#include "z3++.h"
#include "liberty/Optimizer/RemedCritBG.h"

namespace optimizer{
std::vector<unsigned> base_optimizer(RemedCritBG &graph, std::vector<int> &price,
                              bool enforce_exclusive ) {
  // Z3 environment
  z3::context c;
  z3::optimize opt(c);
  z3::params p(c);
  p.set("priority",c.str_symbol("pareto")); // use Pareto optimal front
  opt.set(p);

  graph.print_graph();
  // get info from map
  unsigned num_remeds = graph.get_row_size();
  unsigned num_crits = graph.get_col_size();
  assert(num_remeds == price.size()); // size must be the same

  // Add remedies into Z3
  // initialize expression vector
  z3::expr_vector remeds(c);

  for (unsigned i = 0; i < num_remeds; i++){
    std::stringstream r_name;
    r_name << "r_" << i; // start from r_0
    remeds.push_back(c.int_const(r_name.str().c_str()));
    opt.add(remeds[i] >= 0 && remeds[i] <=1); //only zero or one
  }

  // set criticism constraints
  for (unsigned cidx = 0; cidx < num_crits; cidx++){
    z3::expr crit = c.int_val(0);

    for (unsigned ridx = 0; ridx < num_remeds; ridx++){
      if (graph.get_elem(ridx, cidx))
        crit = crit + remeds[ridx];
    }
    if (enforce_exclusive)
      opt.add(crit == 1);
    else
      opt.add(crit >= 1);

  }

  // Set cost optimzer
  z3::expr total_cost = c.int_val(0);
  for (unsigned i = 0; i < num_remeds; i++){
    z3::expr cost = price[i] * remeds[i];
    total_cost = total_cost + cost;
  }
  z3::optimize::handle h_tc = opt.minimize(total_cost);

  std::vector<unsigned> selected_remed_idx;

  std::stringbuf str;
  std::ostream stream(&str);
  //while (true) {
  if (z3::sat == opt.check()) {
    z3::model m = opt.get_model();
    stream << "Minimal Cost: " << opt.lower(h_tc) << "\n";
    stream << "Remediator selection: \n";
    for (unsigned i = 0; i < num_remeds; i++){
      int select = m.eval(remeds[i]).get_numeral_int();
      stream << "R_" << i << " and price: " << select << " " << price[i]<< "\n";
      if (select == 1)
        selected_remed_idx.push_back(i);
    }
  }
  llvm::errs() << str.str();

  return selected_remed_idx;
}
} //namespace optimizer


/**

int main() {

  RemedCritBG rcbg(4,4);
  std::vector<bool> r0 = {1, 0, 1, 0};
  std::vector<bool> r1 = {0, 1, 1, 0};
  std::vector<bool> r2 = {1, 1, 1, 1};
  std::vector<bool> r3 = {1, 0, 0, 1};
  std::vector<int> price = {1,3,6,2};

  rcbg.update_one_remedy(0, r0);
  rcbg.update_one_remedy(1, r1);
  rcbg.update_one_remedy(2, r2);
  rcbg.update_one_remedy(3, r3);
  rcbg.print_graph();
  try {
    std::vector<unsigned> selectedIdx = optimizer::base_optimizer(rcbg, price, false); std::cout << "\n";
    for (auto idx : selectedIdx)
      std::cout << idx << " ";
  }
  catch (z3::exception & ex) {
    std::cout << "unexpected error: " << ex << "\n";
  }
  return 0;
}
**/
