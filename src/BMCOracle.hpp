#ifndef CADICAL_BMC_SOLVER_INTERFACE_H
#define CADICAL_BMC_SOLVER_INTERFACE_H

#include "cadical.hpp"

#include <deque>
#include <algorithm>
#include <map>

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

    bool changeInTrail = true; // checks whether the trail has changed since the last propagation step

    std::vector<std::vector<int>> new_clauses;
    std::deque<std::vector<int>> current_trail; // for each decision lvl store the assigned literals (only positive version)
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
    std::vector<int> rename_lit_loop_assignment;
    std::vector<std::map<std::string, int>> variable_by_step;
    int K = 0;
    std::string ltlspec;
    std::vector<int> edges_id;
    /////////////////////////////////////////////////////////////

    BMCOracle(CaDiCaL::Solver *s, const char *cnf_filename);
    ~BMCOracle()
    {
        solver->disconnect_external_propagator();
        printf(" problem ici ?\n");
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
        int abs_lit = std::abs(lit);
        curr_trail_sz++;
        changeInTrail = true;
        // for (size_t i = 0; i < rename_lit_loop.size(); i++)
        //     if (abs_lit == rename_lit_loop[i])
        //         rename_lit_loop_assignment[i] = lit < 0 ? -1 : 1;

        if (is_fixed)
        {
            current_trail.front().push_back(lit);
        }
        else
        {
            current_trail.back().push_back(lit);
        }
        assert(info_variables[abs_lit].property);

        // Test if it is interesting to run BMC Oracle

        if (!init_box_done)
            return;

        int result = curr_trail_sz - prev_trail_sz;
        if (result < 0)
            return;
        bool filter1 = curr_trail_sz * 100. / nb_property_variable <= 30.;
        bool filter2 = (result * 100. / nb_property_variable) >= 5.;
        bool filter3 = (result * 100. / nb_property_variable) <= 10.;

        if (filter1 && (prev_trail_sz == 0 || (filter2 && filter3)))
        {
            printf("c assigned property rate : %.2f  (with prev trail %.2f)\n", curr_trail_sz * 100. / nb_property_variable, result * 100. / nb_property_variable);
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
            // for (size_t i = 0; i < rename_lit_loop.size(); i++)
            // {
            //     if (std::find(last_lvl.begin(), last_lvl.end(), rename_lit_loop[i]) != last_lvl.end())
            //         rename_lit_loop_assignment[i] = 0;
            // }
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
        // PRINT_CURRENT_LINE
        // printf("Call: Add external clauses %d\n", (int)new_clauses.size());
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

    int cb_decide() { return 0; }
    int cb_propagate() { return 0; }
    int cb_add_reason_clause_lit(int plit)
    {
        (void)plit;
        return 0;
    };
};

#endif
