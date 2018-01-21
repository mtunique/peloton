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
#include "optimizer/stats/column_stats.h"

namespace peloton {
namespace optimizer {

using GroupID = int32_t;

const GroupID UNDEFINED_GROUP = -1;
class ColumnStats;

//===--------------------------------------------------------------------===//
// Group
//===--------------------------------------------------------------------===//
class Group {
 public:
  Group(GroupID id, std::unordered_set<std::string> table_alias);

  // If the GroupExpression is generated by applying a
  // property enforcer, we add them to enforced_exprs_
  // which will not be enumerated during OptimizeExpression
  void AddExpression(std::shared_ptr<GroupExpression> expr, bool enforced);

  void RemoveLogicalExpression(size_t idx) {
    logical_expressions_.erase(logical_expressions_.begin() + idx);
  }

  bool SetExpressionCost(GroupExpression *expr, double cost,
                         std::shared_ptr<PropertySet> &properties);

  GroupExpression *GetBestExpression(std::shared_ptr<PropertySet> &properties);

  inline const std::unordered_set<std::string> &GetTableAliases() const {
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

  std::shared_ptr<ColumnStats> GetStats(std::string column_name) {
    if (!stats_.count(column_name)) {
      return nullptr;
    }
    return stats_[column_name];
  }
  void AddStats(std::string column_name, std::shared_ptr<ColumnStats> stats) {
    PL_ASSERT(stats_.empty() || GetNumRows() == stats->num_rows);
    stats_[column_name] = stats;
  }
  bool HasColumnStats(std::string column_name) { return stats_.count(column_name); }
  size_t GetNumRows() {
    if (stats_.empty()) {
      return 0;
    }
    return stats_.begin()->second->num_rows;
  }

  inline GroupID GetID() { return id_; }

  // This is called in rewrite phase to erase the only logical expression in the
  // group
  inline void EraseLogicalExpression() {
    PL_ASSERT(logical_expressions_.size() == 1);
    PL_ASSERT(physical_expressions_.size() == 0);
    logical_expressions_.clear();
  }

  // This should only be called in rewrite phase to retrieve the only logical
  // expr in the group
  inline GroupExpression *GetLogicalExpression() {
    PL_ASSERT(logical_expressions_.size() == 1);
    PL_ASSERT(physical_expressions_.size() == 0);
    return logical_expressions_[0].get();
  }

 private:
  GroupID id_;
  // All the table alias this group represents. This will not change once create
  // TODO(boweic) Do not use string, store table alias id
  std::unordered_set<std::string> table_aliases_;
  std::unordered_map<std::shared_ptr<PropertySet>,
                     std::tuple<double, GroupExpression *>, PropSetPtrHash,
                     PropSetPtrEq> lowest_cost_expressions_;

  // Whether equivalent logical expressions have been explored for this group
  bool has_explored_;

  std::vector<std::shared_ptr<GroupExpression>> logical_expressions_;
  std::vector<std::shared_ptr<GroupExpression>> physical_expressions_;
  std::vector<std::shared_ptr<GroupExpression>> enforced_exprs_;

  // We'll add stats lazily
  // TODO(boweic):
  // 1. use table alias id + column offset to identify the column
  // 2. Support stats for arbitary expressions
  std::unordered_map<std::string, std::shared_ptr<ColumnStats>> stats_;
  double cost_lower_bound_ = -1;
};

}  // namespace optimizer
}  // namespace peloton
