//===----------------------------------------------------------------------===//
//
//                         Peloton
//
// rule_test.cpp
//
// Identification: test/optimizer/rule_test.cpp
//
// Copyright (c) 2015-16, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#define private public

#include "binder/bind_node_visitor.h"
#include "catalog/catalog.h"
#include "common/harness.h"
#include "common/logger.h"
#include "common/statement.h"
#include "concurrency/transaction_manager_factory.h"
#include "executor/create_executor.h"
#include "executor/delete_executor.h"
#include "executor/insert_executor.h"
#include "executor/plan_executor.h"
#include "executor/update_executor.h"
#include "expression/abstract_expression.h"
#include "expression/operator_expression.h"
#include "optimizer/operator_expression.h"
#include "optimizer/operators.h"
#include "optimizer/optimizer.h"
#include "optimizer/rule.h"
#include "optimizer/rule_impls.h"
#include "parser/postgresparser.h"
#include "planner/create_plan.h"
#include "planner/delete_plan.h"
#include "planner/insert_plan.h"
#include "planner/update_plan.h"
#include "sql/testing_sql_util.h"
#include "type/value_factory.h"

namespace peloton {
namespace test {

//===--------------------------------------------------------------------===//
// Binding Tests
//===--------------------------------------------------------------------===//

using namespace optimizer;

class OptimizerRuleTests : public PelotonTest {
 protected:
  GroupExpression *GetSingleGroupExpression(Memo &memo, GroupExpression *expr,
                                            int child_group_idx) {
    auto group = memo.GetGroupByID(expr->GetChildGroupId(child_group_idx));
    EXPECT_EQ(1, group->logical_expressions_.size());
    return group->logical_expressions_[0].get();
  }
};

TEST_F(OptimizerRuleTests, SimpleCommutativeRuleTest) {
  // Build op plan node to match rule
  auto left_get = std::make_shared<OperatorExpression>(LogicalGet::make());
  auto right_get = std::make_shared<OperatorExpression>(LogicalGet::make());
  auto join = std::make_shared<OperatorExpression>(LogicalInnerJoin::make());
  join->PushChild(left_get);
  join->PushChild(right_get);

  // Setup rule
  InnerJoinCommutativity rule;

  EXPECT_TRUE(rule.Check(join, nullptr));

  std::vector<std::shared_ptr<OperatorExpression>> outputs;
  rule.Transform(join, outputs, nullptr);
  EXPECT_EQ(outputs.size(), 1);

  auto output_join = outputs[0];

  EXPECT_EQ(output_join->Children()[0], right_get);
  EXPECT_EQ(output_join->Children()[1], left_get);
}

TEST_F(OptimizerRuleTests, SimpleAssociativeRuleTest) {
  // Start Join Structure: (left JOIN middle) JOIN right
  // End Join Structure: left JOIN (middle JOIN right)
  // Query: SELECT * from test1, test2, test3 WHERE test1.a = test2.a AND
  // test1.a = test3.a; Test ensures that predicate "test1.a = test2.a" is
  // redistributed to parent join

  // Setup Memo
  Optimizer optimizer;

  auto left_get = std::make_shared<OperatorExpression>(
      LogicalGet::make(0, {}, nullptr, "test1"));
  auto middle_get = std::make_shared<OperatorExpression>(
      LogicalGet::make(1, {}, nullptr, "test2"));
  auto right_get = std::make_shared<OperatorExpression>(
      LogicalGet::make(2, {}, nullptr, "test3"));

  auto left_get_group = optimizer.metadata_.memo.InsertExpression(
      optimizer.metadata_.MakeGroupExpression(left_get), true);
  auto middle_get_group = optimizer.metadata_.memo.InsertExpression(
      optimizer.metadata_.MakeGroupExpression(middle_get), true);
  auto right_get_group = optimizer.metadata_.memo.InsertExpression(
      optimizer.metadata_.MakeGroupExpression(right_get), true);

  auto left_leaf = std::make_shared<OperatorExpression>(
      LeafOperator::make(left_get_group->GetGroupID()));
  auto middle_leaf = std::make_shared<OperatorExpression>(
      LeafOperator::make(middle_get_group->GetGroupID()));
  auto right_leaf = std::make_shared<OperatorExpression>(
      LeafOperator::make(right_get_group->GetGroupID()));

  // Make Child Join
  std::vector<AnnotatedExpression> child_join_predicates;
  std::unordered_set<std::string> child_tables({"test1", "test2"});
  auto dummy_expr = std::shared_ptr<expression::AbstractExpression>{
      new expression::OperatorExpression(ExpressionType::COMPARE_EQUAL,
                                         type::TypeId::INTEGER)};

  AnnotatedExpression pred = {dummy_expr, child_tables};
  child_join_predicates.push_back(pred);

  auto child_join = std::make_shared<OperatorExpression>(
      LogicalInnerJoin::make(child_join_predicates));
  child_join->PushChild(left_leaf);
  child_join->PushChild(middle_leaf);
  optimizer.metadata_.memo.InsertExpression(
      optimizer.metadata_.MakeGroupExpression(child_join), true);

  // Make Parent join
  std::vector<AnnotatedExpression> parent_join_predicates;
  std::unordered_set<std::string> parent_tables({"test1", "test3"});
  pred = {dummy_expr, parent_tables};
  parent_join_predicates.push_back(pred);

  auto parent_join = std::make_shared<OperatorExpression>(
      LogicalInnerJoin::make(parent_join_predicates));
  parent_join->PushChild(child_join);
  parent_join->PushChild(right_leaf);

  optimizer.metadata_.memo.InsertExpression(
      optimizer.metadata_.MakeGroupExpression(parent_join), true);
  OptimizeContext *root_context =
      new OptimizeContext(&(optimizer.metadata_), nullptr);

  EXPECT_EQ(left_leaf, parent_join->Children()[0]->Children()[0]);
  EXPECT_EQ(middle_leaf, parent_join->Children()[0]->Children()[1]);
  EXPECT_EQ(right_leaf, parent_join->Children()[1]);
  EXPECT_EQ(1,
            parent_join->Op().As<LogicalInnerJoin>()->join_predicates.size());
  EXPECT_EQ(1, parent_join->Children()[0]
                   ->Op()
                   .As<LogicalInnerJoin>()
                   ->join_predicates.size());

  // Setup rule
  InnerJoinAssociativity rule;

  EXPECT_TRUE(rule.Check(parent_join, root_context));
  std::vector<std::shared_ptr<OperatorExpression>> outputs;
  rule.Transform(parent_join, outputs, root_context);
  EXPECT_EQ(1, outputs.size());

  auto output_join = outputs[0];

  EXPECT_EQ(left_leaf, output_join->Children()[0]);
  EXPECT_EQ(middle_leaf, output_join->Children()[1]->Children()[0]);
  EXPECT_EQ(right_leaf, output_join->Children()[1]->Children()[1]);

  auto parent_join_op = output_join->Op().As<LogicalInnerJoin>();
  auto child_join_op = output_join->Children()[1]->Op().As<LogicalInnerJoin>();
  EXPECT_EQ(2, parent_join_op->join_predicates.size());
  EXPECT_EQ(0, child_join_op->join_predicates.size());
}

TEST_F(OptimizerRuleTests, SimpleAssociativeRuleTest2) {
  // Start Join Structure: (left JOIN middle) JOIN right
  // End Join Structure: left JOIN (middle JOIN right)
  // Query: SELECT * from test1, test2, test3 WHERE test1.a = test3.a AND
  // test2.a = test3.a; Test ensures that predicate "test2.a = test3.a" is
  // redistributed to child join

  // Setup Memo
  Optimizer optimizer;
  auto &memo = optimizer.metadata_.memo;

  auto left_get = std::make_shared<OperatorExpression>(
      LogicalGet::make(0, {}, nullptr, "test1"));
  auto middle_get = std::make_shared<OperatorExpression>(
      LogicalGet::make(1, {}, nullptr, "test2"));
  auto right_get = std::make_shared<OperatorExpression>(
      LogicalGet::make(2, {}, nullptr, "test3"));

  // Create Groups for Get Operators
  auto left_get_group = memo.InsertExpression(
      optimizer.metadata_.MakeGroupExpression(left_get), true);
  auto middle_get_group = memo.InsertExpression(
      optimizer.metadata_.MakeGroupExpression(middle_get), true);
  auto right_get_group = memo.InsertExpression(
      optimizer.metadata_.MakeGroupExpression(right_get), true);

  auto left_leaf = std::make_shared<OperatorExpression>(
      LeafOperator::make(left_get_group->GetGroupID()));
  auto middle_leaf = std::make_shared<OperatorExpression>(
      LeafOperator::make(middle_get_group->GetGroupID()));
  auto right_leaf = std::make_shared<OperatorExpression>(
      LeafOperator::make(right_get_group->GetGroupID()));

  // Make Child Join
  auto child_join =
      std::make_shared<OperatorExpression>(LogicalInnerJoin::make());
  child_join->PushChild(left_leaf);
  child_join->PushChild(middle_leaf);
  optimizer.metadata_.memo.InsertExpression(
      optimizer.metadata_.MakeGroupExpression(child_join), true);

  // Make Parent join
  std::vector<AnnotatedExpression> parent_join_predicates;
  auto dummy_expr = std::shared_ptr<expression::AbstractExpression>{
      new expression::OperatorExpression(ExpressionType::COMPARE_EQUAL,
                                         type::TypeId::INTEGER)};
  std::unordered_set<std::string> pred1_tables({"test1", "test3"});
  AnnotatedExpression pred1 = {dummy_expr, pred1_tables};
  parent_join_predicates.push_back(pred1);

  std::unordered_set<std::string> pred2_tables({"test2", "test3"});
  AnnotatedExpression pred2 = {dummy_expr, pred2_tables};
  parent_join_predicates.push_back(pred2);

  auto parent_join = std::make_shared<OperatorExpression>(
      LogicalInnerJoin::make(parent_join_predicates));
  parent_join->PushChild(child_join);
  parent_join->PushChild(right_leaf);

  optimizer.metadata_.memo.InsertExpression(
      optimizer.metadata_.MakeGroupExpression(parent_join), true);
  OptimizeContext *root_context =
      new OptimizeContext(&(optimizer.metadata_), nullptr);

  EXPECT_EQ(left_leaf, parent_join->Children()[0]->Children()[0]);
  EXPECT_EQ(middle_leaf, parent_join->Children()[0]->Children()[1]);
  EXPECT_EQ(right_leaf, parent_join->Children()[1]);
  EXPECT_EQ(2,
            parent_join->Op().As<LogicalInnerJoin>()->join_predicates.size());
  EXPECT_EQ(0, parent_join->Children()[0]
                   ->Op()
                   .As<LogicalInnerJoin>()
                   ->join_predicates.size());

  // Setup rule
  InnerJoinAssociativity rule;

  EXPECT_TRUE(rule.Check(parent_join, root_context));
  std::vector<std::shared_ptr<OperatorExpression>> outputs;
  rule.Transform(parent_join, outputs, root_context);
  EXPECT_EQ(1, outputs.size());

  auto output_join = outputs[0];

  EXPECT_EQ(left_leaf, output_join->Children()[0]);
  EXPECT_EQ(middle_leaf, output_join->Children()[1]->Children()[0]);
  EXPECT_EQ(right_leaf, output_join->Children()[1]->Children()[1]);

  auto parent_join_op = output_join->Op().As<LogicalInnerJoin>();
  auto child_join_op = output_join->Children()[1]->Op().As<LogicalInnerJoin>();
  EXPECT_EQ(1, parent_join_op->join_predicates.size());
  EXPECT_EQ(1, child_join_op->join_predicates.size());
}

}  // namespace test
}  // namespace peloton
