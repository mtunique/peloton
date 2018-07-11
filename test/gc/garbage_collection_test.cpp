//===----------------------------------------------------------------------===//
//
//                         Peloton
//
// garbage_collection_test.cpp
//
// Identification: test/gc/garbage_collection_test.cpp
//
// Copyright (c) 2015-16, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "concurrency/testing_transaction_util.h"
#include "executor/testing_executor_util.h"
#include "common/harness.h"
#include "gc/gc_manager.h"
#include "gc/gc_manager_factory.h"
#include "concurrency/epoch_manager.h"
#include "concurrency/transaction_manager_factory.h"

#include "catalog/catalog.h"
#include "storage/data_table.h"
#include "storage/database.h"
#include "storage/storage_manager.h"
#include "storage/tile_group.h"

namespace peloton {
namespace test {

//===--------------------------------------------------------------------===//
// Garbage Collection Tests
//===--------------------------------------------------------------------===//

class GarbageCollectionTests : public PelotonTest {};

void UpdateTuple(storage::DataTable *table, const int update_num,
                 const int total_num) {
  srand(15721);

  auto &txn_manager = concurrency::TransactionManagerFactory::GetInstance();
  TransactionScheduler scheduler(1, table, &txn_manager);
  for (int i = 0; i < update_num; i++) {
    scheduler.Txn(0).Update(rand() % total_num, rand() % 15721);
  }
  scheduler.Txn(0).Commit();
  scheduler.Run();

  EXPECT_TRUE(scheduler.schedules[0].txn_result == ResultType::SUCCESS);
}

void DeleteTuple(storage::DataTable *table, const int delete_num,
                 const int total_num) {
  srand(15721);

  auto &txn_manager = concurrency::TransactionManagerFactory::GetInstance();
  TransactionScheduler scheduler(1, table, &txn_manager);
  for (int i = 0; i < delete_num; i++) {
    scheduler.Txn(0).Delete(rand() % total_num);
  }
  scheduler.Txn(0).Commit();
  scheduler.Run();

  EXPECT_TRUE(scheduler.schedules[0].txn_result == ResultType::SUCCESS);
}

void SelectTuple(storage::DataTable *table, const int select_num,
                 const int total_num) {
  srand(15721);

  auto &txn_manager = concurrency::TransactionManagerFactory::GetInstance();
  TransactionScheduler scheduler(1, table, &txn_manager);
  for (int i = 0; i < select_num; i++) {
    scheduler.Txn(0).Read(rand() % total_num);
  }
  scheduler.Txn(0).Commit();
  scheduler.Run();

  EXPECT_TRUE(scheduler.schedules[0].txn_result == ResultType::SUCCESS);
}

// count number of expired versions.
int GarbageNum(storage::DataTable *table) {
  auto table_tile_group_count_ = table->GetTileGroupCount();
  auto current_tile_group_offset_ = START_OID;

  int old_num = 0;

  while (current_tile_group_offset_ < table_tile_group_count_) {
    auto tile_group = table->GetTileGroup(current_tile_group_offset_++);
    auto tile_group_header = tile_group->GetHeader();
    oid_t active_tuple_count = tile_group->GetNextTupleSlot();

    for (oid_t tuple_id = 0; tuple_id < active_tuple_count; tuple_id++) {
      auto tuple_txn_id = tile_group_header->GetTransactionId(tuple_id);
      auto tuple_end_cid = tile_group_header->GetEndCommitId(tuple_id);
      if (tuple_txn_id == INITIAL_TXN_ID && tuple_end_cid != MAX_CID) {
        old_num++;
      }
    }
  }

  LOG_INFO("old version num = %d", old_num);
  return old_num;
}

// get tuple recycled by GC
int RecycledNum(storage::DataTable *table) {
  int count = 0;
  while (
      !gc::GCManagerFactory::GetInstance().GetRecycledTupleSlot(table->GetOid()).IsNull())
    count++;

  LOG_INFO("recycled version num = %d", count);
  return count;
}

TEST_F(GarbageCollectionTests, UpdateTest) {
  auto &epoch_manager = concurrency::EpochManagerFactory::GetInstance();
  epoch_manager.Reset(1);

  std::vector<std::unique_ptr<std::thread>> gc_threads;

  gc::GCManagerFactory::Configure(1);
  auto &gc_manager = gc::GCManagerFactory::GetInstance();

  auto storage_manager = storage::StorageManager::GetInstance();
  // create database
  auto database = TestingExecutorUtil::InitializeDatabase("update_db");
  oid_t db_id = database->GetOid();
  EXPECT_TRUE(storage_manager->HasDatabase(db_id));

  auto prev_tc = gc_manager.GetTableCount();

  // create a table with only one key
  const int num_key = 1;
  std::unique_ptr<storage::DataTable> table(TestingTransactionUtil::CreateTable(
      num_key, "UPDATE_TABLE", db_id, 12345, 1234, true));

  EXPECT_EQ(1, gc_manager.GetTableCount() - prev_tc);

  gc_manager.StartGC(gc_threads);

  const int update_num = 1;
  UpdateTuple(table.get(), update_num, num_key);

  // count garbage num
  auto old_num = GarbageNum(table.get());

  auto recycle_num = RecycledNum(table.get());

  // there should be only one garbage
  // generated by the last update
  EXPECT_EQ(1, old_num);
  // nothing is recycled yet.
  EXPECT_EQ(0, recycle_num);

  epoch_manager.SetCurrentEpochId(2);

  // get expired epoch id.
  // as the current epoch id is set to 2,
  // the expected expired epoch id should be 1.
  auto expired_eid = epoch_manager.GetExpiredEpochId();

  EXPECT_EQ(1, expired_eid);

  auto current_eid = epoch_manager.GetCurrentEpochId();

  EXPECT_EQ(2, current_eid);

  // sleep a while for gc to unlink expired version.
  std::this_thread::sleep_for(std::chrono::seconds(1));

  old_num = GarbageNum(table.get());

  recycle_num = RecycledNum(table.get());

  EXPECT_EQ(1, old_num);

  EXPECT_EQ(0, recycle_num);

  epoch_manager.SetCurrentEpochId(3);

  // sleep a while for gc to recycle expired version.
  std::this_thread::sleep_for(std::chrono::seconds(1));

  // there should be no garbage
  old_num = GarbageNum(table.get());

  recycle_num = RecycledNum(table.get());

  EXPECT_EQ(0, old_num);

  // there should be 1 tuple recycled
  EXPECT_EQ(1, recycle_num);

  gc_manager.StopGC();
  gc::GCManagerFactory::Configure(0);

  table.release();

  // DROP!
  TestingExecutorUtil::DeleteDatabase("update_db");
  auto &txn_manager = concurrency::TransactionManagerFactory::GetInstance();
  auto txn = txn_manager.BeginTransaction();
  EXPECT_THROW(catalog::Catalog::GetInstance()->GetDatabaseCatalogEntry(txn,
                                                                        db_id),
               CatalogException);
  txn_manager.CommitTransaction(txn);
  // EXPECT_FALSE(storage_manager->HasDatabase(db_id));

  for (auto &gc_thread : gc_threads) {
    gc_thread->join();
  }
}

TEST_F(GarbageCollectionTests, DeleteTest) {
  auto &epoch_manager = concurrency::EpochManagerFactory::GetInstance();
  epoch_manager.Reset(1);

  std::vector<std::unique_ptr<std::thread>> gc_threads;
  gc::GCManagerFactory::Configure(1);
  auto &gc_manager = gc::GCManagerFactory::GetInstance();
  auto storage_manager = storage::StorageManager::GetInstance();
  // create database
  auto database = TestingExecutorUtil::InitializeDatabase("delete_db");
  oid_t db_id = database->GetOid();
  EXPECT_TRUE(storage_manager->HasDatabase(db_id));

  auto prev_tc = gc_manager.GetTableCount();

  // create a table with only one key
  const int num_key = 1;
  std::unique_ptr<storage::DataTable> table(TestingTransactionUtil::CreateTable(
      num_key, "DELETE_TABLE", db_id, 12346, 1234, true));

  EXPECT_EQ(1, gc_manager.GetTableCount() - prev_tc);

  gc_manager.StartGC(gc_threads);

  const int delete_num = 1;
  DeleteTuple(table.get(), delete_num, num_key);

  // count garbage num
  auto old_num = GarbageNum(table.get());

  auto recycle_num = RecycledNum(table.get());

  // there should be only one garbage
  // generated by the last update
  EXPECT_EQ(1, old_num);
  // nothing is recycled yet.
  EXPECT_EQ(0, recycle_num);

  epoch_manager.SetCurrentEpochId(2);

  // get expired epoch id.
  // as the current epoch id is set to 2,
  // the expected expired epoch id should be 1.
  auto expired_eid = epoch_manager.GetExpiredEpochId();

  EXPECT_EQ(1, expired_eid);

  auto current_eid = epoch_manager.GetCurrentEpochId();

  EXPECT_EQ(2, current_eid);

  // sleep a while for gc to unlink expired version.
  std::this_thread::sleep_for(std::chrono::seconds(1));

  old_num = GarbageNum(table.get());

  recycle_num = RecycledNum(table.get());

  EXPECT_EQ(1, old_num);

  EXPECT_EQ(0, recycle_num);

  epoch_manager.SetCurrentEpochId(3);

  // sleep a while for gc to recycle expired version.
  std::this_thread::sleep_for(std::chrono::seconds(1));

  // there should be no garbage
  old_num = GarbageNum(table.get());

  recycle_num = RecycledNum(table.get());

  EXPECT_EQ(0, old_num);

  // there should be two versions to be recycled by the GC:
  // the deleted version and the empty version.
  EXPECT_EQ(2, recycle_num);

  gc_manager.StopGC();
  gc::GCManagerFactory::Configure(0);

  table.release();

  // DROP!
  TestingExecutorUtil::DeleteDatabase("delete_db");
  auto &txn_manager = concurrency::TransactionManagerFactory::GetInstance();
  auto txn = txn_manager.BeginTransaction();
  EXPECT_THROW(
      catalog::Catalog::GetInstance()->GetDatabaseCatalogEntry(txn, "DATABASE0"),
      CatalogException);
  txn_manager.CommitTransaction(txn);
  // EXPECT_FALSE(storage_manager->HasDatabase(db_id));

  for (auto &gc_thread : gc_threads) {
    gc_thread->join();
  }
}

}  // namespace test
}  // namespace peloton
