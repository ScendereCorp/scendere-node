#pragma once

#include <scendere/lib/config.hpp>
#include <scendere/lib/logger_mt.hpp>
#include <scendere/lib/numbers.hpp>
#include <scendere/node/rocksdb/rocksdb_iterator.hpp>
#include <scendere/secure/common.hpp>
#include <scendere/secure/store/account_store_partial.hpp>
#include <scendere/secure/store/confirmation_height_store_partial.hpp>
#include <scendere/secure/store/final_vote_store_partial.hpp>
#include <scendere/secure/store/frontier_store_partial.hpp>
#include <scendere/secure/store/online_weight_partial.hpp>
#include <scendere/secure/store/peer_store_partial.hpp>
#include <scendere/secure/store/pending_store_partial.hpp>
#include <scendere/secure/store/pruned_store_partial.hpp>
#include <scendere/secure/store/unchecked_store_partial.hpp>
#include <scendere/secure/store/version_store_partial.hpp>
#include <scendere/secure/store_partial.hpp>

#include <rocksdb/db.h>
#include <rocksdb/filter_policy.h>
#include <rocksdb/options.h>
#include <rocksdb/slice.h>
#include <rocksdb/table.h>
#include <rocksdb/utilities/optimistic_transaction_db.h>
#include <rocksdb/utilities/transaction.h>

namespace scendere
{
class logging_mt;
class rocksdb_config;
class rocksdb_store;

class unchecked_rocksdb_store : public unchecked_store_partial<rocksdb::Slice, scendere::rocksdb_store>
{
public:
	explicit unchecked_rocksdb_store (scendere::rocksdb_store &);

private:
	scendere::rocksdb_store & rocksdb_store;
};

class version_rocksdb_store : public version_store_partial<rocksdb::Slice, scendere::rocksdb_store>
{
public:
	explicit version_rocksdb_store (scendere::rocksdb_store &);
	void version_put (scendere::write_transaction const &, int);

private:
	scendere::rocksdb_store & rocksdb_store;
};

/**
 * rocksdb implementation of the block store
 */
class rocksdb_store : public store_partial<rocksdb::Slice, rocksdb_store>
{
private:
	scendere::block_store_partial<rocksdb::Slice, rocksdb_store> block_store_partial;
	scendere::frontier_store_partial<rocksdb::Slice, rocksdb_store> frontier_store_partial;
	scendere::account_store_partial<rocksdb::Slice, rocksdb_store> account_store_partial;
	scendere::pending_store_partial<rocksdb::Slice, rocksdb_store> pending_store_partial;
	scendere::unchecked_rocksdb_store unchecked_rocksdb_store;
	scendere::online_weight_store_partial<rocksdb::Slice, rocksdb_store> online_weight_store_partial;
	scendere::pruned_store_partial<rocksdb::Slice, rocksdb_store> pruned_store_partial;
	scendere::peer_store_partial<rocksdb::Slice, rocksdb_store> peer_store_partial;
	scendere::confirmation_height_store_partial<rocksdb::Slice, rocksdb_store> confirmation_height_store_partial;
	scendere::final_vote_store_partial<rocksdb::Slice, rocksdb_store> final_vote_store_partial;
	scendere::version_rocksdb_store version_rocksdb_store;

public:
	friend class scendere::unchecked_rocksdb_store;
	friend class scendere::version_rocksdb_store;

	explicit rocksdb_store (scendere::logger_mt &, boost::filesystem::path const &, scendere::ledger_constants & constants, scendere::rocksdb_config const & = scendere::rocksdb_config{}, bool open_read_only = false);

	scendere::write_transaction tx_begin_write (std::vector<scendere::tables> const & tables_requiring_lock = {}, std::vector<scendere::tables> const & tables_no_lock = {}) override;
	scendere::read_transaction tx_begin_read () const override;

	std::string vendor_get () const override;

	uint64_t count (scendere::transaction const & transaction_a, tables table_a) const override;

	bool exists (scendere::transaction const & transaction_a, tables table_a, scendere::rocksdb_val const & key_a) const;
	int get (scendere::transaction const & transaction_a, tables table_a, scendere::rocksdb_val const & key_a, scendere::rocksdb_val & value_a) const;
	int put (scendere::write_transaction const & transaction_a, tables table_a, scendere::rocksdb_val const & key_a, scendere::rocksdb_val const & value_a);
	int del (scendere::write_transaction const & transaction_a, tables table_a, scendere::rocksdb_val const & key_a);

	void serialize_memory_stats (boost::property_tree::ptree &) override;

	bool copy_db (boost::filesystem::path const & destination) override;
	void rebuild_db (scendere::write_transaction const & transaction_a) override;

	unsigned max_block_write_batch_num () const override;

	template <typename Key, typename Value>
	scendere::store_iterator<Key, Value> make_iterator (scendere::transaction const & transaction_a, tables table_a, bool const direction_asc) const
	{
		return scendere::store_iterator<Key, Value> (std::make_unique<scendere::rocksdb_iterator<Key, Value>> (db.get (), transaction_a, table_to_column_family (table_a), nullptr, direction_asc));
	}

	template <typename Key, typename Value>
	scendere::store_iterator<Key, Value> make_iterator (scendere::transaction const & transaction_a, tables table_a, scendere::rocksdb_val const & key) const
	{
		return scendere::store_iterator<Key, Value> (std::make_unique<scendere::rocksdb_iterator<Key, Value>> (db.get (), transaction_a, table_to_column_family (table_a), &key, true));
	}

	bool init_error () const override;

	std::string error_string (int status) const override;

private:
	bool error{ false };
	scendere::logger_mt & logger;
	scendere::ledger_constants & constants;
	// Optimistic transactions are used in write mode
	rocksdb::OptimisticTransactionDB * optimistic_db = nullptr;
	std::unique_ptr<rocksdb::DB> db;
	std::vector<std::unique_ptr<rocksdb::ColumnFamilyHandle>> handles;
	std::shared_ptr<rocksdb::TableFactory> small_table_factory;
	std::unordered_map<scendere::tables, scendere::mutex> write_lock_mutexes;
	scendere::rocksdb_config rocksdb_config;
	unsigned const max_block_write_batch_num_m;

	class tombstone_info
	{
	public:
		tombstone_info (uint64_t, uint64_t const);
		std::atomic<uint64_t> num_since_last_flush;
		uint64_t const max;
	};

	std::unordered_map<scendere::tables, tombstone_info> tombstone_map;
	std::unordered_map<char const *, scendere::tables> cf_name_table_map;

	rocksdb::Transaction * tx (scendere::transaction const & transaction_a) const;
	std::vector<scendere::tables> all_tables () const;

	bool not_found (int status) const override;
	bool success (int status) const override;
	int status_code_not_found () const override;
	int drop (scendere::write_transaction const &, tables) override;

	rocksdb::ColumnFamilyHandle * table_to_column_family (tables table_a) const;
	int clear (rocksdb::ColumnFamilyHandle * column_family);

	void open (bool & error_a, boost::filesystem::path const & path_a, bool open_read_only_a);

	void construct_column_family_mutexes ();
	rocksdb::Options get_db_options ();
	rocksdb::ColumnFamilyOptions get_common_cf_options (std::shared_ptr<rocksdb::TableFactory> const & table_factory_a, unsigned long long memtable_size_bytes_a) const;
	rocksdb::ColumnFamilyOptions get_active_cf_options (std::shared_ptr<rocksdb::TableFactory> const & table_factory_a, unsigned long long memtable_size_bytes_a) const;
	rocksdb::ColumnFamilyOptions get_small_cf_options (std::shared_ptr<rocksdb::TableFactory> const & table_factory_a) const;
	rocksdb::BlockBasedTableOptions get_active_table_options (std::size_t lru_size) const;
	rocksdb::BlockBasedTableOptions get_small_table_options () const;
	rocksdb::ColumnFamilyOptions get_cf_options (std::string const & cf_name_a) const;

	void on_flush (rocksdb::FlushJobInfo const &);
	void flush_table (scendere::tables table_a);
	void flush_tombstones_check (scendere::tables table_a);
	void generate_tombstone_map ();
	std::unordered_map<char const *, scendere::tables> create_cf_name_table_map () const;

	std::vector<rocksdb::ColumnFamilyDescriptor> create_column_families ();
	unsigned long long base_memtable_size_bytes () const;
	unsigned long long blocks_memtable_size_bytes () const;

	constexpr static int base_memtable_size = 16;
	constexpr static int base_block_cache_size = 8;

	friend class rocksdb_block_store_tombstone_count_Test;
};

extern template class store_partial<rocksdb::Slice, rocksdb_store>;
}
