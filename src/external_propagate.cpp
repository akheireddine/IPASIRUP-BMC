#include "internal.hpp"


#include <unordered_set>

namespace CaDiCaL {

void Internal::add_observed_var (int ilit) {
  int idx = vidx (ilit);
  if (idx >= (int64_t) relevanttab.size ())
    relevanttab.resize (1 + (size_t) idx, 0);
  unsigned & ref = relevanttab[idx];
  if (ref < UINT_MAX) {
    ref++;
    LOG ("variable %d is observed %u times", idx, ref);
  } else LOG ("variable %d remains observed forever", idx);
}

void Internal::remove_observed_var (int ilit) {
  assert (fixed (ilit) || !level);
  
  if (!fixed (ilit) && level) backtrack ();
  const int idx = vidx (ilit);

  // Removing an observed var can happen only once it is ensured
  // that there is no unexplained propagation in the implication
  // graph involving this variable.
  // level == 0 ensures that there is no IG, fixed (ilit) ensures that there is
  // no missing explanation.
  
  unsigned & ref = relevanttab[idx];
  assert (fixed (ilit) || ref > 0);
  if (fixed (ilit)) ref = 0;
  else if (ref < UINT_MAX) {
    if (!--ref) {
      LOG ("variable %d is not observed anymore", idx);
    } else
      LOG ("variable %d is unobserved once but remains observed %u times",
        ilit, ref);
  } else LOG ("variable %d remains observed forever", idx);
}

bool Internal::observed (int ilit) const { return relevanttab[vidx (ilit)] > 0; }

bool Internal::is_decision (int ilit) {
  if (!level || fixed (ilit) || !val (ilit)) return false;
  
  const int idx = vidx (ilit);
  Var & v = var (idx);
#ifndef NDEBUG
    LOG (v.reason,"is_decision: i%d (current level: %d, is_fixed: %d, v.level: %d, is_external_reason: %d, v.reason: )",
      ilit, level, fixed(ilit), v.level, v.reason == external_reason);
#endif 
  if (!v.level || v.reason ) return false;
  assert(!v.reason);
  return true;
}


bool Internal::external_propagate () {
  if (level) require_mode (SEARCH);
  
  assert (!unsat);

  if (!conflict && external->propagator && !external->propagator->is_lazy) {
#ifndef NDEBUG
    LOG ("external propagation starts (decision level: %d, trail size: %zd, notified %zd)",
      level, trail.size(), notified);
#endif
    stats.ext_prop.triggered++;

    notify_assignments ();

    int elit = external->propagator->cb_propagate ();
    while (elit) {
      assert (external->is_observed[abs(elit)]);
      const int ilit = external->internalize (elit); // TODO: try e2i
      int tmp = val(ilit);
#ifndef NDEBUG
      assert (fixed(ilit) || observed(ilit));
      LOG("External propagation of e%d (i%d val: %d)",elit,ilit,tmp);
#endif
      if (!tmp) {
        // variable is not assigned, we can propagate it
        search_assign_external (ilit);
        stats.ext_prop.propagated++;

        if (unsat || conflict) break;
        propagate ();
        if (unsat || conflict) break;
        notify_assignments ();
      } else if (tmp < 0) {
        LOG("External propgation of %d is falsified under current trail",ilit);
        Clause * res = learn_external_reason_clause (ilit, elit);
#ifndef NDEBUG
        LOG(res,"reason clause of external propagation:");
#endif
        int level_before = level;
        bool trail_changed = (handle_external_clause(res) || level != level_before);
        
        if (unsat || conflict) break;

        if (trail_changed) {
          propagate ();
          if (!unsat && !conflict) break;
          notify_assignments ();
        }
      } // else (tmp > 0) -> case of satisfied literal is ignored
      elit = external->propagator->cb_propagate ();
    }

#ifndef NDEBUG
    LOG ("External propagation ends (decision level: %d, trail size: %zd, notified %zd)",
    level, trail.size(), notified);
#endif
    if (!unsat && !conflict) {
      bool has_external_clause = external->propagator->cb_has_external_clause ();

#ifndef NDEBUG     
      if (has_external_clause) LOG("New external clauses are to be added.");
      else LOG("No external clauses to be added.");
#endif

      while (has_external_clause) {
        int level_before = level;

        Clause * res = add_external_clause(false, 0);
        bool trail_changed = (handle_external_clause(res) || level != level_before);
        
        if (unsat || conflict) break;

        if (trail_changed) {
          propagate ();
          if (unsat || conflict) break;
          notify_assignments ();
        }
        has_external_clause = external->propagator->cb_has_external_clause();
      }
    }
#ifndef NDEBUG
    LOG ("External clause addition ends on decision level %d at trail size %zd (notified %zd)",
    level, trail.size(), notified);
#endif
  }

  return !conflict;
}

void Internal::move_literal_to_watch (bool other_watch) {
  if (clause.size() < 2) return;
  int i = 0;
  if (other_watch) i++;

  int highest_position = i;
  int highest_literal = clause[i];
  
  int highest_level = var (highest_literal).level;
  int highest_value = val (highest_literal);

  for (size_t j = i + 1; j < clause.size(); j++) {
    const int other = clause[j];
    const int other_level = var (other).level;
    const int other_value = val (other);

    if (other_value < 0) {
      if (highest_value >= 0) continue;
      if (other_level <= highest_level) continue;
    } else if (other_value > 0) {
      if (highest_value > 0 && other_level >= highest_level) continue;
    } else {
      if (highest_value >= 0) continue;
    }

    highest_position = j;
    highest_literal = other;
    highest_level = other_level;
    highest_value = other_value;
  
  }
#ifndef NDEBUG
    LOG("highest position: %d highest level: %d highest value: %d",highest_position,highest_level,highest_value);
#endif
  
  if (highest_position == i) return;
  if (highest_position > i) {
    std::swap(clause[i],clause[highest_position]);
  }
  
}

// Reads out from the external propagator the lemma/proapgation reason clause
// literal by literal.
// In case propagated_elit is 0, it is about an external clause via 
// 'cb_add_external_clause_lit'. Otherwise, it is about learning the reason of
// 'propagated_elit' via 'cb_add_reason_clause_lit'.
// The learned clause is simplified by the current root-level assignments (i.e.
// root-level falsified literals are removed, root satisfied clauses are skipped).
// Duplicate literals are removed, tauotologies are detected and skipped.
// It always adds the original (un-simplified) external clause to the proof as an
// input (redundant, if as_redundant is true) clause and the simplified version
// of it (except exceptions below) as a derived clause.
//
// In case the external clause, after simplifications, is satisfied, no clause is
// constructed, and the function returns 0.
// In case the external clause, after simplifications, is empty, no clause is
// constructed, unsat is set true and the function returns 0.
// In case the external clause, after simplifications, is unit, no clause is
// constructed, 'Internal::clause' has the unit literal (without 0) and the
// function returns 0.
//
// In every other cases a new clause is constructed and the pointer to it is
// returned.

Clause * Internal::add_external_clause(bool as_redundant, int propagated_elit) {
  size_t lemma_size = 0;
  size_t original_size = 0;
  bool skip = false;
  unordered_set<int> learned_levels;
  size_t unassigned = 0;
  int elit = 0;
  
  if (propagated_elit) {
#ifndef NDEBUG
    LOG (" ** add external reason of propagated lit: %d",propagated_elit);
#endif   
    elit = external->propagator->cb_add_reason_clause_lit (propagated_elit);
  } else elit = external->propagator->cb_add_external_clause_lit();
  
  // Read out the external lemma into original and simplify it into clause
  assert(clause.empty());
  assert(original.empty());

  while (elit) {
    assert (external->is_observed[abs(elit)]);
    original_size++;

    if (opts.check && (opts.checkwitness || opts.checkfailed)) {
      external->original.push_back(elit);
    }

    //TODO: double-check the consequences of internalization (tainting!)
    const int ilit = external->internalize (elit);
    original.push_back(ilit);
#ifndef NDEBUG
    assert (fixed(ilit) || observed(ilit));
    LOG(" ** elit: %d -> ilit: %d",elit,ilit);
#endif
    int tmp = marked (ilit);
    if (tmp > 0) {
      LOG (" ** removing duplicated literal %d", ilit);
      printf (" ** removing duplicated literal %d\n", ilit);
    } else if (tmp < 0) {
      LOG (" ** tautological since both %d and %d occur", -ilit, ilit);
      // printf (" ** tautological since both %d and %d occur\n", -ilit, ilit);
      if (propagated_elit) {
        LOG (" ** tautological clause was given as reason of external propagation.");
        printf (" ** tautological clause was given as reason of external propagation.\n");
      }
      skip = true;
    } else {
      tmp = fixed (ilit);
      if (tmp > 0) {
        LOG (" ** root level satisfied literal %d", ilit);
        // printf (" ** root level satisfied literal %d\n", ilit);
        if (propagated_elit){ 
          LOG ("satisfied clause was given as reason of external propagation.");
          printf ("satisfied clause was given as reason of external propagation.\n");
        }
        skip = true;
        if (propagated_elit) elit = external->propagator->cb_add_reason_clause_lit (propagated_elit);
        else elit = external->propagator->cb_add_external_clause_lit();
        continue;

      } else if (tmp < 0) {
        LOG (" ** root level falsified literal %d is ignored", ilit);
        // printf (" ** root level falsified literal %d is ignored\n", ilit);
       
        if (propagated_elit) elit = external->propagator->cb_add_reason_clause_lit (propagated_elit); 
        else elit = external->propagator->cb_add_external_clause_lit();
        continue;
      }
    
      // Mark lit to recognize tautology and duplication
      mark (ilit);
      
      tmp = val(ilit);
      if (tmp) learned_levels.insert(var (ilit).level);
      else unassigned++;

#ifndef NDEBUG
      if (tmp) {
        LOG(" ** elit: %d -> ilit: %d val: %d level: %d",elit,ilit,tmp,var (ilit).level);  
      } else {
        LOG(" ** elit: %d -> ilit: %d val: %d level: %d - unassigned",elit,ilit,tmp,var (ilit).level);
      }
#endif

      clause.push_back(ilit);
      lemma_size++;
    }
    
    if (propagated_elit) elit = external->propagator->cb_add_reason_clause_lit (propagated_elit);
    else elit = external->propagator->cb_add_external_clause_lit();
  }
  if (opts.check && (opts.checkwitness || opts.checkfailed)) {
      external->original.push_back(elit);
  }


  if (proof) proof->add_original_clause (original);

  // Clean up marks
  for (const auto & lit : clause)
    unmark (lit);

  if (skip) {
    assert (!propagated_elit);
    // TODO: handle the error-case when satisfied clause is given as reason of propagation.
    if (proof) proof->delete_clause (original);
    original.clear();
    clause.clear(); 
    return 0;
  }

  if (original_size > lemma_size) {
    external->check_learned_clause ();
    if (proof) {
        proof->add_derived_clause (clause);
        proof->delete_clause (original);
    }
  }
  original.clear();
  int glue = (int) (learned_levels.size () + unassigned);
  assert( glue <= (int) clause.size() );

  if (lemma_size > 1) {
    move_literal_to_watch(false);
    move_literal_to_watch(true);
    
#ifndef NDEBUG
    check_watched_literals ();
#endif

    Clause * res = new_clause (as_redundant, glue);
    
    assert (watching ());
    watch_clause (res);

    // printf("size %d , glue %d\n",(int)clause.size(),glue);

    // printf (" ** New external clause is constructed: %ld\n",clause.size());
    clause.clear();  
    LOG (res, " ** New external clause is constructed: ");
    cls_added++;
    return res;
  } else { 
    assert(clause.size() < 2);
    if (clause.empty()) {
      if (!original_size) VERBOSE (1, " ** empty clause is learnt from external propagator");
      else MSG (" ** falsified clause is learnt from external propagator");
      unsat = true;
      printf(" ** c UNSAT external clause !\n");
    } else {
      assert (clause.size() == 1);
      unit_cls_added++;
      LOG(clause," ** unit clause is learnt from external propagator");
    }
    return 0;
  }
  return 0;
}

/*------------------------------------------------------------------------*/
void Internal::explain_reason (int ilit, Clause * reason, int & open) {
#ifndef NDEBUG
  LOG(reason,"explain_reason %d (open: %d)",ilit,open);
#endif
  assert (reason);
  assert (reason != external_reason);
  for (const auto & other : *reason) {
    if (other == ilit) continue;
    Flags & f = flags (other);
    if (f.seen) continue;
    Var & v = var (other);
    if (!v.level) continue;
    assert (val (other) < 0);
    assert (v.level <= level);
    if (v.reason == external_reason) {
      v.reason = learn_external_reason_clause (-other);
      if (!v.reason) {
        v.level = 0;
        learn_unit_clause (-other);
      }
    }
    if (v.level && v.reason) {
      f.seen = true;
      open++;
    }
  }
}


// In case external propagation was used, the reason clauses of the relevant
// propagations must be learned lazily before/during conflict analysis.
// While conflict analysis needs to analyze only the current level, lazy
// clause learning must check every clause on every level that is backward
// reachable from the conflicting clause.
// So this explanation round is separated from the conflict analysis, thereby
// guranteeing that the flags and datastructures can be properly used later.
//
// This function must be called before the conflict analysis, in order to
// guarantee that every relevant reason clause is indeed learned already and
// to be sure that the levels of assignments are set correctly.
//
void Internal::explain_external_propagations () {
  assert (conflict);

  Clause * reason = conflict;
  int i = trail.size ();            // Start at end-of-trail
  int open = 0;                     // Seen but not explained literal
  
  explain_reason (0, reason, open); // marks conflict clause lits as seen
  std::vector<int> seen_lits;

  while (i > 0) {
    const int lit = trail[--i];
    if (!flags (lit).seen) continue;
    seen_lits.push_back(lit);
    Var & v = var (lit);
    if (!v.level) continue;
    if (v.reason) {
      open--;
      explain_reason (lit, v.reason, open);      
    }
    if (!open) break;
  }
  assert(!open);
  // Traverse now in the opposite direction (from lower to higher levels)
  // and calculate the actual assignment level for the seen assignments.
  for (auto it = seen_lits.rbegin(); it != seen_lits.rend(); ++it) {
    const int lit = *it;
    Flags & f = flags (lit);
    Var & v = var (lit);
    if (v.reason) {
      int real_level = 0;
      for (const auto & other : *v.reason) {
        if (other == lit) continue;
        int tmp = var (other).level;
        if (tmp > real_level) real_level = tmp;
      }
      if (v.level && !real_level) {
        learn_unit_clause(lit);
      }
      v.level = real_level;
    } 
    f.seen = false;
  }

#ifndef NDEBUG
  for (const auto & lit: trail) {
    assert (!flags (lit).seen);
  }
#endif
}


// Learns the reason clause of the propagation of ilit from the
// external propagator via 'add_external_clause'.
// In case of falsified propagation steps (which are actually user errors),
// if the propagated literal is already fixed to the opposite value
// externalize will not necessarily give back the original elit (but an
// equivalent one). To avoid that, in falsified propagation cases elit is
// added as a second argument.
Clause * Internal::learn_external_reason_clause (int ilit, int falsified_elit) {
  assert(external->propagator);

  assert(clause.empty());
  assert(original.empty());
  
  int elit = 0;
  if (!falsified_elit) {
    elit = externalize(ilit);
    assert(!fixed(ilit));
  } else elit = falsified_elit;
  
  Clause * res = add_external_clause(false, elit);
  
  if (res) {
    // All went well, new external clause was learned and can be returned
    LOG(res, "reason of %d is learned from external propagator: ", elit);
    assert (clause.empty());
    return res;
  }

  assert (!res);
  LOG("reason of %d is asked from external propagator: unit", elit);

  // No clause can be constructed because the reason is either a unit
  // clause or was empty (later should never happen, it is then an
  // error in the external propagator).
  assert (clause.size() < 2);

  if (clause.size () == 1) {
    if (falsified_elit) {
      // We keep the forced literal in clause[0] in that corner-case, return
      // side must clean it up.
      return 0;
    } else {
      assert (clause[0] == ilit);  
      // The reason of external propagation is a unit clause (after
      // root-level simplifications).
      // By returning 0 as reason, the literal will be learned as a new
      // root-level unit clause.
      clause.clear();
      return 0;
    }
  } else {
    assert (clause.empty ());
    LOG("empty reason clause was added (rendering the problem to unsat).");
    // Same as before, return 0, but set unsat to true, so the problem can be
    // recognized.
    assert (unsat);
    return 0;
  }
}

// This function decides what to do upon learning a new clause from the external
// propagator. The possible scenarios are:
// 1. External clause is satisfied by root-level assignment.
//    In that case nothing happens, the function returns false.
// 2. External clause is a unit clause (after root-level simplifications).
//    In that case the solver backtracks to root-level, assigns the unit and 
//    does BCP.
//    If that new unit+BCP leads to a conflict, unsat is set true and the function
//    returns true. Otherwise, the function returns true, without changing 'unsat'.
// 3. External clause is empty (after root-level simplifications).
//    In that case variable 'unsat' is set true and the function returns true.
// 4. External clause is falsified under the current trail (after root-level
//    simplifications, but not empty under the root-level assignments).
//    In that case, if chrono is off, the solver backtracks to the highest level
//    of the external clause. Variable 'conflict' is set to point to the external
//    clause and the function returns true (if did backtrack) or false (ow).
// 5. External clause propagates under the current trail (after root-level
//    simplifications, but not unit under the root-level assignments).
//    In that case, if chrono is off, the solver backtracks to the highest assigned
//    level of the external clause. Then, 'search_assign_driving' is called on the
//    unassigned literal, giving the external clause as a reason and the function
//    returns true.
// 6. External clause does not fall into any of the previous cases.
//    The external clause is learned, but nothing else happens, the function
//    returns false.
bool Internal::handle_external_clause (Clause * res) {
  bool trail_changed = false;
  if (!res) {
    if (clause.empty() && !unsat) {
      // Might be an ignored or root-satisfied clause, there is nothing to do with it
      LOG (clause,"No external clause is learned.");
      // trail did not change
      return false;
    } else {
      stats.ext_prop.lemmas++;
      assert (clause.size() < 2);
      if (clause.size() == 1) {
        LOG (clause,"External lemma is unit clause, backtrack to fix it and propagate:");
        if (level) backtrack ();
        assign_original_unit (clause[0]);
        clause.clear();
              
        if (unsat) {
          LOG ("External lemma made the formula unsatisfiable.");
          assert (conflict);
          stats.ext_prop.conflicting++;
        } else {
          stats.ext_prop.propagated++;
        }
        return true;

      } else if (clause.size() == 0) {
        // Empty/Falsified clause is learned from external propagator
        assert(unsat);
        stats.ext_prop.conflicting++;
        return false;
      }
    }
  } else if (val(res->literals[0]) < 0 && val(res->literals[1]) < 0) {
    
    if (!opts.chrono) {
      const int conflict_level = var(res->literals[0]).level;
      if (conflict_level != level) {
        backtrack (conflict_level);
        trail_changed = true;
      }
    }
    conflict = res;
    stats.ext_prop.lemmas++;
    stats.ext_prop.conflicting++;

    return trail_changed;
  } else if (!val(res->literals[0]) && val(res->literals[1]) < 0) {
    if (!opts.chrono) {
      const int last_assignment_level = var(res->literals[1]).level;
      if (last_assignment_level != level) {
        backtrack (last_assignment_level);
      }
    }
    search_assign_driving (res->literals[0], res);
    stats.ext_prop.lemmas++;
    stats.ext_prop.propagated++;
   
    return true;
  // } else if (val(res->literals[0]) > 0 && val(res->literals[1]) < 0) {
  //   TODO: evaluate effect of repropagation
    // const int last_assignment_level = var(res->literals[1]).level;
    // if (last_assignment_level < var(res->literals[0]).level) {
      // LOG("Lower (%d instead of %d) re-propagation of %d is possible due to external lemma.",last_assignment_level,res->literals[0]).level,res->literals[0]);
      // backtrack (last_assignment_level);
      // search_assign (res->literals[0], res);
      // trail_changed = true;
    // }
  } else {
    LOG(res,"Learning external clause (no propagation).");
    stats.ext_prop.lemmas++;
    
    return false;
  }

  return false;
}


// Asks the external propagator if the current solution is OK
// by calling 'cb_check_found_model (model)'.
//
// The checked model is built up after everything is restored
// from the reconstruction stack and every variable is reactivated
// and so it is not just simply the trail (i.e. it can be expensive).
//
// If the external propagator approves the model, the function
// returns true.
//
// If the propagator does not approve the model, the solver asks
// the propagator to add an external clause.
// This external clause, however, does NOT have to be falsified by
// the current model. The possible cases and reactions are described
// below.
// The possible states after that function:
// - A solution was found and accepted by the external propagator
// - A conflicting clause was learned from the external propagator
// - The empty clause was learned due to something new learned from
// the external propagator.
bool Internal::external_check_solution () {
  if (!external->propagator) return true;
  // Note: external->max_var != internal->max_var

  bool trail_changed = true;
  bool added_new_clauses = false;
  while (trail_changed || added_new_clauses) {
    notify_assignments ();
    if (!satisfied ()) break;
    trail_changed = false; // to be on the safe side
    added_new_clauses = false;
    LOG("Final check by external propagator is invoked.");
    stats.ext_prop.triggered++;
    stats.ext_prop.final_check++;
    external->reset_extended();
    external->extend ();

    std::vector<int> etrail;
    
    // Here the variables must be filtered by external->is_observed,
    // because fixed variables are internally not necessarily 
    // observed anymore.
    for (unsigned i = 1; i <= (unsigned) external->max_var; i++) {
      if (!external->is_observed[i]) continue;
      const int tmp = external->ival (i);
      if (tmp > 0) etrail.push_back(i);
      else etrail.push_back(-i);
#ifndef NDEBUG    
      bool p = external->vals[i];
      LOG("evals[%d]: %d ival(%d): %d",i,p,i,tmp);
#endif
    }

    bool is_consistent = external->propagator->cb_check_found_model(etrail);

    if (is_consistent) {
      LOG("Found solution is approved by external propagator.");
      return true;
    }

    bool has_external_clause = external->propagator->cb_has_external_clause();
    assert(has_external_clause);
    
    LOG("Found solution triggered new clauses from external propagator.");

    while (has_external_clause) {
      Clause * res = add_external_clause(false, 0);
      trail_changed = handle_external_clause(res);
      added_new_clauses = true;
      // There are many possible scenarios here:
      // - Learned conflicting clause: return to CDCL loop (conflict true)
      // - Learned conflicting unit clause that after backtrack+BCP leads to
      //   a new complete solution: force the outer loop to check the new
      //   model (trail_changed is true, but conflict&unsat are false)
      // - Learned empty clause: return to CDCL loop (unsat true)
      // - Learned a non-conflicting unit clause: Though it does not invalidate
      //   the current solution, the solver will backtrack to the root level
      //   and will repropagate it. The search will start again (saved phases
      //   hopefully make it quick), but it is needed in order to guarantee
      //   that every fixed variable is properly handled+notified (important
      //   for incremental use cases). 
      // - Otherwise: the solution is considered approved and the CDCL-loop can
      //   return with res = 10.
      
      if (unsat || conflict || trail_changed) break;
      has_external_clause = external->propagator->cb_has_external_clause();
    }
    LOG("No more external clause to add.");
    if (unsat || conflict) break;
  }

  if (!unsat && conflict) {
    const int conflict_level = var(conflict->literals[0]).level;
    if (conflict_level != level) {
      backtrack (conflict_level);
    }
  }
  
  return !conflict;
}


void Internal::notify_assignments () {
  if (!external->propagator || external->propagator->is_lazy) return;

  const size_t end_of_trail = trail.size ();
  if (notified < end_of_trail) LOG ("notify external propagator about new assignments");
  while (notified < end_of_trail) {
    int ilit = trail[notified++];
    if (fixed (ilit) || !observed (ilit)) continue; //fixed literals are notified eagerly
    int elit = externalize(ilit); //TODO: tainting!
    assert (elit);
    assert (external->observed(elit));
    external->propagator->notify_assignment(elit, false);
  }
}

void Internal::notify_decision () {
  if (!external->propagator || external->propagator->is_lazy) return;
  external->propagator->notify_new_decision_level ();
}

void Internal::notify_backtrack (size_t new_level) {
  if (!external->propagator || external->propagator->is_lazy) return;
  external->propagator->notify_backtrack (new_level);
}


int Internal::ask_decision () {
  if (!external->propagator || external->propagator->is_lazy) return 0;
  int elit = external->propagator->cb_decide ();
  if (!elit) return 0;
  LOG ("external propagator wants to proposes a decision: %d",elit);
  assert (external->is_observed[abs(elit)]);
  if (!external->is_observed[abs(elit)]) return 0;
  
  const int ilit = external->internalize (elit);
 
  assert (fixed(ilit) || observed(ilit));

  LOG("Asking external propagator for decision returned: %d (internal: %d, fixed: %d, val: %d)",
    elit,ilit,fixed(ilit),val(ilit));

  if (fixed (ilit) || val(ilit)) {
    LOG("Proposed decision variable is already assigned, falling back to internal decision.");
    return 0;
  }
  
  return ilit;
}

void Internal::check_watched_literals () {
      //new val does not support this anymore:
      //assert (val(clause[0]) >= val(clause[1]));
#ifndef NDEBUG  
      int v0 = 0;
      int v1 = 0;
      
      if (val (clause[0]) > 0) v0 = 1;
      else if  (val (clause[0]) < 0) v0 = -1;

      if (val (clause[1]) > 0) v1 = 1;
      else if  (val (clause[1]) < 0) v1 = -1;
      assert (v0 >= v1);
#endif
      if (val(clause[0]) > 0) {
        if (val(clause[1]) > 0) { // Case 1: Both literals are satisfied
          // They are ordered by lower to higher decision level
          assert (var (clause[0]).level <= var (clause[1]).level);

          // Every other literal of the clause is either
          //    - satisfied at higher level
          //    - unassigned
          //    - falsified
          for (size_t i = 2; i < clause.size (); i++)
            assert( val(clause[i]) <= 0 || (var (clause[1]).level <= var (clause[i]).level));

        } else if (val(clause[1]) == 0) { // Case 2: First satisfied, next unassigned

          // Every other literal of the clause is either
          //    - unassigned
          //    - falsified
          for (size_t i = 2; i < clause.size (); i++)
            assert( val(clause[i]) <= 0 );

        } else { // Case 3: First satisfied, next falsified -> could have been a reason of a previous propagation
          // Every other literal of the clause is falsified but at a lower decision level
          for (size_t i = 2; i < clause.size (); i++)
            assert( val(clause[i]) < 0 && (var (clause[1]).level >= var (clause[i]).level) );
        }
      } else if (val(clause[0]) == 0) {
        if (val(clause[1]) == 0) { // Case 4: Both literals are unassigned

          // Every other literal of the clause is either
          //    - unassigned
          //    - falsified
          for (size_t i = 2; i < clause.size (); i++)
            assert( val(clause[i]) <= 0 );

        } else { // Case 5: First unassigned, next falsified -> PROPAGATE
          // Every other literal of the clause is falsified but at a lower decision level
          for (size_t i = 2; i < clause.size (); i++)
            assert( val(clause[i]) < 0 && (var (clause[1]).level >= var (clause[i]).level) );
        }
      } else {
        assert (val(clause[0]) < 0 && val(clause[1]) < 0); // Case 6: Both literals are falsified

        // They are ordered by higher to lower decision level
        assert (var (clause[0]).level >= var (clause[1]).level);

        // Every other literal of the clause is falsified, but at a lower level
        for (size_t i = 2; i < clause.size (); i++)
          assert( val(clause[i]) < 0 && (var (clause[1]).level >= var (clause[i]).level));
      }
}

}