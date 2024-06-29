
#include "BMCOracle.hpp"
#include "smvHead.hpp"
#include "cadical.hpp"
#include "internal.hpp"

#include <filesystem>
#include <cinttypes>

#include <spot/twaalgos/translate.hh>
#include <spot/twaalgos/hoa.hh>
#include <spot/tl/parse.hh>
#include <spot/twa/bddprint.hh>
#include <spot/twa/formula2bdd.hh>
#include <spot/twaalgos/product.hh>
#include <spot/twaalgos/sccinfo.hh>
#include <spot/twaalgos/word.hh>
#include <spot/misc/common.hh>
#include <spot/misc/minato.hh>
#include <bddx.h>

// add formula and register propagator
BMCOracle::BMCOracle(CaDiCaL::Solver *s, const char *cnf_filename) : solver(s),
                                                                     ltl_aut(NULL), word_aut(NULL),
                                                                     K(0),
                                                                     ltlspec(""),
                                                                     max_vars(0),
                                                                     first_var(INT_MAX),
                                                                     init_box_done(false),
                                                                     num_edge_loop(-1), nb_model_variable(0), nb_property_variable(0),
                                                                     added_clauses(0), numb_call(0), time_blackbox(0)
{
    double t = (double)clock() / CLOCKS_PER_SEC;

    solver = s;
    // The root-level of the trail is always there
    current_trail.push_back(std::vector<int>());

    // solver->set("log", 1);
    // solver->set("debug", 1);

    // register propagator first
    solver->connect_external_propagator(this);

    readHeadFile(this, cnf_filename);

    init_spot();
    
    init_time = (double)clock() / CLOCKS_PER_SEC - t;
}

void BMCOracle::init_spot()
{
    /// init some attributes
    rename_lit_loop_assignment.resize(rename_lit_loop.size(), 0);
    nb_property_variable = rename_lit_loop.size();
    nb_model_variable -= nb_property_variable;
    numb_clauses.resize(4, 0);
    size_clauses.resize(4, 0);
    variable_by_step.resize(K + 1);                           // from  [0 ... K]
    spot::parsed_formula pf = spot::parse_infix_psl(ltlspec); // ltlspec initialized in smvHead.hpp

    if (pf.format_errors(std::cerr))
    {
        std::cerr << "c fomula error" << std::endl;
        exit(1);
    }

    spot::translator trans;
    trans.set_type(spot::postprocessor::Buchi);
    ltl_aut = trans.run(pf.f);

    for (unsigned var = first_var; var < info_variables.size(); var++)
    {
        varInfo vf = info_variables[var];
        if (info_variables[var].name == "")
            continue;

        variable_by_step[vf.num_step].insert(std::make_pair(vf.name, var));

        if (vf.name.find("loop") != std::string::npos)
        {
            info_variables[var].property = true; // initialized in smvHead.hpp
            /* Fixed variables (at level 0) are never modified */
            solver->add_observed_var(var); // loop's variables to observe
        }
    }
    for (int step = 0; step < K + 1; step++)
        for (spot::formula ap : ltl_aut->ap())
        {
            std::string name = ap.get_child_of({}).ap_name();
            int var = variable_by_step[step][name];
            info_variables[var].property = true;
            nb_property_variable++;
            nb_model_variable--;
            solver->add_observed_var(var); // property's variables to observe
        }
    init_box_done = true;

    enable_edge_loop_cond = enable_intersection = true;

    std::cerr << "c Number of formula states: " << ltl_aut->num_states() << "\n";
    std::cerr << "c Number synchr states: " << ltl_aut->num_states() * K << "\n";
    std::cerr << "c Number of property variables: " << nb_property_variable << "\n";
    std::cerr << "c Number of model variables: " << nb_model_variable << "\n";
}

// Create automata of the new assignement
void BMCOracle::create_alpha_language_automaton(int loop, bool same_cond_aut)
{
    if (same_cond_aut && word_aut)
    {
        word_aut->edge_storage(num_edge_loop).dst = loop;
        return;
    }

    spot::bdd_dict_ptr bdd_dict = ltl_aut->get_dict();
    bool new_aut = false;
    if (word_aut == NULL)
    {
        word_aut = spot::make_twa_graph(bdd_dict);
        word_aut->copy_ap_of(ltl_aut);
        word_aut->new_states(K);
        edges_id.resize(K, -1);
        new_aut = true;
    }
    std::vector<bdd> conditions(K + 1, bddtrue);

    for (auto level_vars : current_trail)
    {
        for (auto var_time : level_vars)
        {
            int abs_var = std::abs(var_time);
            int step = info_variables[abs_var].num_step;
            std::string var_name = info_variables[abs_var].name;

            if (var_name.find("loop") != std::string::npos)
                continue;

            // std::cout << " name var " << var_name << "(" << var_time << ") \n";

            int var = bdd_dict->has_registered_proposition(spot::formula::ap(var_name), ltl_aut);
            // std::cout << "\t -> " << var << "\n";
            if (var < 0)
            {
                printf("c Which situation ? %d, %d\n", var_time, var);
                continue;
            }

            if (var_time == 0) // Undefined variable should never happen
            {
                printf("Variable neither true nor false -> UNDEF (%d)\n", var);
                exit(1);
            }

            if (var_time > 0)
                conditions[step] &= bdd_ithvar(var);
            else
                conditions[step] &= bdd_nithvar(var);
        }
    }

    for (int step = 0; step < K; step++) // TODO : verifier K ou K+1?
    {
        if (step != K - 1)
        {
            if (new_aut)
                edges_id[step] = word_aut->new_edge(step, step + 1, conditions[step]);
            else
                word_aut->edge_storage(edges_id[step]).cond = conditions[step];
        }
        else
        {
            if (new_aut)
                num_edge_loop = word_aut->new_edge(step, loop, conditions[step]);
            else
                word_aut->edge_storage(num_edge_loop).cond = conditions[step];
            // std::cout << "\nc CONDITION "
            //   << " : " << spot::bdd_format_formula(ltl_aut->get_dict(), cond) << std::endl;
        }
    }
    // printf("\n\nMOT \n");
    // spot::print_hoa(std::cout, word_aut, "k");
}

void BMCOracle::add_trail_lits_to_clause(std::vector<int> &cls)
{
    for (auto level_vars : current_trail)
    {
        for (auto var : level_vars)
            cls.push_back(-var); // reverse assignment trail
    }
}

// ADD CLAUSE (Li -> -alpha)   ----------->   (-Li v -alpha) ---------> -rename v -alpha --------> -rename v -alpha[0] v -alpha[1]...
void BMCOracle::add_alpha_loop_clause(int loop)
{
    std::vector<int> cls_result;
    if (rename_lit_loop_assignment[loop] == 1)
        return;
    cls_result.push_back(-rename_lit_loop[loop]);
    add_trail_lits_to_clause(cls_result);
    new_clauses.push_back(cls_result);
    added_clauses++;
    //////////////////////// STATS ////////////////////////
    numb_clauses[EMPTY_INTERSECT]++;
    size_clauses[EMPTY_INTERSECT] += cls_result.size();
    //////////////////////////////////////////////////////
}

std::vector<bdd> BMCOracle::get_edges_constraints_of_loop(int loop)
{
    spot::twa_graph_ptr p = spot::product(word_aut, ltl_aut);
    spot::scc_info si(p);
    std::vector<bdd> constraints(K, bddfalse);

    if (!si.is_useful_state(p->get_init_state_number()))
        return constraints;

    auto *ps = p->get_named_prop<spot::product_states>("product-states");
    int nb_scc = si.scc_count();

    for (int scc = 0; scc < nb_scc; scc++)
    {
        if (!si.is_useful_scc(scc))
            continue;
        for (unsigned s : si.states_of(scc))
        {
            unsigned step = (*ps)[s].first;
            if (step == (unsigned)K - 1)
            {
                for (auto &e : p->out(s))
                    if (si.is_accepting_scc(si.scc_of(e.dst)) && ((*ps)[e.dst].first == (unsigned)loop))
                        constraints[step] |= e.cond;
            }
            else
            {
                for (auto &e : p->out(s))
                    if (si.is_useful_state(e.dst))
                        constraints[step] |= e.cond;
            }
        }
    }
    return constraints;
}

// ADD CLAUSE  (alpha ^ Li -> constraints[i]) ---->  -alpha v -Li v constraints[i] ---> -alpha v -renamei v  constraints[i]
void BMCOracle::add_edge_loop_clauses(bdd formula, int curr_step, int loop)
{
    if (formula == bddtrue)
        return;

    spot::minato_isop isop(!formula);
    bdd cube, b;

    // std::cout << "\nc EDGE "
    //   << " : " << spot::bdd_format_formula(ltl_aut->get_dict(), formula) << std::endl;

    while ((cube = isop.next()) != bddfalse)
    {
        bool already_sat = false;
        b = cube;
        std::vector<int> cls_result;

        if (b == bddfalse)
            return;

        // add bdd formula (constraint)
        while (b != bddtrue)
        {
            int var = bdd_var(b);
            const spot::bdd_dict::bdd_info &i = ltl_aut->get_dict()->bdd_map[var];
            int res_int = variable_by_step[curr_step][(i.f).get_child_of({}).ap_name()];
            bdd high = bdd_high(b);
            if (high == bddfalse)
                b = bdd_low(b);
            else
            {
                res_int = -res_int;
                // If bdd_low is not false, then b was not a conjunction.
                assert(bdd_low(b) == bddfalse);
                b = high;
            }
            assert(b != bddfalse);

            // Below condition is not necessary
            // bool value_sign = solver->val(solver->external->internalize(res_int)); TODO
            // if (value_sign > 0)
            // {
            //     already_sat = true;
            //     break;
            // }
            cls_result.push_back(res_int);
        }
        // Skip unuseful clause
        if (already_sat)
            continue;

        add_trail_lits_to_clause(cls_result);

        if (loop != -1)
            cls_result.push_back(-rename_lit_loop[loop]);

        new_clauses.push_back(cls_result);
        added_clauses++;

        //////////////////////// STATS ////////////////////////
        if (curr_step == K - 1)
        {
            numb_clauses[LOOP_CONST]++;
            size_clauses[LOOP_CONST] += cls_result.size();
        }
        else
        {
            numb_clauses[EDGE_CONST]++;
            size_clauses[EDGE_CONST] += cls_result.size();
        }
        //////////////////////////////////////////////////////
    }
}

// main function
void BMCOracle::evaluate_assignment()
{
    if (!init_box_done)
        return;

    double parsed_time = (double)clock() / CLOCKS_PER_SEC;
    numb_call++;
    word_aut = NULL; // recreate alpha automaton
    std::vector<bool> is_empty_inter(K, false);
    int nb_empty_inter = 0;
    std::vector<bdd> loop_constraints_aut(K, bddtrue);
    bdd loop_formula_all_aut = bddtrue;
    std::vector<std::vector<bdd>> edges_constraints_aut(K);
    std::vector<bdd> edges_formula_all_aut(K, bddtrue);
    std::vector<int> cls_result;

    for (int i = 0; i < K; i++)
    {
        if (rename_lit_loop[i] == 0)
            continue;

        if (rename_lit_loop_assignment[i] == -1)
            continue;

        create_alpha_language_automaton(i, (i != 0));

        std::vector<bdd> constraints = get_edges_constraints_of_loop(i);
        if (constraints.empty() && enable_intersection)
        {
            is_empty_inter[i] = true;
            nb_empty_inter++;
        }
        else if (!constraints.empty())
        {
            edges_constraints_aut[i] = constraints;
            for (int j = 0; j < K; j++)
            {
                if (edges_formula_all_aut[j] == bddtrue) // init j-th edge bdd
                    edges_formula_all_aut[j] = edges_constraints_aut[i][j];
                else if (edges_formula_all_aut[j] != edges_constraints_aut[i][j]) // not equal at least once on j-th edge
                    edges_formula_all_aut[j] = bddfalse;
            }
        }
    }
    // Check if bdds are the same, then combine all bdds edges / intersection
    {
        if (nb_empty_inter == K && enable_intersection)
        {
            // std::cout << "c ALL INTERSECTION VIDE " << solver->curr_trail.size() << "\n";
            cls_result.clear();
            add_trail_lits_to_clause(cls_result);
            new_clauses.push_back(cls_result);
            added_clauses++;
            //////////////////////// STATS ////////////////////////
            numb_clauses[EMPTY_INTERSECT]++;
            size_clauses[EMPTY_INTERSECT] += cls_result.size();
            //////////////////////////////////////////////////////
        }
        if (enable_edge_loop_cond)
        {
            for (int j = 0; j < K; j++)
            {
                if (edges_formula_all_aut[j] != bddtrue)
                {
                    if (edges_formula_all_aut[j] != bddfalse)
                        add_edge_loop_clauses(edges_formula_all_aut[j], j, -1);
                }
            }
        }
    }
    // Try again when bdds are not equivalent
    for (int i = 0; i < K; i++)
    {
        if (rename_lit_loop[i] == 0)
            continue;
        if (rename_lit_loop_assignment[i] == -1)
            continue;

        cls_result.clear();
        if (nb_empty_inter < K && is_empty_inter[i] && enable_intersection)
        {
            add_alpha_loop_clause(i);
        }
        if (enable_edge_loop_cond)
        {
            if (edges_constraints_aut[i].empty())
                continue;
            for (int j = 0; j < K; j++)
            {
                if (edges_constraints_aut[i][j] != bddtrue)
                    if (edges_formula_all_aut[j] == bddfalse) // bdd's are not equivalent between automata
                        add_edge_loop_clauses(edges_constraints_aut[i][j], j, i);
            }
        }
    }
    time_blackbox += ((double)clock() / CLOCKS_PER_SEC) - parsed_time;

    // printf("c Generated new clauses: %d (%ld calls)\n", (int)new_clauses.size(), numb_call);
}

void BMCOracle::printStats(double total_time)
{
    // double mem_used = memUsedPeak();
    // printf("c restarts              : %" PRIu64 "\n", solver.starts);
    // printf("c conflicts             : %-12" PRIu64 "   (%.0f /sec)\n", solver.conflicts, solver.conflicts / cpu_time);
    // printf("c decisions             : %-12" PRIu64 "   (%4.2f %% random) (%.0f /sec)  (%.0f %% decision)\n", solver.decisions,
    // (float)solver.rnd_decisions * 100 / (float)solver.decisions,
    // solver.decisions / cpu_time,
    // (float)solver.decisions * 100 / (float)solver.nVars());
    // printf("c propagations          : %-12" PRIu64 "   (%.0f /sec)\n", solver.propagations, solver.propagations / cpu_time);
    // printf("c conflict literals     : %-12" PRIu64 "   (%4.2f %% deleted)\n", solver.tot_literals, (solver.max_literals - solver.tot_literals) * 100 / (double)solver.max_literals);

    uint64_t totalGeneratedCls = accumulate(numb_clauses.begin(), numb_clauses.end(), 0);

    fprintf(stderr, "c calls                 : %-12" PRIu64 "\n", numb_call);
    fprintf(stderr, "c total clauses         : %-12" PRIu64 "\n", totalGeneratedCls);
    // fprintf(stderr, "c total unit clauses    : %-12" PRIu64 "\n", unit_clauses);
    fprintf(stderr, "c numb cls EMPTY_INTERS : %-12" PRIu64 "\n", numb_clauses[EMPTY_INTERSECT]);
    fprintf(stderr, "c numb cls LOOP_REMOVED : %-12" PRIu64 "\n", numb_clauses[LOOP_EXCLUDED]);
    fprintf(stderr, "c numb cls EDGE_CONST   : %-12" PRIu64 "\n", numb_clauses[EDGE_CONST]);
    fprintf(stderr, "c numb cls LOOP_CONST   : %-12" PRIu64 "\n", numb_clauses[LOOP_CONST]);

    if (totalGeneratedCls > 0)
    {
        // fprintf(stderr, "c avg lbd               : %g\n", lbd_clauses * 1.0 / totalGeneratedCls);
        // fprintf(stderr, "c falsified cls         : %-12" PRIu64 "   (%4.2f %%)\n", numb_clauses_falsified, numb_clauses_falsified * 100.0 / totalGeneratedCls);
        // fprintf(stderr, "c asserting cls         : %-12" PRIu64 "   (%4.2f %%)\n", numb_asserting_clauses, numb_asserting_clauses * 100.0 / totalGeneratedCls);
        fprintf(stderr, "c numb produced clauses    : %-12" PRIu64 "   (%4.2f %%)\n", added_clauses, added_clauses * 100.0 / totalGeneratedCls);
    }
    fprintf(stderr, "c avg size EMPTY_INTERS : %g\n", (numb_clauses[EMPTY_INTERSECT] > 0) ? size_clauses[EMPTY_INTERSECT] * 1.0 / numb_clauses[EMPTY_INTERSECT] : -1);
    fprintf(stderr, "c avg size LOOP_REMOVED : %g\n", (numb_clauses[LOOP_EXCLUDED] > 0) ? size_clauses[LOOP_EXCLUDED] * 1.0 / numb_clauses[LOOP_EXCLUDED] : -1);
    fprintf(stderr, "c avg size EDGE_CONST   : %g\n", (numb_clauses[EDGE_CONST] > 0) ? size_clauses[EDGE_CONST] * 1.0 / numb_clauses[EDGE_CONST] : -1);
    fprintf(stderr, "c avg size LOOP_CONST   : %g\n", (numb_clauses[LOOP_CONST] > 0) ? size_clauses[LOOP_CONST] * 1.0 / numb_clauses[LOOP_CONST] : -1);
    fprintf(stderr, "c time blackbox         : %g s\n", time_blackbox);
    fprintf(stderr, "c time init             : %g s\n", init_time);
    // fprintf(stderr, "c time synch product    : %g s\n", time_sync_product);
    fprintf(stderr, "c ----------------------------------------\n");
    // fprintf(stderr, "c Real assertives                        : %d\n", real_assertive);
    // fprintf(stderr, "c Used clauses from bbox (first time)    : %d  (%4.2f %%)\n", first_used_bbox_clauses, first_used_bbox_clauses * 100.0 / getTotalClauses());
    // fprintf(stderr, "c Max used clause from bbox              : %d \n", max_used_bbox_clause - 1);

    // if (mem_used != 0)
    // fprintf(stderr, "c Memory used           : %.2f MB\n", mem_used);
    fprintf(stderr, "c Real time             : %g s\n", total_time - time_blackbox);
}