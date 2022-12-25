#pragma once

#include <scendere/lib/lmdbconfig.hpp>
#include <scendere/lib/locks.hpp>
#include <scendere/lib/work.hpp>
#include <scendere/node/lmdb/lmdb.hpp>
#include <scendere/node/lmdb/wallet_value.hpp>
#include <scendere/node/openclwork.hpp>
#include <scendere/secure/common.hpp>
#include <scendere/secure/store.hpp>

#include <atomic>
#include <mutex>
#include <thread>
#include <unordered_set>
namespace scendere
{
class node;
class node_config;
class wallets;
// The fan spreads a key out over the heap to decrease the likelihood of it being recovered by memory inspection
class fan final
{
public:
	fan (scendere::raw_key const &, std::size_t);
	void value (scendere::raw_key &);
	void value_set (scendere::raw_key const &);
	std::vector<std::unique_ptr<scendere::raw_key>> values;

private:
	scendere::mutex mutex;
	void value_get (scendere::raw_key &);
};
class kdf final
{
public:
	kdf (unsigned & kdf_work) :
		kdf_work{ kdf_work }
	{
	}
	void phs (scendere::raw_key &, std::string const &, scendere::uint256_union const &);
	scendere::mutex mutex;
	unsigned & kdf_work;
};
enum class key_type
{
	not_a_type,
	unknown,
	adhoc,
	deterministic
};
class wallet_store final
{
public:
	wallet_store (bool &, scendere::kdf &, scendere::transaction &, scendere::account, unsigned, std::string const &);
	wallet_store (bool &, scendere::kdf &, scendere::transaction &, scendere::account, unsigned, std::string const &, std::string const &);
	std::vector<scendere::account> accounts (scendere::transaction const &);
	void initialize (scendere::transaction const &, bool &, std::string const &);
	scendere::uint256_union check (scendere::transaction const &);
	bool rekey (scendere::transaction const &, std::string const &);
	bool valid_password (scendere::transaction const &);
	bool valid_public_key (scendere::public_key const &);
	bool attempt_password (scendere::transaction const &, std::string const &);
	void wallet_key (scendere::raw_key &, scendere::transaction const &);
	void seed (scendere::raw_key &, scendere::transaction const &);
	void seed_set (scendere::transaction const &, scendere::raw_key const &);
	scendere::key_type key_type (scendere::wallet_value const &);
	scendere::public_key deterministic_insert (scendere::transaction const &);
	scendere::public_key deterministic_insert (scendere::transaction const &, uint32_t const);
	scendere::raw_key deterministic_key (scendere::transaction const &, uint32_t);
	uint32_t deterministic_index_get (scendere::transaction const &);
	void deterministic_index_set (scendere::transaction const &, uint32_t);
	void deterministic_clear (scendere::transaction const &);
	scendere::uint256_union salt (scendere::transaction const &);
	bool is_representative (scendere::transaction const &);
	scendere::account representative (scendere::transaction const &);
	void representative_set (scendere::transaction const &, scendere::account const &);
	scendere::public_key insert_adhoc (scendere::transaction const &, scendere::raw_key const &);
	bool insert_watch (scendere::transaction const &, scendere::account const &);
	void erase (scendere::transaction const &, scendere::account const &);
	scendere::wallet_value entry_get_raw (scendere::transaction const &, scendere::account const &);
	void entry_put_raw (scendere::transaction const &, scendere::account const &, scendere::wallet_value const &);
	bool fetch (scendere::transaction const &, scendere::account const &, scendere::raw_key &);
	bool exists (scendere::transaction const &, scendere::account const &);
	void destroy (scendere::transaction const &);
	scendere::store_iterator<scendere::account, scendere::wallet_value> find (scendere::transaction const &, scendere::account const &);
	scendere::store_iterator<scendere::account, scendere::wallet_value> begin (scendere::transaction const &, scendere::account const &);
	scendere::store_iterator<scendere::account, scendere::wallet_value> begin (scendere::transaction const &);
	scendere::store_iterator<scendere::account, scendere::wallet_value> end ();
	void derive_key (scendere::raw_key &, scendere::transaction const &, std::string const &);
	void serialize_json (scendere::transaction const &, std::string &);
	void write_backup (scendere::transaction const &, boost::filesystem::path const &);
	bool move (scendere::transaction const &, scendere::wallet_store &, std::vector<scendere::public_key> const &);
	bool import (scendere::transaction const &, scendere::wallet_store &);
	bool work_get (scendere::transaction const &, scendere::public_key const &, uint64_t &);
	void work_put (scendere::transaction const &, scendere::public_key const &, uint64_t);
	unsigned version (scendere::transaction const &);
	void version_put (scendere::transaction const &, unsigned);
	scendere::fan password;
	scendere::fan wallet_key_mem;
	static unsigned const version_1 = 1;
	static unsigned const version_2 = 2;
	static unsigned const version_3 = 3;
	static unsigned const version_4 = 4;
	static unsigned constexpr version_current = version_4;
	static scendere::account const version_special;
	static scendere::account const wallet_key_special;
	static scendere::account const salt_special;
	static scendere::account const check_special;
	static scendere::account const representative_special;
	static scendere::account const seed_special;
	static scendere::account const deterministic_index_special;
	static std::size_t const check_iv_index;
	static std::size_t const seed_iv_index;
	static int const special_count;
	scendere::kdf & kdf;
	std::atomic<MDB_dbi> handle{ 0 };
	std::recursive_mutex mutex;

private:
	MDB_txn * tx (scendere::transaction const &) const;
};
// A wallet is a set of account keys encrypted by a common encryption key
class wallet final : public std::enable_shared_from_this<scendere::wallet>
{
public:
	std::shared_ptr<scendere::block> change_action (scendere::account const &, scendere::account const &, uint64_t = 0, bool = true);
	std::shared_ptr<scendere::block> receive_action (scendere::block_hash const &, scendere::account const &, scendere::uint128_union const &, scendere::account const &, uint64_t = 0, bool = true);
	std::shared_ptr<scendere::block> send_action (scendere::account const &, scendere::account const &, scendere::uint128_t const &, uint64_t = 0, bool = true, boost::optional<std::string> = {});
	bool action_complete (std::shared_ptr<scendere::block> const &, scendere::account const &, bool const, scendere::block_details const &);
	wallet (bool &, scendere::transaction &, scendere::wallets &, std::string const &);
	wallet (bool &, scendere::transaction &, scendere::wallets &, std::string const &, std::string const &);
	void enter_initial_password ();
	bool enter_password (scendere::transaction const &, std::string const &);
	scendere::public_key insert_adhoc (scendere::raw_key const &, bool = true);
	bool insert_watch (scendere::transaction const &, scendere::public_key const &);
	scendere::public_key deterministic_insert (scendere::transaction const &, bool = true);
	scendere::public_key deterministic_insert (uint32_t, bool = true);
	scendere::public_key deterministic_insert (bool = true);
	bool exists (scendere::public_key const &);
	bool import (std::string const &, std::string const &);
	void serialize (std::string &);
	bool change_sync (scendere::account const &, scendere::account const &);
	void change_async (scendere::account const &, scendere::account const &, std::function<void (std::shared_ptr<scendere::block> const &)> const &, uint64_t = 0, bool = true);
	bool receive_sync (std::shared_ptr<scendere::block> const &, scendere::account const &, scendere::uint128_t const &);
	void receive_async (scendere::block_hash const &, scendere::account const &, scendere::uint128_t const &, scendere::account const &, std::function<void (std::shared_ptr<scendere::block> const &)> const &, uint64_t = 0, bool = true);
	scendere::block_hash send_sync (scendere::account const &, scendere::account const &, scendere::uint128_t const &);
	void send_async (scendere::account const &, scendere::account const &, scendere::uint128_t const &, std::function<void (std::shared_ptr<scendere::block> const &)> const &, uint64_t = 0, bool = true, boost::optional<std::string> = {});
	void work_cache_blocking (scendere::account const &, scendere::root const &);
	void work_update (scendere::transaction const &, scendere::account const &, scendere::root const &, uint64_t);
	// Schedule work generation after a few seconds
	void work_ensure (scendere::account const &, scendere::root const &);
	bool search_receivable (scendere::transaction const &);
	void init_free_accounts (scendere::transaction const &);
	uint32_t deterministic_check (scendere::transaction const & transaction_a, uint32_t index);
	/** Changes the wallet seed and returns the first account */
	scendere::public_key change_seed (scendere::transaction const & transaction_a, scendere::raw_key const & prv_a, uint32_t count = 0);
	void deterministic_restore (scendere::transaction const & transaction_a);
	bool live ();
	std::unordered_set<scendere::account> free_accounts;
	std::function<void (bool, bool)> lock_observer;
	scendere::wallet_store store;
	scendere::wallets & wallets;
	scendere::mutex representatives_mutex;
	std::unordered_set<scendere::account> representatives;
};

class wallet_representatives
{
public:
	uint64_t voting{ 0 }; // Number of representatives with at least the configured minimum voting weight
	bool half_principal{ false }; // has representatives with at least 50% of principal representative requirements
	std::unordered_set<scendere::account> accounts; // Representatives with at least the configured minimum voting weight
	bool have_half_rep () const
	{
		return half_principal;
	}
	bool exists (scendere::account const & rep_a) const
	{
		return accounts.count (rep_a) > 0;
	}
	void clear ()
	{
		voting = 0;
		half_principal = false;
		accounts.clear ();
	}
};

/**
 * The wallets set is all the wallets a node controls.
 * A node may contain multiple wallets independently encrypted and operated.
 */
class wallets final
{
public:
	wallets (bool, scendere::node &);
	~wallets ();
	std::shared_ptr<scendere::wallet> open (scendere::wallet_id const &);
	std::shared_ptr<scendere::wallet> create (scendere::wallet_id const &);
	bool search_receivable (scendere::wallet_id const &);
	void search_receivable_all ();
	void destroy (scendere::wallet_id const &);
	void reload ();
	void do_wallet_actions ();
	void queue_wallet_action (scendere::uint128_t const &, std::shared_ptr<scendere::wallet> const &, std::function<void (scendere::wallet &)>);
	void foreach_representative (std::function<void (scendere::public_key const &, scendere::raw_key const &)> const &);
	bool exists (scendere::transaction const &, scendere::account const &);
	void start ();
	void stop ();
	void clear_send_ids (scendere::transaction const &);
	scendere::wallet_representatives reps () const;
	bool check_rep (scendere::account const &, scendere::uint128_t const &, bool const = true);
	void compute_reps ();
	void ongoing_compute_reps ();
	void split_if_needed (scendere::transaction &, scendere::store &);
	void move_table (std::string const &, MDB_txn *, MDB_txn *);
	std::unordered_map<scendere::wallet_id, std::shared_ptr<scendere::wallet>> get_wallets ();
	scendere::network_params & network_params;
	std::function<void (bool)> observer;
	std::unordered_map<scendere::wallet_id, std::shared_ptr<scendere::wallet>> items;
	std::multimap<scendere::uint128_t, std::pair<std::shared_ptr<scendere::wallet>, std::function<void (scendere::wallet &)>>, std::greater<scendere::uint128_t>> actions;
	scendere::locked<std::unordered_map<scendere::account, scendere::root>> delayed_work;
	scendere::mutex mutex;
	scendere::mutex action_mutex;
	scendere::condition_variable condition;
	scendere::kdf kdf;
	MDB_dbi handle;
	MDB_dbi send_action_ids;
	scendere::node & node;
	scendere::mdb_env & env;
	std::atomic<bool> stopped;
	std::thread thread;
	static scendere::uint128_t const generate_priority;
	static scendere::uint128_t const high_priority;
	/** Start read-write transaction */
	scendere::write_transaction tx_begin_write ();

	/** Start read-only transaction */
	scendere::read_transaction tx_begin_read ();

private:
	mutable scendere::mutex reps_cache_mutex;
	scendere::wallet_representatives representatives;
};

std::unique_ptr<container_info_component> collect_container_info (wallets & wallets, std::string const & name);

class wallets_store
{
public:
	virtual ~wallets_store () = default;
	virtual bool init_error () const = 0;
};
class mdb_wallets_store final : public wallets_store
{
public:
	mdb_wallets_store (boost::filesystem::path const &, scendere::lmdb_config const & lmdb_config_a = scendere::lmdb_config{});
	scendere::mdb_env environment;
	bool init_error () const override;
	bool error{ false };
};
}
