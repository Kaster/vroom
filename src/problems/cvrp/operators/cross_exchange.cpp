/*

This file is part of VROOM.

Copyright (c) 2015-2019, Julien Coupey.
All rights reserved (see LICENSE).

*/

#include "problems/cvrp/operators/cross_exchange.h"

namespace vroom {
namespace cvrp {

CrossExchange::CrossExchange(const Input& input,
                             const utils::SolutionState& sol_state,
                             RawRoute& s_route,
                             Index s_vehicle,
                             Index s_rank,
                             RawRoute& t_route,
                             Index t_vehicle,
                             Index t_rank)
  : Operator(input,
             sol_state,
             s_route,
             s_vehicle,
             s_rank,
             t_route,
             t_vehicle,
             t_rank),
    _gain_upper_bound_computed(false),
    reverse_s_edge(false),
    reverse_t_edge(false),
    s_is_normal_valid(false),
    s_is_reverse_valid(false),
    t_is_normal_valid(false),
    t_is_reverse_valid(false) {
  assert(s_vehicle != t_vehicle);
  assert(s_route.size() >= 2);
  assert(t_route.size() >= 2);
  assert(s_rank < s_route.size() - 1);
  assert(t_rank < t_route.size() - 1);
}

Gain CrossExchange::gain_upper_bound() {
  const auto& m = _input.get_matrix();
  const auto& v_source = _input.vehicles[s_vehicle];
  const auto& v_target = _input.vehicles[t_vehicle];

  // For source vehicle, we consider the cost of replacing edge
  // starting at rank s_rank with target edge. Part of that cost
  // (for adjacent edges) is stored in
  // _sol_state.edge_costs_around_edge.  reverse_* checks whether we
  // should change the target edge order.
  Index s_index = _input.jobs[s_route[s_rank]].index();
  Index s_after_index = _input.jobs[s_route[s_rank + 1]].index();
  Index t_index = _input.jobs[t_route[t_rank]].index();
  Index t_after_index = _input.jobs[t_route[t_rank + 1]].index();

  // Determine costs added with target edge.
  Gain previous_cost = 0;
  Gain next_cost = 0;
  Gain reverse_previous_cost = 0;
  Gain reverse_next_cost = 0;

  if (s_rank == 0) {
    if (v_source.has_start()) {
      auto p_index = v_source.start.get().index();
      previous_cost = m[p_index][t_index];
      reverse_previous_cost = m[p_index][t_after_index];
    }
  } else {
    auto p_index = _input.jobs[s_route[s_rank - 1]].index();
    previous_cost = m[p_index][t_index];
    reverse_previous_cost = m[p_index][t_after_index];
  }

  if (s_rank == s_route.size() - 2) {
    if (v_source.has_end()) {
      auto n_index = v_source.end.get().index();
      next_cost = m[t_after_index][n_index];
      reverse_next_cost = m[t_index][n_index];
    }
  } else {
    auto n_index = _input.jobs[s_route[s_rank + 2]].index();
    next_cost = m[t_after_index][n_index];
    reverse_next_cost = m[t_index][n_index];
  }

  _normal_s_gain = _sol_state.edge_costs_around_edge[s_vehicle][s_rank] -
                   previous_cost - next_cost;

  Gain reverse_edge_cost = static_cast<Gain>(m[t_index][t_after_index]) -
                           static_cast<Gain>(m[t_after_index][t_index]);
  _reversed_s_gain = _sol_state.edge_costs_around_edge[s_vehicle][s_rank] +
                     reverse_edge_cost - reverse_previous_cost -
                     reverse_next_cost;

  // For target vehicle, we consider the cost of replacing edge
  // starting at rank t_rank with source edge. Part of that cost (for
  // adjacent edges) is stored in _sol_state.edge_costs_around_edge.
  // reverse_* checks whether we should change the source edge order.
  previous_cost = 0;
  next_cost = 0;
  reverse_previous_cost = 0;
  reverse_next_cost = 0;

  if (t_rank == 0) {
    if (v_target.has_start()) {
      auto p_index = v_target.start.get().index();
      previous_cost = m[p_index][s_index];
      reverse_previous_cost = m[p_index][s_after_index];
    }
  } else {
    auto p_index = _input.jobs[t_route[t_rank - 1]].index();
    previous_cost = m[p_index][s_index];
    reverse_previous_cost = m[p_index][s_after_index];
  }

  if (t_rank == t_route.size() - 2) {
    if (v_target.has_end()) {
      auto n_index = v_target.end.get().index();
      next_cost = m[s_after_index][n_index];
      reverse_next_cost = m[s_index][n_index];
    }
  } else {
    auto n_index = _input.jobs[t_route[t_rank + 2]].index();
    next_cost = m[s_after_index][n_index];
    reverse_next_cost = m[s_index][n_index];
  }

  _normal_t_gain = _sol_state.edge_costs_around_edge[t_vehicle][t_rank] -
                   previous_cost - next_cost;

  reverse_edge_cost = static_cast<Gain>(m[s_index][s_after_index]) -
                      static_cast<Gain>(m[s_after_index][s_index]);
  _reversed_t_gain = _sol_state.edge_costs_around_edge[t_vehicle][t_rank] +
                     reverse_edge_cost - reverse_previous_cost -
                     reverse_next_cost;

  _gain_upper_bound_computed = true;

  return std::max(_normal_s_gain, _reversed_s_gain) +
         std::max(_normal_t_gain, _reversed_t_gain);
}

void CrossExchange::compute_gain() {
  assert(_gain_upper_bound_computed);
  assert(s_is_normal_valid or s_is_reverse_valid);
  if (_reversed_s_gain > _normal_s_gain) {
    // Biggest potential gain is obtained when reversing edge.
    if (s_is_reverse_valid) {
      stored_gain += _reversed_s_gain;
      reverse_t_edge = true;
    } else {
      stored_gain += _normal_s_gain;
    }
  } else {
    // Biggest potential gain is obtained when not reversing edge.
    if (s_is_normal_valid) {
      stored_gain += _normal_s_gain;
    } else {
      stored_gain += _reversed_s_gain;
      reverse_t_edge = true;
    }
  }

  assert(t_is_normal_valid or t_is_reverse_valid);
  if (_reversed_t_gain > _normal_t_gain) {
    // Biggest potential gain is obtained when reversing edge.
    if (t_is_reverse_valid) {
      stored_gain += _reversed_t_gain;
      reverse_s_edge = true;
    } else {
      stored_gain += _normal_t_gain;
    }
  } else {
    // Biggest potential gain is obtained when not reversing edge.
    if (t_is_normal_valid) {
      stored_gain += _normal_t_gain;
    } else {
      stored_gain += _reversed_t_gain;
      reverse_s_edge = true;
    }
  }

  gain_computed = true;
}

bool CrossExchange::is_valid() {
  auto s_current_job_rank = s_route[s_rank];
  auto t_current_job_rank = t_route[t_rank];
  auto s_after_job_rank = s_route[s_rank + 1];
  auto t_after_job_rank = t_route[t_rank + 1];

  bool valid = _input.vehicle_ok_with_job(t_vehicle, s_current_job_rank);
  valid &= _input.vehicle_ok_with_job(t_vehicle, s_after_job_rank);
  valid &= _input.vehicle_ok_with_job(s_vehicle, t_current_job_rank);
  valid &= _input.vehicle_ok_with_job(s_vehicle, t_after_job_rank);

  auto target_pickup = _input.jobs[t_current_job_rank].pickup +
                       _input.jobs[t_after_job_rank].pickup;
  auto target_delivery = _input.jobs[t_current_job_rank].delivery +
                         _input.jobs[t_after_job_rank].delivery;
  valid &= source.is_valid_addition_for_capacity_margins(_input,
                                                         target_pickup,
                                                         target_delivery,
                                                         s_rank,
                                                         s_rank + 2);

  if (valid) {
    // Keep target edge direction when inserting in source route.
    auto t_start = t_route.begin() + t_rank;
    s_is_normal_valid =
      source.is_valid_addition_for_capacity_inclusion(_input,
                                                      target_delivery,
                                                      t_start,
                                                      t_start + 2,
                                                      s_rank,
                                                      s_rank + 2);
    // Reverse target edge direction when inserting in source route.
    auto t_reverse_start = t_route.rbegin() + t_route.size() - 2 - t_rank;
    s_is_reverse_valid =
      source.is_valid_addition_for_capacity_inclusion(_input,
                                                      target_delivery,
                                                      t_reverse_start,
                                                      t_reverse_start + 2,
                                                      s_rank,
                                                      s_rank + 2);

    valid = s_is_normal_valid or s_is_reverse_valid;
  }

  auto source_pickup = _input.jobs[s_current_job_rank].pickup +
                       _input.jobs[s_after_job_rank].pickup;
  auto source_delivery = _input.jobs[s_current_job_rank].delivery +
                         _input.jobs[s_after_job_rank].delivery;

  valid &= target.is_valid_addition_for_capacity_margins(_input,
                                                         source_pickup,
                                                         source_delivery,
                                                         t_rank,
                                                         t_rank + 2);

  if (valid) {

    // Keep source edge direction when inserting in target route.
    auto s_start = s_route.begin() + s_rank;
    t_is_normal_valid =
      target.is_valid_addition_for_capacity_inclusion(_input,
                                                      source_delivery,
                                                      s_start,
                                                      s_start + 2,
                                                      t_rank,
                                                      t_rank + 2);

    // Reverse source edge direction when inserting in target route.
    auto s_reverse_start = s_route.rbegin() + s_route.size() - 2 - s_rank;
    t_is_reverse_valid =
      target.is_valid_addition_for_capacity_inclusion(_input,
                                                      source_delivery,
                                                      s_reverse_start,
                                                      s_reverse_start + 2,
                                                      t_rank,
                                                      t_rank + 2);
    valid = t_is_normal_valid or t_is_reverse_valid;
  }

  return valid;
}

void CrossExchange::apply() {
  std::swap(s_route[s_rank], t_route[t_rank]);
  std::swap(s_route[s_rank + 1], t_route[t_rank + 1]);

  if (reverse_s_edge) {
    std::swap(t_route[t_rank], t_route[t_rank + 1]);
  }
  if (reverse_t_edge) {
    std::swap(s_route[s_rank], s_route[s_rank + 1]);
  }
}

std::vector<Index> CrossExchange::addition_candidates() const {
  return {s_vehicle, t_vehicle};
}

std::vector<Index> CrossExchange::update_candidates() const {
  return {s_vehicle, t_vehicle};
}

} // namespace cvrp
} // namespace vroom
