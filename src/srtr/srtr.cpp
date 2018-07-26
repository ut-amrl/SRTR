// Copyright 2017-2018 jaholtz@cs.umass.edu
// College of Information and Computer Sciences,
// University of Massachusetts Amherst
//
//
// This software is free: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License Version 3,
// as published by the Free Software Foundation.
//
// This software is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// Version 3 in the file COPYING that came with this distribution.
// If not, see <http://www.gnu.org/licenses/>.
// ========================================================================

#include "srtr/srtr.h"
namespace srtr {

void GetParameters(context* c,
                   optimize* opt,
                   const vector<StateMachineData>& machines,
                   map<string, float>* base_parameters,
                   vector<string>* param_names,
                   map<string, expr>* epsilons,
                   map<string, expr>* absolutes,
                   vector<optimize::handle>* handles) {
  for (auto data : machines) {
    expr sum = c->real_val("sum");
    bool uninit = true;
      for (int i = 0; i < data.tuneable_params_size(); ++i) {
        MapFieldEntry entry = data.tuneable_params(i);
        if (!base_parameters->count(entry.key())) {
          (*base_parameters)[entry.key()] = entry.value();
          expr temp = c->real_const(entry.key().c_str());
          param_names->push_back(entry.key());
          expr absolute = c->real_val("absolute");
          absolute = ite(temp >= 0,
                        temp,
                        -temp);
          if (uninit) {
            sum = absolute;
            uninit = false;
          } else {
            sum = sum + absolute;
          }
          absolutes->insert(std::pair<string, expr>(entry.key(), absolute));
          epsilons->insert(std::pair<string, expr>(entry.key(), temp));
        }
      }
    }
}

double SolveWithBlocks(context* c,
             const vector<StateMachineData>& machines,
             const vector<PossibleTransition>& data,
             map<string, float>* params,
             map<string, float>* lowers) {
  // The thresholds we will tune and their epsilons
  map<string, float> base_parameters;
  map<string, expr> tuning;
  vector<string> param_names;
  vector<optimize::handle> handles;
  map<string, expr> absolutes;
  // Declare solvers
  solver solver(*c);
  optimize opt(*c);

  GetParameters(c,
                &opt,
                machines,
                &base_parameters,
                &param_names,
                &tuning,
                &absolutes,
                &handles);

  *params = base_parameters;
  // For each possible transition add a statement
  std::set<string> tuned_parameters;
  for (size_t i = 0; i < data.size(); ++i) {
    expr transition = c->bool_val("transition");
    expr neg_transition = c->bool_val("negative_transition");
    PossibleTransition data_point = data[i];
    // For each block in that transition (builds full boolean clause)
    for (auto k = 0; k < data_point.blocks_size(); ++k) {
      const bool constrained = data_point.human_constraint();
      MinuteBotsProto::TransitionBlock block = data_point.blocks(k);
      expr block_transition = c->bool_val("block_transition");
      // For each clause in that block
      for (auto j = 0; j < block.clauses_size(); ++j) {
        TransitionClause clause = block.clauses(j);
        const float lhs = clause.lhs();
        const string rhs = clause.rhs();
        if (constrained) {
          tuned_parameters.insert(rhs);
        }
        expr clause_bool = c->bool_val("case");

        // Identify which comparator is used and add the statement
        if (clause.comparator().compare(">") == 0) {
          expr temp = static_cast<int>(lhs)
              > static_cast<int>(base_parameters[rhs])
                  + tuning.at(rhs);
          clause_bool = temp;
        } else if (clause.comparator().compare("<") == 0) {
          expr temp = static_cast<int>(lhs)
              < static_cast<int>(base_parameters[rhs])
                  + tuning.at(rhs);
          clause_bool = temp;
        }  // etc for all handled comparators right now only handles ">" and "<"

        // Combine this statement with the rest of the block
        if (j == 0) {
          block_transition = clause_bool;
        } else {
          if (clause.and_()) {
            block_transition = block_transition && clause_bool;
          } else {
            block_transition = block_transition || clause_bool;
          }
        }
      }
      // Combine this block with the other blocks
      if (k == 0) {
        transition = block_transition;
      } else if (block.and_()) {
        transition = transition && block_transition;
      } else {
        transition = transition || block_transition;
      }
    }

    // Add the data point if it's a human constraint
    if (data_point.human_constraint()) {
      if (data_point.should_transition()) {
        opt.add(transition, 1);
      } else {
        opt.add(!transition, 1);
      }
    }
  }

  vector<expr> epsilon_values;
  for (auto param : tuned_parameters) {
    optimize::handle handle = opt.minimize(absolutes.at(param));
    handles.push_back(handle);
    epsilon_values.push_back(tuning.at(param));
  }

  std::cout << "Starting solver" << std::endl << std::endl;
  std::cout << "---------------------------------" << std::endl;
  std::cout << "SMT2 Representation" << std::endl;
  std::cout << "---------------------------------" << std::endl;
  std::cout << opt;
  std::cout << "---------------------------------" << std::endl << std::endl;
  std::cout << "---------------------------------" << std::endl;

  std::cout << "Parameter Adjustments" << std::endl;
  std::cout << "---------------------------------" << std::endl;
//   double start_time = GetMonotonicTime();
  if (sat == opt.check()) {
    for (auto param : tuned_parameters) {
      int i = 0;
      // TODO(jaholtz) param_names may need to be corrected.
      std::cout << param;
      string handle_test = opt.upper(handles[i]).get_decimal_string(5);
      string handle_test_lower = opt.lower(handles[i]).get_decimal_string(5);
      (*lowers)[param_names[i]] = std::atof(handle_test.c_str());
      std::cout << ": " <<
          opt.get_model().get_const_interp(epsilon_values[i].decl())
          << std::endl;
      i++;
    }
  }
  std::cout << "---------------------------------" << std::endl;
  std::cout << endl;
//   double end_time = GetMonotonicTime();
//   double execution_time = end_time - start_time;
  return 0;
}

// TODO(jaholtz) Make a default read and run function not for reading
// from a soccer log.


}  // namespace srtr

