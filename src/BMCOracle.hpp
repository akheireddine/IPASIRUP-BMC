#ifndef CADICAL_BMC_SOLVER_INTERFACE_H
#define CADICAL_BMC_SOLVER_INTERFACE_H

#include "cadical.hpp"

#include <deque>
#include <algorithm>
#include <map>
#include <unordered_set>

#include <spot/twa/twagraph.hh>

typedef struct varinfo
{
    int num_step = -1;
    bool property = false;
    std::string name = "";
} varInfo;

enum CONSTYPE
{
    EMPTY_INTERSECT = 0,
    LOOP_EXCLUDED = 1,
    EDGE_CONST = 2,
    LOOP_CONST = 3
};

class BMCOracle : public CaDiCaL::ExternalPropagator
{
public:
    CaDiCaL::Solver *solver;

    std::vector<std::vector<int>> new_clauses;
    std::deque<std::vector<int>> current_trail;    // for each decision lvl store the assigned literals (only positive version)
    std::map<std::string, int> assignment_counter; // for each variable name, set the number of assigned variable in the different K steps
    std::unordered_set<std::string> ap_to_rm;
    std::map<std::string, std::unordered_set<int>> unassigned_vars;
    ///////////////////////////////////////

    //////////////////////// INPUT-PARAMETERS ////////////////////////
    bool enable_intersection = true, enable_edge_loop_cond = true;
    /////////////////////////////////////////////////////////////////

    spot::twa_graph_ptr ltl_aut = NULL;
    spot::twa_graph_ptr word_aut = NULL;

    bool init_box_done = false;

    int num_edge_loop = 0;

    //////////////////////// STATS ////////////////////////
    std::vector<uint64_t> numb_clauses, size_clauses;
    uint64_t numb_call = 0, added_clauses = 0;
    double time_blackbox = 0.0, init_time = 0.0;
    //////////////////////////////////////////////////////

    //////////////////// HEAD CNF INFORMATION ////////////////////
    int max_vars = 0, first_var = INT_MAX;
    int nb_property_variable = 0, nb_model_variable = 0;
    std::vector<varInfo> info_variables;
    std::vector<int> rename_lit_loop;
    std::vector<std::map<std::string, int>> variable_by_step;
    int K = 0;
    std::string ltlspec;
    std::vector<int> edges_id;
    /////////////////////////////////////////////////////////////

    BMCOracle(CaDiCaL::Solver *s, const char *cnf_filename);
    ~BMCOracle()
    {
        solver->disconnect_external_propagator();
    }

    ////////////////////  BMC-SPOT  ////////////////////
    void init_spot();
    void create_alpha_language_automaton(int loop, bool same_cond_aut);
    void add_trail_lits_to_clause(std::vector<int> &cls);
    void add_alpha_loop_clause(int loop);
    std::vector<bdd> get_edges_constraints_of_loop(int loop);
    void add_edge_loop_clauses(bdd formula, int curr_step, int loop);
    void evaluate_assignment();
    void printStats(double total_time);
    ////////////////////////////////////////////////////

    int prev_trail_sz = 0;
    int curr_trail_sz = 0;

    static bool has_lit(const std::deque<std::vector<int>> &curr_trail, int lit)
    {
        for (auto level_lits : curr_trail)
        {
            if (std::find(level_lits.begin(), level_lits.end(), lit) != level_lits.end())
            {
                return true;
            }
        }
        return false;
    }
    // paramter lit is an elit
    void notify_assignment(int lit, bool is_fixed)
    {
        unsigned abs_var = std::abs(lit);
        std::string var_name = info_variables[abs_var].name;
        curr_trail_sz++;

        unassigned_vars[var_name].erase(abs_var);

        if (is_fixed && current_trail.size() == 1) // only on root level
        {
            assignment_counter[var_name] += 1;
            if (assignment_counter[var_name] == K)
            {
                std::cout << "c " << var_name << " is already assigned in root level." << std::endl;
                ap_to_rm.insert(var_name);
                info_variables[abs_var].property = false;
                nb_property_variable -= K;
                nb_model_variable += K;
                return;
            }
        }

        // if (is_fixed && lit < 0) // literal at root level that is false and will be true in oracle's clauses
        //     return;

        if (is_fixed)
        {
            current_trail.front().push_back(lit);
        }
        else
        {
            current_trail.back().push_back(lit);
        }
        assert(info_variables[abs_var].property);

        // Test if it is interesting to run BMC Oracle

        if (!init_box_done)
            return;

        if (ap_to_rm.size() > 0)
        {
            std::cout << "** Before AP size : " << ltl_aut->ap().size() << std::endl;
            for (auto ap : ap_to_rm)
            {
                spot::bdd_dict_ptr bdd_dict = ltl_aut->get_dict();
                int num_bdd = bdd_dict->has_registered_proposition(spot::formula::ap(ap), ltl_aut);
                ltl_aut->unregister_ap(num_bdd);
            }
            ap_to_rm.clear();
            ltl_aut->purge_dead_states();
            ltl_aut->remove_unused_ap();
            std::cout << "** Update AP size : " << ltl_aut->ap().size() << std::endl;
        }

        int result = curr_trail_sz - prev_trail_sz;
        if (result < 0)
            return;
        bool filter1 = curr_trail_sz * 100. / nb_property_variable <= 30.;
        bool filter2 = (result * 100. / nb_property_variable) >= 5.;
        bool filter3 = (result * 100. / nb_property_variable) <= 10.;

        if (filter1 && (prev_trail_sz == 0 || (filter2 && filter3)))
        {
            // printf("c assigned property rate : %.2f  (with prev trail %.2f)\n", curr_trail_sz * 100. / nb_property_variable, result * 100. / nb_property_variable);
            evaluate_assignment();
            prev_trail_sz = curr_trail_sz;
        }
    }

    void notify_new_decision_level()
    {
        current_trail.push_back(std::vector<int>());
    }

    void notify_backtrack(size_t new_level)
    {
        while (current_trail.size() > new_level + 1)
        {
            auto last_lvl = current_trail.back();
            curr_trail_sz -= last_lvl.size();

            for (auto v : last_lvl)
            {
                std::string var_name = info_variables[std::abs(v)].name;
                unassigned_vars[var_name].insert(std::abs(v));
            }

            current_trail.pop_back();
        }
    }

    // currently not checked in propagator but with the normal incremental interface to allow adding other literals or even new once.
    bool cb_check_found_model(const std::vector<int> &model)
    {
        (void)model;
        return true;
    }

    bool cb_has_external_clause()
    {
        return (!new_clauses.empty());
    }

    int cb_add_external_clause_lit()
    {
        if (new_clauses.empty())
            return 0;

        std::vector<int> &lastClause = new_clauses[new_clauses.size() - 1];
        // end of clause
        if (lastClause.empty())
        {
            new_clauses.pop_back(); // delete last clause
            return 0;
        }
        else // still read last clause
        {
            int lit = lastClause.back();
            lastClause.pop_back();
            return lit;
        }
    }

    // Decide on the next AP that has few assignments left
    int cb_decide()
    {
        if (unassigned_vars.empty())
            return 0;

        std::string min_unassigned_varname = unassigned_vars.begin()->first;

        for (auto const &[name, val] : unassigned_vars)
            if (unassigned_vars[name].size() < unassigned_vars[min_unassigned_varname].size())
                min_unassigned_varname = name;

        return *(unassigned_vars[min_unassigned_varname].begin());
    }
    int cb_propagate() { return 0; }
    int cb_add_reason_clause_lit(int plit)
    {
        (void)plit;
        return 0;
    };
};

#endif
