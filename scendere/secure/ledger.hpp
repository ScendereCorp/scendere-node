#pragma once

#include <scendere/lib/rep_weights.hpp>
#include <scendere/lib/timer.hpp>
#include <scendere/secure/common.hpp>

#include <map>

namespace scendere
{
class store;
class stat;
class write_transaction;

// map of vote weight per block, ordered greater first
using tally_t = std::map<scendere::uint128_t, std::shared_ptr<scendere::block>, std::greater<scendere::uint128_t>>;

class uncemented_info
{
public:
	uncemented_info (scendere::block_hash const & cemented_frontier, scendere::block_hash const & frontier, scendere::account const & account);
	scendere::block_hash cemented_frontier;
	scendere::block_hash frontier;
	scendere::account account;
};

class ledger final
{
public:
	ledger (scendere::store &, scendere::stat &, scendere::ledger_constants & constants, scendere::generate_cache const & = scendere::generate_cache ());
	scendere::account account (scendere::transaction const &, scendere::block_hash const &) const;
	scendere::account account_safe (scendere::transaction const &, scendere::block_hash const &, bool &) const;
	scendere::uint128_t amount (scendere::transaction const &, scendere::account const &);
	scendere::uint128_t amount (scendere::transaction const &, scendere::block_hash const &);
	/** Safe for previous block, but block hash_a must exist */
	scendere::uint128_t amount_safe (scendere::transaction const &, scendere::block_hash const & hash_a, bool &) const;
	scendere::uint128_t balance (scendere::transaction const &, scendere::block_hash const &) const;
	scendere::uint128_t balance_safe (scendere::transaction const &, scendere::block_hash const &, bool &) const;
	scendere::uint128_t account_balance (scendere::transaction const &, scendere::account const &, bool = false);
	scendere::uint128_t account_receivable (scendere::transaction const &, scendere::account const &, bool = false);
	scendere::uint128_t weight (scendere::account const &);
	std::shared_ptr<scendere::block> successor (scendere::transaction const &, scendere::qualified_root const &);
	std::shared_ptr<scendere::block> forked_block (scendere::transaction const &, scendere::block const &);
	bool block_confirmed (scendere::transaction const &, scendere::block_hash const &) const;
	scendere::block_hash latest (scendere::transaction const &, scendere::account const &);
	scendere::root latest_root (scendere::transaction const &, scendere::account const &);
	scendere::block_hash representative (scendere::transaction const &, scendere::block_hash const &);
	scendere::block_hash representative_calculated (scendere::transaction const &, scendere::block_hash const &);
	bool block_or_pruned_exists (scendere::block_hash const &) const;
	bool block_or_pruned_exists (scendere::transaction const &, scendere::block_hash const &) const;
	std::string block_text (char const *);
	std::string block_text (scendere::block_hash const &);
	bool is_send (scendere::transaction const &, scendere::state_block const &) const;
	scendere::account const & block_destination (scendere::transaction const &, scendere::block const &);
	scendere::block_hash block_source (scendere::transaction const &, scendere::block const &);
	std::pair<scendere::block_hash, scendere::block_hash> hash_root_random (scendere::transaction const &) const;
	scendere::process_return process (scendere::write_transaction const &, scendere::block &, scendere::signature_verification = scendere::signature_verification::unknown);
	bool rollback (scendere::write_transaction const &, scendere::block_hash const &, std::vector<std::shared_ptr<scendere::block>> &);
	bool rollback (scendere::write_transaction const &, scendere::block_hash const &);
	void update_account (scendere::write_transaction const &, scendere::account const &, scendere::account_info const &, scendere::account_info const &);
	uint64_t pruning_action (scendere::write_transaction &, scendere::block_hash const &, uint64_t const);
	void dump_account_chain (scendere::account const &, std::ostream & = std::cout);
	bool could_fit (scendere::transaction const &, scendere::block const &) const;
	bool dependents_confirmed (scendere::transaction const &, scendere::block const &) const;
	bool is_epoch_link (scendere::link const &) const;
	std::array<scendere::block_hash, 2> dependent_blocks (scendere::transaction const &, scendere::block const &) const;
	std::shared_ptr<scendere::block> find_receive_block_by_send_hash (scendere::transaction const & transaction, scendere::account const & destination, scendere::block_hash const & send_block_hash);
	scendere::account const & epoch_signer (scendere::link const &) const;
	scendere::link const & epoch_link (scendere::epoch) const;
	std::multimap<uint64_t, uncemented_info, std::greater<>> unconfirmed_frontiers () const;
	bool migrate_lmdb_to_rocksdb (boost::filesystem::path const &) const;
	static scendere::uint128_t const unit;
	scendere::ledger_constants & constants;
	scendere::store & store;
	scendere::ledger_cache cache;
	scendere::stat & stats;
	std::unordered_map<scendere::account, scendere::uint128_t> bootstrap_weights;
	std::atomic<size_t> bootstrap_weights_size{ 0 };
	uint64_t bootstrap_weight_max_blocks{ 1 };
	std::atomic<bool> check_bootstrap_weights;
	bool pruning{ false };

private:
	void initialize (scendere::generate_cache const &);
};

std::unique_ptr<container_info_component> collect_container_info (ledger & ledger, std::string const & name);
}
