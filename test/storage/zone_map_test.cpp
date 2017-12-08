//===----------------------------------------------------------------------===//
//
//                         Peloton
//
// zone_map_test.cpp
//
// Identification: test/storage/zone_map_test.cpp
//
// Copyright (c) 2015-16, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include <stdio.h>
#include <stdlib.h>
#include "common/harness.h"

#include "storage/data_table.h"

#include "executor/testing_executor_util.h"
#include "storage/tile_group.h"
#include "storage/database.h"
#include "storage/tile.h"
#include "storage/tile_group_header.h"
#include "storage/tuple.h"
#include "storage/zone_map_manager.h"
#include "catalog/schema.h"
#include "catalog/catalog.h"
#include "catalog/zone_map_catalog.h"
#include "expression/abstract_expression.h"
#include "expression/expression_util.h"
#include "concurrency/transaction_manager_factory.h"

namespace peloton {
namespace test {

class ZoneMapTests : public PelotonTest {};

namespace {

  storage::DataTable *CreateTestTable() {
    std::unique_ptr<storage::DataTable> data_table(TestingExecutorUtil::CreateTable(5, false, 1));
    auto &txn_manager = concurrency::TransactionManagerFactory::GetInstance();
    auto txn = txn_manager.BeginTransaction();
    TestingExecutorUtil::PopulateTable(data_table.get(), 20, false, false, false, txn);
    txn_manager.CommitTransaction(txn);
    oid_t num_tile_groups = (data_table.get())->GetTileGroupCount();
    for (oid_t i = 0; i < num_tile_groups - 1; i++) {
      auto tile_group = (data_table.get())->GetTileGroup(i);
      auto tile_group_ptr = tile_group.get();
      auto tile_group_header = tile_group_ptr->GetHeader();
      tile_group_header->SetImmutability();
    }
    auto catalog = catalog::Catalog::GetInstance();
    (void)catalog;
    storage::ZoneMapManager *zone_map_manager = storage::ZoneMapManager::GetInstance();
    zone_map_manager->CreateZoneMapTableInCatalog();
    txn = txn_manager.BeginTransaction();
    zone_map_manager->CreateZoneMapsForTable((data_table.get()), txn);
    txn_manager.CommitTransaction(txn);
    return data_table.release();
  }


  expression::AbstractExpression *CreateSinglePredicate(int col_id, ExpressionType type, type::Value constant_value) {
    expression::AbstractExpression *tuple_value_expr = nullptr;
    tuple_value_expr = expression::ExpressionUtil::TupleValueFactory( type::TypeId::INTEGER, 0, col_id);
    expression::AbstractExpression *constant_value_expr = nullptr;
    constant_value_expr = expression::ExpressionUtil::ConstantValueFactory(constant_value);
    expression::AbstractExpression *predicate = expression::ExpressionUtil::ComparisonFactory(
        type, tuple_value_expr, constant_value_expr);
    return predicate;
  }

  expression::AbstractExpression *CreateConjunctionPredicate(expression::AbstractExpression *expr1 ,expression::AbstractExpression *expr2) {
    expression::AbstractExpression *predicate = expression::ExpressionUtil::ConjunctionFactory(
        ExpressionType::CONJUNCTION_AND, expr1, expr2);
    return predicate;
  }


TEST_F(ZoneMapTests, ZoneMapContentsTest) {
  
  std::unique_ptr<storage::DataTable> data_table(CreateTestTable());
  oid_t database_id = (data_table.get())->GetDatabaseOid();
  oid_t table_id = (data_table.get())->GetOid();
  oid_t num_tile_groups = (data_table.get())->GetTileGroupCount();
  storage::ZoneMapManager *zone_map_manager = storage::ZoneMapManager::GetInstance();
  
  for (oid_t i = 0; i < num_tile_groups - 1; i++) {
    for (int j = 0; j < 4; j++) {
      std::shared_ptr<storage::ZoneMapManager::ColumnStatistics> stats = zone_map_manager->GetZoneMapFromCatalog(database_id, table_id, i, j);
      type::Value min_val = (stats.get())->min;
      type::Value max_val = (stats.get())->max;
      int max = ((TESTS_TUPLES_PER_TILEGROUP * (i+1)) - 1)*10;
      int min = (TESTS_TUPLES_PER_TILEGROUP * (i))*10;
      // Integer Columns
      if (j == 0 || j == 1) {
        int min_zone_map = min_val.GetAs<int>();
        int max_zone_map = max_val.GetAs<int>();
        EXPECT_EQ(min + j, min_zone_map);
        EXPECT_EQ(max + j, max_zone_map);
      } else if (j == 2) {
        // Decimal Column
        double min_zone_map = min_val.GetAs<double>();
        double max_zone_map = max_val.GetAs<double>();
        EXPECT_EQ((double)(min + j), min_zone_map);
        EXPECT_EQ((double)(max + j), max_zone_map);
      } else {
        // VARCHAR Column
        const char *min_zone_map_str;
        const char *max_zone_map_str;

        min_zone_map_str = min_val.GetData();
        max_zone_map_str = max_val.GetData();

        std::stringstream min_ss;
        std::stringstream max_ss;
        if (i == 0) {
          min_ss << (min + j + 10);
        } else {
          min_ss << (min + j);
        }
        max_ss << (max +j);
        std::string min_str = min_ss.str();
        std::string max_str = max_ss.str();
        EXPECT_EQ(min_str, min_zone_map_str);
        EXPECT_EQ(max_str, max_zone_map_str);
      }
    }
  }
}

TEST_F(ZoneMapTests, ZoneMapIntegerEqualityPredicateTest) {
  // Predicate A = 10
  std::unique_ptr<storage::DataTable> data_table(CreateTestTable());
  auto constant_value = type::ValueFactory::GetIntegerValue(10);
  auto pred = CreateSinglePredicate(0, ExpressionType::COMPARE_EQUAL, constant_value);
  bool zone_mappable = pred->IsZoneMappable();
  EXPECT_EQ(zone_mappable, true);
  auto parsed_predicates = pred->GetParsedPredicates();
  size_t num_preds = parsed_predicates->size();
  EXPECT_EQ(num_preds, 1);
  storage::ZoneMapManager *zone_map_manager = storage::ZoneMapManager::GetInstance();
  oid_t num_tile_groups = (data_table.get())->GetTileGroupCount();
  auto temp =(std::vector<storage::PredicateInfo> *)parsed_predicates;
  for (oid_t i = 0; i < num_tile_groups - 1; i++) {
      bool result = zone_map_manager->ComparePredicateAgainstZoneMap(temp->data() , 1, data_table.get(), i);
      if (i == 0) {
        EXPECT_EQ(result, true);
      } else {
        EXPECT_EQ(result, false);
      }
  }
  pred->ClearParsedPredicates();
  delete pred;
}


TEST_F(ZoneMapTests, ZoneMapIntegerLessThanPredicateTest) {
  // Predicate A < 100
  std::unique_ptr<storage::DataTable> data_table(CreateTestTable());
  auto constant_value = type::ValueFactory::GetIntegerValue(100);
  auto pred = CreateSinglePredicate(0, ExpressionType::COMPARE_LESSTHAN, constant_value);
  bool zone_mappable = pred->IsZoneMappable();
  EXPECT_EQ(zone_mappable, true);
  auto parsed_predicates = pred->GetParsedPredicates();
  size_t num_preds = parsed_predicates->size();
  EXPECT_EQ(num_preds, 1);
  storage::ZoneMapManager *zone_map_manager = storage::ZoneMapManager::GetInstance();
  oid_t num_tile_groups = (data_table.get())->GetTileGroupCount();
  auto temp =(std::vector<storage::PredicateInfo> *)parsed_predicates;
  for (oid_t i = 0; i < num_tile_groups - 1; i++) {
      bool result = zone_map_manager->ComparePredicateAgainstZoneMap(temp->data() , 1, data_table.get(), i);
      if (i <= 1) {
        EXPECT_EQ(result, true);
      } else {
        EXPECT_EQ(result, false);
      }
  }
  pred->ClearParsedPredicates();
  delete pred;
}

TEST_F(ZoneMapTests, ZoneMapIntegerGreaterThanPredicateTest) {
  // Predicate A > 140
  std::unique_ptr<storage::DataTable> data_table(CreateTestTable());
  auto constant_value = type::ValueFactory::GetIntegerValue(140);
  auto pred = CreateSinglePredicate(0, ExpressionType::COMPARE_GREATERTHAN, constant_value);
  bool zone_mappable = pred->IsZoneMappable();
  EXPECT_EQ(zone_mappable, true);
  auto parsed_predicates = pred->GetParsedPredicates();
  size_t num_preds = parsed_predicates->size();
  EXPECT_EQ(num_preds, 1);
  storage::ZoneMapManager *zone_map_manager = storage::ZoneMapManager::GetInstance();
  oid_t num_tile_groups = (data_table.get())->GetTileGroupCount();
  auto temp =(std::vector<storage::PredicateInfo> *)parsed_predicates;
  for (oid_t i = 0; i < num_tile_groups - 1; i++) {
      bool result = zone_map_manager->ComparePredicateAgainstZoneMap(temp->data() , 1, data_table.get(), i);
      if (i <= 2) {
        EXPECT_EQ(result, false);
      } else {
        EXPECT_EQ(result, true);
      }
  }
  pred->ClearParsedPredicates();
  delete pred;
}

TEST_F(ZoneMapTests, ZoneMapIntegerConjunctionPredicateTest) {
  // Predicate A > 40 and A < 150
  std::unique_ptr<storage::DataTable> data_table(CreateTestTable());

  auto constant_value = type::ValueFactory::GetIntegerValue(40);
  auto pred1 = CreateSinglePredicate(0, ExpressionType::COMPARE_GREATERTHAN, constant_value);

  constant_value = type::ValueFactory::GetIntegerValue(150);
  auto pred2 = CreateSinglePredicate(0, ExpressionType::COMPARE_LESSTHAN, constant_value);

  auto conj_pred = CreateConjunctionPredicate(pred1, pred2);

  bool zone_mappable = conj_pred->IsZoneMappable();
  EXPECT_EQ(zone_mappable, true);
  auto parsed_predicates = conj_pred->GetParsedPredicates();
  size_t num_preds = parsed_predicates->size();
  EXPECT_EQ(num_preds, 2);
  storage::ZoneMapManager *zone_map_manager = storage::ZoneMapManager::GetInstance();
  oid_t num_tile_groups = (data_table.get())->GetTileGroupCount();
  auto temp =(std::vector<storage::PredicateInfo> *)parsed_predicates;
  for (oid_t i = 0; i < num_tile_groups - 1; i++) {
      bool result = zone_map_manager->ComparePredicateAgainstZoneMap(temp->data() , 2, data_table.get(), i);
      if (i == 0 || i == 3) {
        EXPECT_EQ(result, false);
      } else {
        EXPECT_EQ(result, true);
      }
  }
  conj_pred->ClearParsedPredicates();
  pred1->ClearParsedPredicates();
  pred2->ClearParsedPredicates();
  delete conj_pred;
}

TEST_F(ZoneMapTests, ZoneMapDecimalConjunctionPredicateTest) {
  // Predicate A > 150 and A < 200
  std::unique_ptr<storage::DataTable> data_table(CreateTestTable());

  auto constant_value = type::ValueFactory::GetDecimalValue(150);
  auto pred1 = CreateSinglePredicate(2, ExpressionType::COMPARE_GREATERTHAN, constant_value);

  constant_value = type::ValueFactory::GetDecimalValue(200);
  auto pred2 = CreateSinglePredicate(2, ExpressionType::COMPARE_LESSTHAN, constant_value);

  auto conj_pred = CreateConjunctionPredicate(pred1, pred2);

  bool zone_mappable = conj_pred->IsZoneMappable();
  EXPECT_EQ(zone_mappable, true);
  auto parsed_predicates = conj_pred->GetParsedPredicates();
  size_t num_preds = parsed_predicates->size();
  EXPECT_EQ(num_preds, 2);
  storage::ZoneMapManager *zone_map_manager = storage::ZoneMapManager::GetInstance();
  oid_t num_tile_groups = (data_table.get())->GetTileGroupCount();
  auto temp =(std::vector<storage::PredicateInfo> *)parsed_predicates;
  for (oid_t i = 0; i < num_tile_groups - 1; i++) {
      bool result = zone_map_manager->ComparePredicateAgainstZoneMap(temp->data() , 2, data_table.get(), i);
      if (i < 3) {
        EXPECT_EQ(result, false);
      } else {
        EXPECT_EQ(result, true);
      }
  }
  conj_pred->ClearParsedPredicates();
  pred1->ClearParsedPredicates();
  pred2->ClearParsedPredicates();
  delete conj_pred;
}




}
}  // End test namespace
}  // End peloton namespace