






#include <iostream>
#include <spot/tl/parse.hh>
#include <spot/twaalgos/translate.hh>
#include <spot/twaalgos/neverclaim.hh>

int main()
{
  spot::parsed_formula pf = spot::parse_infix_psl("GFa -> GFb");
  if (pf.format_errors(std::cerr))
    return 1;
  spot::translator trans;
  trans.set_type(spot::postprocessor::Buchi);
  trans.set_pref(spot::postprocessor::SBAcc
                 | spot::postprocessor::Small);
  spot::twa_graph_ptr aut = trans.run(pf.f);
  print_never_claim(std::cout, aut) << '\n';
  return 0;
}

