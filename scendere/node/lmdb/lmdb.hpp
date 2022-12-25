#pragma once

#include <scendere/lib/diagnosticsconfig.hpp>
#include <scendere/lib/lmdbconfig.hpp>
#include <scendere/lib/logger_mt.hpp>
#include <scendere/lib/numbers.hpp>
#include <scendere/node/lmdb/lmdb_env.hpp>
#include <scendere/node/lmdb/lmdb_iterator.hpp>
#include <scendere/node/lmdb/lmdb_txn.hpp>
#include <scendere/secure/common.hpp>
#include <scendere/secure/store/account_store_partial.hpp>
#include <scendere/secure/store/block_store_partial.hpp>
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
#include <scendere/secure/versioning.hpp>

#include <boost/optional.hpp>

#include <lmdb/libraries/liblmdb/lmdb.h>

namespace boost
{
namespace filesystem
{
	class path;
}
}

namespace scendere
{
using mdb_val = db_val<MDB_val>;

class logging_mt;
class mdb_store;

class unchecked_mdb_store : public unchecked_store_partial<MDB_val, mdb_store>
{
public:
	explicit unchecked_mdb_store (scendere::mdb_store &);
};

/**
 * mdb implementation of the block store
 */
class mdb_store : public store_partial<MDB_val, mdb_store>
{
private:
	scendere::block_store_partial<MDB_val, mdb_store> block_store_partial;
	scendere::frontier_store_partial<MDB_val, mdb_store> frontier_store_partial;
	scendere::account_store_partial<MDB_val, mdb_store> account_store_partial;
	scendere::pending_store_partial<MDB_val, mdb_store> pending_store_partial;
	scendere::unchecked_mdb_store unchecked_mdb_store;
	scendere::online_weight_store_partial<MDB_val, mdb_store> online_weight_store_partial;
	scendere::pruned_store_partial<MDB_val, mdb_store> pruned_store_partial;
	scendere::peer_store_partial<MDB_val, mdb_store> peer_store_partial;
	scendere::confirmation_height_store_partial<MDB_val, mdb_store> confirmation_height_store_partial;
	scendere::final_vote_store_partial<MDB_val, mdb_store> final_vote_store_partial;
	scendere::version_store_partial<MDB_val, mdb_store> version_store_partial;

	friend class scendere::unchecked_mdb_store;

public:
	mdb_store (scendere::logger_mt &, boost::filesystem::path const &, scendere::ledger_constants & constants, scendere::txn_tracking_config const & txn_tracking_config_a = scendere::txn_tracking_config{}, std::chrono::milliseconds block_processor_batch_max_time_a = std::chrono::milliseconds (5000), scendere::lmdb_config const & lmdb_config_a = scendere::lmdb_config{}, bool backup_before_upgrade = false);
	scendere::write_transaction tx_begin_write (std::vector<scendere::tables> const & tables_requiring_lock = {}, std::vector<scendere::tables> const & tables_no_lock = {}) override;
	scendere::read_transaction tx_begin_read () const override;

	std::string vendor_get () const override;

	void serialize_mdb_tracker (boost::property_tree::ptree &, std::chrono::milliseconds, std::chrono::milliseconds) override;

	static void create_backup_file (scendere::mdb_env &, boost::filesystem::path const &, scendere::logger_mt &);

	void serialize_memory_stats (boost::property_tree::ptree &) override;

	unsigned max_block_write_batch_num () const override;

private:
	scendere::logger_mt & logger;
	bool error{ false };

public:
	scendere::mdb_env env;

	/**
	 * Maps head block to owning account
	 * scendere::block_hash -> scendere::account
	 */
	MDB_dbi frontiers_handle{ 0 };

	/**
	 * Maps account v1 to account information, head, rep, open, balance, timestamp and block count. (Removed)
	 * scendere::account -> scendere::block_hash, scendere::block_hash, scendere::block_hash, scendere::amount, uint64_t, uint64_t
	 */
	MDB_dbi accounts_v0_handle{ 0 };

	/**
	 * Maps account v0 to account information, head, rep, open, balance, timestamp and block count. (Removed)
	 * scendere::account -> scendere::block_hash, scendere::block_hash, scendere::block_hash, scendere::amount, uint64_t, uint64_t
	 */
	MDB_dbi accounts_v1_handle{ 0 };

	/**
	 * Maps account v0 to account information, head, rep, open, balance, timestamp, block count and epoch
	 * scendere::account -> scendere::block_hash, scendere::block_hash, scendere::block_hash, scendere::amount, uint64_t, uint64_t, scendere::epoch
	 */
	MDB_dbi accounts_handle{ 0 };

	/**
	 * Maps block hash to send block. (Removed)
	 * scendere::block_hash -> scendere::send_block
	 */
	MDB_dbi send_blocks_handle{ 0 };

	/**
	 * Maps block hash to receive block. (Removed)
	 * scendere::block_hash -> scendere::receive_block
	 */
	MDB_dbi receive_blocks_handle{ 0 };

	/**
	 * Maps block hash to open block. (Removed)
	 * scendere::block_hash -> scendere::open_block
	 */
	MDB_dbi open_blocks_handle{ 0 };

	/**
	 * Maps block hash to change block. (Removed)
	 * scendere::block_hash -> scendere::change_block
	 */
	MDB_dbi change_blocks_handle{ 0 };

	/**
	 * Maps block hash to v0 state block. (Removed)
	 * scendere::block_hash -> scendere::state_block
	 */
	MDB_dbi state_blocks_v0_handle{ 0 };

	/**
	 * Maps block hash to v1 state block. (Removed)
	 * scendere::block_hash -> scendere::state_block
	 */
	MDB_dbi state_blocks_v1_handle{ 0 };

	/**
	 * Maps block hash to state block. (Removed)
	 * scendere::block_hash -> scendere::state_block
	 */
	MDB_dbi state_blocks_handle{ 0 };

	/**
	 * Maps min_version 0 (destination account, pending block) to (source account, amount). (Removed)
	 * scendere::account, scendere::block_hash -> scendere::account, scendere::amount
	 */
	MDB_dbi pending_v0_handle{ 0 };

	/**
	 * Maps min_version 1 (destination account, pending block) to (source account, amount). (Removed)
	 * scendere::account, scendere::block_hash -> scendere::account, scendere::amount
	 */
	MDB_dbi pending_v1_handle{ 0 };

	/**
	 * Maps (destination account, pending block) to (source account, amount, version). (Removed)
	 * scendere::account, scendere::block_hash -> scendere::account, scendere::amount, scendere::epoch
	 */
	MDB_dbi pending_handle{ 0 };

	/**
	 * Representative weights. (Removed)
	 * scendere::account -> scendere::uint128_t
	 */
	MDB_dbi representation_handle{ 0 };

	/**
	 * Unchecked bootstrap blocks info.
	 * scendere::block_hash -> scendere::unchecked_info
	 */
	MDB_dbi unchecked_handle{ 0 };

	/**
	 * Samples of online vote weight
	 * uint64_t -> scendere::amount
	 */
	MDB_dbi online_weight_handle{ 0 };

	/**
	 * Meta information about block store, such as versions.
	 * scendere::uint256_union (arbitrary key) -> blob
	 */
	MDB_dbi meta_handle{ 0 };

	/**
	 * Pruned blocks hashes
	 * scendere::block_hash -> none
	 */
	MDB_dbi pruned_handle{ 0 };

	/*
	 * Endpoints for peers
	 * scendere::endpoint_key -> no_value
	*/
	MDB_dbi peers_handle{ 0 };

	/*
	 * Confirmation height of an account, and the hash for the block at that height
	 * scendere::account -> uint64_t, scendere::block_hash
	 */
	MDB_dbi confirmation_height_handle{ 0 };

	/*
	 * Contains block_sideband and block for all block types (legacy send/change/open/receive & state blocks)
	 * scendere::block_hash -> scendere::block_sideband, scendere::block
	 */
	MDB_dbi blocks_handle{ 0 };

	/**
	 * Maps root to block hash for generated final votes.
	 * scendere::qualified_root -> scendere::block_hash
	 */
	MDB_dbi final_votes_handle{ 0 };

	bool exists (scendere::transaction const & transaction_a, tables table_a, scendere::mdb_val const & key_a) const;

	int get (scendere::transaction const & transaction_a, tables table_a, scendere::mdb_val const & key_a, scendere::mdb_val & value_a) const;
	int put (scendere::write_transaction const & transaction_a, tables table_a, scendere::mdb_val const & key_a, scendere::mdb_val const & value_a) const;
	int del (scendere::write_transaction const & transaction_a, tables table_a, scendere::mdb_val const & key_a) const;

	bool copy_db (boost::filesystem::path const & destination_file) override;
	void rebuild_db (scendere::write_transaction const & transaction_a) override;

	template <typename Key, typename Value>
	scendere::store_iterator<Key, Value> make_iterator (scendere::transaction const & transaction_a, tables table_a, bool const direction_asc) const
	{
		return scendere::store_iterator<Key, Value> (std::make_unique<scendere::mdb_iterator<Key, Value>> (transaction_a, table_to_dbi (table_a), scendere::mdb_val{}, direction_asc));
	}

	template <typename Key, typename Value>
	scendere::store_iterator<Key, Value> make_iterator (scendere::transaction const & transaction_a, tables table_a, scendere::mdb_val const & key) const
	{
		return scendere::store_iterator<Key, Value> (std::make_unique<scendere::mdb_iterator<Key, Value>> (transaction_a, table_to_dbi (table_a), key));
	}

	bool init_error () const override;

	uint64_t count (scendere::transaction const &, MDB_dbi) const;
	std::string error_string (int status) const override;

	// These are only use in the upgrade process.
	std::shared_ptr<scendere::block> block_get_v14 (scendere::transaction const & transaction_a, scendere::block_hash const & hash_a, scendere::block_sideband_v14 * sideband_a = nullptr, bool * is_state_v1 = nullptr) const;
	std::size_t block_successor_offset_v14 (scendere::transaction const & transaction_a, std::size_t entry_size_a, scendere::block_type type_a) const;
	scendere::block_hash block_successor_v14 (scendere::transaction const & transaction_a, scendere::block_hash const & hash_a) const;
	scendere::mdb_val block_raw_get_v14 (scendere::transaction const & transaction_a, scendere::block_hash const & hash_a, scendere::block_type & type_a, bool * is_state_v1 = nullptr) const;
	boost::optional<scendere::mdb_val> block_raw_get_by_type_v14 (scendere::transaction const & transaction_a, scendere::block_hash const & hash_a, scendere::block_type & type_a, bool * is_state_v1) const;

private:
	bool do_upgrades (scendere::write_transaction &, bool &);
	void upgrade_v14_to_v15 (scendere::write_transaction &);
	void upgrade_v15_to_v16 (scendere::write_transaction const &);
	void upgrade_v16_to_v17 (scendere::write_transaction const &);
	void upgrade_v17_to_v18 (scendere::write_transaction const &);
	void upgrade_v18_to_v19 (scendere::write_transaction const &);
	void upgrade_v19_to_v20 (scendere::write_transaction const &);
	void upgrade_v20_to_v21 (scendere::write_transaction const &);

	std::shared_ptr<scendere::block> block_get_v18 (scendere::transaction const & transaction_a, scendere::block_hash const & hash_a) const;
	scendere::mdb_val block_raw_get_v18 (scendere::transaction const & transaction_a, scendere::block_hash const & hash_a, scendere::block_type & type_a) const;
	boost::optional<scendere::mdb_val> block_raw_get_by_type_v18 (scendere::transaction const & transaction_a, scendere::block_hash const & hash_a, scendere::block_type & type_a) const;
	scendere::uint128_t block_balance_v18 (scendere::transaction const & transaction_a, scendere::block_hash const & hash_a) const;

	void open_databases (bool &, scendere::transaction const &, unsigned);

	int drop (scendere::write_transaction const & transaction_a, tables table_a) override;
	int clear (scendere::write_transaction const & transaction_a, MDB_dbi handle_a);

	bool not_found (int status) const override;
	bool success (int status) const override;
	int status_code_not_found () const override;

	MDB_dbi table_to_dbi (tables table_a) const;

	mutable scendere::mdb_txn_tracker mdb_txn_tracker;
	scendere::mdb_txn_callbacks create_txn_callbacks () const;
	bool txn_tracking_enabled;

	uint64_t count (scendere::transaction const & transaction_a, tables table_a) const override;

	bool vacuum_after_upgrade (boost::filesystem::path const & path_a, scendere::lmdb_config const & lmdb_config_a);

	class upgrade_counters
	{
	public:
		upgrade_counters (uint64_t count_before_v0, uint64_t count_before_v1);
		bool are_equal () const;

		uint64_t before_v0;
		uint64_t before_v1;
		uint64_t after_v0{ 0 };
		uint64_t after_v1{ 0 };
	};
};

template <>
void * mdb_val::data () const;
template <>
std::size_t mdb_val::size () const;
template <>
mdb_val::db_val (std::size_t size_a, void * data_a);
template <>
void mdb_val::convert_buffer_to_value ();

extern template class store_partial<MDB_val, mdb_store>;
}
