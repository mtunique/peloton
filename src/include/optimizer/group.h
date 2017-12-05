//===----------------------------------------------------------------------===//
//
//                         Peloton
//
// group.h
//
// Identification: src/include/optimizer/group.h
//
// Copyright (c) 2015-16, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#pragma once

#include <unordered_map>
#include <vector>

#include "optimizer/group_expression.h"
#include "optimizer/operator_node.h"
#include "optimizer/property.h"
#include "optimizer/property_set.h"

namespace peloton {
namespace optimizer {

using GroupID = int32_t;

const GroupID UNDEFINED_GROUP = -1;

//===--------------------------------------------------------------------===//
// Group
//===--------------------------------------------------------------------===//
class Group {
 public:
  Group(GroupID id, std::unordered_set<std::string> table_alias,
        std::shared_ptr<Stats> stats);

  // If the GroupExpression is generated by applying a
  // property enforcer, we add them to enforced_exprs_
  // which will not be enumerated during OptimizeExpression
  void AddExpression(std::shared_ptr<GroupExpression> expr, bool enforced);

  void RemoveLogicalExpression(size_t idx) {
    logical_expressions_.erase(logical_expressions_.begin() + idx);
  }

  bool SetExpressionCost(GroupExpression* expr, double cost,
                         std::shared_ptr<PropertySet>& properties);

  GroupExpression* GetBestExpression(std::shared_ptr<PropertySet>& properties);

  inline const std::unordered_set<std::string>& GetTableAliases() const {
    return table_aliases_;
  }

  // TODO: thread safety?
  const std::vector<std::shared_ptr<GroupExpression>> GetLogicalExpressions()
      const {
    return logical_expressions_;
  }

  // TODO: thread safety?
  const std::vector<std::shared_ptr<GroupExpression>> GetPhysicalExpressions()
      const {
    return physical_expressions_;
  }

  inline double GetCostLB() { return cost_lower_bound_; }

  inline void SetExplorationFlag() { has_explored_ = true; }
  inline bool HasExplored() { return has_explored_; }

  // Return the raw Stats pointer, caller should not own the Stats
  Stats* GetStats() { return stats_.get(); }

 private:
  GroupID id_;
  // All the table alias this group represents. This will not change once create
  // TODO(boweic) Do not use string, store table alias id
  std::unordered_set<std::string> table_aliases_;
  std::unordered_map<std::shared_ptr<PropertySet>,
                     std::tuple<double, GroupExpression*>, PropSetPtrHash,
                     PropSetPtrEq> lowest_cost_expressions_;

  // Whether equivalent logical expressions have been explored for this group
  bool has_explored_;

  std::vector<std::shared_ptr<GroupExpression>> logical_expressions_;
  std::vector<std::shared_ptr<GroupExpression>> physical_expressions_;
  std::vector<std::shared_ptr<GroupExpression>> enforced_exprs_;

  // TODO(boweic): we need to add some fileds that indicate the logical property
  // of the given group, e.g. output schema, stats and cost
  std::shared_ptr<Stats> stats_;
  double cost_lower_bound_ = -1;
};

}  // namespace optimizer
}  // namespace peloton
