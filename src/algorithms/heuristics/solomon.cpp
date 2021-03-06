/*

This file is part of VROOM.

Copyright (c) 2015-2019, Julien Coupey.
All rights reserved (see LICENSE).

*/

#include <algorithm>
#include <numeric>
#include <set>

#include "algorithms/heuristics/solomon.h"
#include "structures/vroom/raw_route.h"
#include "structures/vroom/tw_route.h"
#include "utils/helpers.h"

namespace vroom {
namespace heuristics {

template <class T> T basic(const Input& input, INIT init, float lambda) {
  T routes;
  for (Index v = 0; v < input.vehicles.size(); ++v) {
    routes.emplace_back(input, v);
  }

  std::set<Index> unassigned;
  for (Index j = 0; j < input.jobs.size(); ++j) {
    unassigned.insert(j);
  }

  // One level of indirection to allow easy ordering of the vehicles
  // within the heuristic.
  std::vector<Index> vehicles_ranks(input.vehicles.size());
  std::iota(vehicles_ranks.begin(), vehicles_ranks.end(), 0);
  // Sort vehicles by "higher" capacity or by time window in case of
  // capacities ties.
  std::stable_sort(vehicles_ranks.begin(),
                   vehicles_ranks.end(),
                   [&](const auto lhs, const auto rhs) {
                     auto& v_lhs = input.vehicles[lhs];
                     auto& v_rhs = input.vehicles[rhs];
                     return v_rhs.capacity << v_lhs.capacity or
                            (v_lhs.capacity == v_rhs.capacity and
                             v_lhs.tw.length > v_rhs.tw.length);
                   });

  const auto& m = input.get_matrix();

  // costs[j] is the cost of fetching job j in an empty route from one
  // of the vehicles (consistent across vehicles in the homogeneous
  // case).
  const auto& v = input.vehicles[0];

  std::vector<Cost> costs(input.jobs.size());
  for (std::size_t j = 0; j < input.jobs.size(); ++j) {
    Index j_index = input.jobs[j].index();

    Cost current_cost = 0;
    if (v.has_start()) {
      current_cost += m[v.start.get().index()][j_index];
    }
    if (v.has_end()) {
      current_cost += m[j_index][v.end.get().index()];
    }
    costs[j] = current_cost;
  }

  for (Index v = 0; v < input.vehicles.size(); ++v) {
    auto v_rank = vehicles_ranks[v];
    auto& current_r = routes[v_rank];

    const auto& vehicle = input.vehicles[v_rank];

    if (init != INIT::NONE) {
      // Initialize current route with the "best" valid job.
      bool init_ok = false;

      Amount higher_amount(input.zero_amount());
      Cost furthest_cost = 0;
      Cost nearest_cost = std::numeric_limits<Cost>::max();
      Duration earliest_deadline = std::numeric_limits<Duration>::max();
      Index best_job_rank = 0;
      for (const auto job_rank : unassigned) {
        if (!input.vehicle_ok_with_job(v_rank, job_rank) or
            !current_r
               .is_valid_addition_for_capacity(input,
                                               input.jobs[job_rank].pickup,
                                               input.jobs[job_rank].delivery,
                                               0) or
            !current_r.is_valid_addition_for_tw(input, job_rank, 0)) {
          continue;
        }

        if (init == INIT::HIGHER_AMOUNT) {
          if (higher_amount << input.jobs[job_rank].pickup) {
            higher_amount = input.jobs[job_rank].pickup;
            best_job_rank = job_rank;
            init_ok = true;
          }
          if (higher_amount << input.jobs[job_rank].delivery) {
            higher_amount = input.jobs[job_rank].delivery;
            best_job_rank = job_rank;
            init_ok = true;
          }
        }
        if (init == INIT::EARLIEST_DEADLINE) {
          Duration current_deadline = input.jobs[job_rank].tws.back().end;
          if (current_deadline < earliest_deadline) {
            earliest_deadline = current_deadline;
            best_job_rank = job_rank;
            init_ok = true;
          }
        }
        if (init == INIT::FURTHEST and furthest_cost < costs[job_rank]) {
          furthest_cost = costs[job_rank];
          best_job_rank = job_rank;
          init_ok = true;
        }
        if (init == INIT::NEAREST and costs[job_rank] < nearest_cost) {
          nearest_cost = costs[job_rank];
          best_job_rank = job_rank;
          init_ok = true;
        }
      }
      if (init_ok) {
        current_r.add(input, best_job_rank, 0);
        current_r.update_amounts(input);
        unassigned.erase(best_job_rank);
      }
    }

    bool keep_going = true;
    while (keep_going) {
      keep_going = false;
      float best_cost = std::numeric_limits<float>::max();
      Index best_job_rank = 0;
      Index best_r = 0;

      for (const auto job_rank : unassigned) {
        if (!input.vehicle_ok_with_job(v_rank, job_rank)) {
          continue;
        }

        for (Index r = 0; r <= current_r.size(); ++r) {
          float current_add = utils::addition_cost(input,
                                                   m,
                                                   job_rank,
                                                   vehicle,
                                                   current_r.route,
                                                   r);

          float current_cost =
            current_add - lambda * static_cast<float>(costs[job_rank]);

          if (current_cost < best_cost and
              current_r
                .is_valid_addition_for_capacity(input,
                                                input.jobs[job_rank].pickup,
                                                input.jobs[job_rank].delivery,
                                                r) and
              current_r.is_valid_addition_for_tw(input, job_rank, r)) {
            best_cost = current_cost;
            best_job_rank = job_rank;
            best_r = r;
          }
        }
      }

      if (best_cost < std::numeric_limits<float>::max()) {
        current_r.add(input, best_job_rank, best_r);
        current_r.update_amounts(input);
        unassigned.erase(best_job_rank);
        keep_going = true;
      }
    }
  }

  return routes;
}

template <class T>
T dynamic_vehicle_choice(const Input& input, INIT init, float lambda) {
  T routes;
  for (Index v = 0; v < input.vehicles.size(); ++v) {
    routes.emplace_back(input, v);
  }

  std::set<Index> unassigned;
  for (Index j = 0; j < input.jobs.size(); ++j) {
    unassigned.insert(j);
  }

  std::vector<Index> vehicles_ranks(input.vehicles.size());
  std::iota(vehicles_ranks.begin(), vehicles_ranks.end(), 0);

  const auto& m = input.get_matrix();

  // costs[j][v] is the cost of fetching job j in an empty route from
  // vehicle at vehicles_ranks[v].
  std::vector<std::vector<Cost>> costs(input.jobs.size(),
                                       std::vector<Cost>(
                                         input.vehicles.size()));
  for (std::size_t j = 0; j < input.jobs.size(); ++j) {
    Index j_index = input.jobs[j].index();

    for (std::size_t v = 0; v < vehicles_ranks.size(); ++v) {
      const auto& vehicle = input.vehicles[vehicles_ranks[v]];
      Cost current_cost = 0;
      if (vehicle.has_start()) {
        current_cost += m[vehicle.start.get().index()][j_index];
      }
      if (vehicle.has_end()) {
        current_cost += m[j_index][vehicle.end.get().index()];
      }
      costs[j][v] = current_cost;
    }
  }

  while (!vehicles_ranks.empty() and !unassigned.empty()) {
    // For any unassigned job at j, jobs_min_costs[j]
    // (resp. jobs_second_min_costs[j]) holds the min cost
    // (resp. second min cost) of picking the job in an empty route
    // for any remaining vehicle.
    std::vector<Cost> jobs_min_costs(input.jobs.size(),
                                     std::numeric_limits<Cost>::max());
    std::vector<Cost> jobs_second_min_costs(input.jobs.size(),
                                            std::numeric_limits<Cost>::max());
    for (const auto job_rank : unassigned) {
      for (const auto v_rank : vehicles_ranks) {
        if (costs[job_rank][v_rank] <= jobs_min_costs[job_rank]) {
          jobs_second_min_costs[job_rank] = jobs_min_costs[job_rank];
          jobs_min_costs[job_rank] = costs[job_rank][v_rank];
        } else {
          if (costs[job_rank][v_rank] < jobs_second_min_costs[job_rank]) {
            jobs_second_min_costs[job_rank] = costs[job_rank][v_rank];
          }
        }
      }
    }

    // Pick vehicle that has the biggest number of compatible jobs
    // closest to him than to any other different vehicle.
    std::vector<unsigned> closest_jobs_count(input.vehicles.size(), 0);
    for (const auto job_rank : unassigned) {
      for (const auto v_rank : vehicles_ranks) {
        if (costs[job_rank][v_rank] == jobs_min_costs[job_rank]) {
          ++closest_jobs_count[v_rank];
        }
      }
    }

    const auto chosen_vehicle =
      std::min_element(vehicles_ranks.begin(),
                       vehicles_ranks.end(),
                       [&](const auto lhs, const auto rhs) {
                         auto& v_lhs = input.vehicles[lhs];
                         auto& v_rhs = input.vehicles[rhs];
                         return closest_jobs_count[lhs] >
                                  closest_jobs_count[rhs] or
                                (closest_jobs_count[lhs] ==
                                   closest_jobs_count[rhs] and
                                 (v_rhs.capacity << v_lhs.capacity or
                                  (v_lhs.capacity == v_rhs.capacity and
                                   v_lhs.tw.length > v_rhs.tw.length)));
                       });
    auto v_rank = *chosen_vehicle;
    vehicles_ranks.erase(chosen_vehicle);

    // Once current vehicle is decided, regrets[j] holds the min cost
    // of picking the job in an empty route for other remaining
    // vehicles.
    std::vector<Cost> regrets(input.jobs.size(),
                              std::numeric_limits<Cost>::max());
    for (const auto job_rank : unassigned) {
      if (jobs_min_costs[job_rank] < costs[job_rank][v_rank]) {
        regrets[job_rank] = jobs_min_costs[job_rank];
      } else {
        regrets[job_rank] = jobs_second_min_costs[job_rank];
      }
    }

    const auto& vehicle = input.vehicles[v_rank];
    auto& current_r = routes[v_rank];

    if (init != INIT::NONE) {
      // Initialize current route with the "best" valid job that is
      //  closest for current vehicle than to any other remaining
      //  vehicle.
      bool init_ok = false;

      Amount higher_amount(input.zero_amount());
      Cost furthest_cost = 0;
      Cost nearest_cost = std::numeric_limits<Cost>::max();
      Duration earliest_deadline = std::numeric_limits<Duration>::max();
      Index best_job_rank = 0;
      for (const auto job_rank : unassigned) {
        if (jobs_min_costs[job_rank] < costs[job_rank][v_rank] or
            // One of the remaining vehicles is closest to that job.
            !input.vehicle_ok_with_job(v_rank, job_rank) or
            !current_r
               .is_valid_addition_for_capacity(input,
                                               input.jobs[job_rank].pickup,
                                               input.jobs[job_rank].delivery,
                                               0) or
            !current_r.is_valid_addition_for_tw(input, job_rank, 0)) {
          continue;
        }

        if (init == INIT::HIGHER_AMOUNT) {
          if (higher_amount << input.jobs[job_rank].pickup) {
            higher_amount = input.jobs[job_rank].pickup;
            best_job_rank = job_rank;
            init_ok = true;
          }
          if (higher_amount << input.jobs[job_rank].delivery) {
            higher_amount = input.jobs[job_rank].delivery;
            best_job_rank = job_rank;
            init_ok = true;
          }
        }
        if (init == INIT::EARLIEST_DEADLINE) {
          Duration current_deadline = input.jobs[job_rank].tws.back().end;
          if (current_deadline < earliest_deadline) {
            earliest_deadline = current_deadline;
            best_job_rank = job_rank;
            init_ok = true;
          }
        }
        if (init == INIT::FURTHEST and
            furthest_cost < costs[job_rank][v_rank]) {
          furthest_cost = costs[job_rank][v_rank];
          best_job_rank = job_rank;
          init_ok = true;
        }
        if (init == INIT::NEAREST and costs[job_rank][v_rank] < nearest_cost) {
          nearest_cost = costs[job_rank][v_rank];
          best_job_rank = job_rank;
          init_ok = true;
        }
      }
      if (init_ok) {
        current_r.add(input, best_job_rank, 0);
        current_r.update_amounts(input);
        unassigned.erase(best_job_rank);
      }
    }

    bool keep_going = true;
    while (keep_going) {
      keep_going = false;
      float best_cost = std::numeric_limits<float>::max();
      Index best_job_rank = 0;
      Index best_r = 0;

      for (const auto job_rank : unassigned) {
        if (!input.vehicle_ok_with_job(v_rank, job_rank)) {
          continue;
        }

        for (Index r = 0; r <= current_r.size(); ++r) {
          float current_add = utils::addition_cost(input,
                                                   m,
                                                   job_rank,
                                                   vehicle,
                                                   current_r.route,
                                                   r);

          float current_cost =
            current_add - lambda * static_cast<float>(regrets[job_rank]);

          if (current_cost < best_cost and
              current_r
                .is_valid_addition_for_capacity(input,
                                                input.jobs[job_rank].pickup,
                                                input.jobs[job_rank].delivery,
                                                r) and
              current_r.is_valid_addition_for_tw(input, job_rank, r)) {
            best_cost = current_cost;
            best_job_rank = job_rank;
            best_r = r;
          }
        }
      }

      if (best_cost < std::numeric_limits<float>::max()) {
        current_r.add(input, best_job_rank, best_r);
        current_r.update_amounts(input);
        unassigned.erase(best_job_rank);
        keep_going = true;
      }
    }
  }

  return routes;
}

using RawSolution = std::vector<RawRoute>;
using TWSolution = std::vector<TWRoute>;

template RawSolution basic(const Input& input, INIT init, float lambda);

template RawSolution dynamic_vehicle_choice(const Input& input,
                                            INIT init,
                                            float lambda);

template TWSolution basic(const Input& input, INIT init, float lambda);

template TWSolution dynamic_vehicle_choice(const Input& input,
                                           INIT init,
                                           float lambda);

} // namespace heuristics
} // namespace vroom
