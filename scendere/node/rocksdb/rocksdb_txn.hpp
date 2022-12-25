#pragma once

#include <scendere/secure/store.hpp>

#include <rocksdb/db.h>
#include <rocksdb/filter_policy.h>
#include <rocksdb/options.h>
#include <rocksdb/slice.h>
#include <rocksdb/utilities/optimistic_transaction_db.h>
#include <rocksdb/utilities/transaction.h>

namespace scendere
{
class read_rocksdb_txn final : public read_transaction_impl
{
public:
	read_rocksdb_txn (rocksdb::DB * db);
	~read_rocksdb_txn ();
	void reset () override;
	void renew () override;
	void * get_handle () const override;

private:
	rocksdb::DB * db;
	rocksdb::ReadOptions options;
};

class write_rocksdb_txn final : public write_transaction_impl
{
public:
	write_rocksdb_txn (rocksdb::OptimisticTransactionDB * db_a, std::vector<scendere::tables> const & tables_requiring_locks_a, std::vector<scendere::tables> const & tables_no_locks_a, std::unordered_map<scendere::tables, scendere::mutex> & mutexes_a);
	~write_rocksdb_txn ();
	void commit () override;
	void renew () override;
	void * get_handle () const override;
	bool contains (scendere::tables table_a) const override;

private:
	rocksdb::Transaction * txn;
	rocksdb::OptimisticTransactionDB * db;
	std::vector<scendere::tables> tables_requiring_locks;
	std::vector<scendere::tables> tables_no_locks;
	std::unordered_map<scendere::tables, scendere::mutex> & mutexes;
	bool active{ true };

	void lock ();
	void unlock ();
};
}
