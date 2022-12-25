#pragma once

#include <scendere/lib/numbers.hpp>
#include <scendere/node/election.hpp>
#include <scendere/node/inactive_cache_information.hpp>
#include <scendere/node/inactive_cache_status.hpp>
#include <scendere/node/voting.hpp>
#include <scendere/secure/common.hpp>

#include <boost/circular_buffer.hpp>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/random_access_index.hpp>
#include <boost/multi_index/sequenced_index.hpp>
#include <boost/multi_index_container.hpp>
#include <boost/optional.hpp>
#include <boost/thread/thread.hpp>

#include <atomic>
#include <condition_variable>
#include <deque>
#include <memory>
#include <unordered_map>
#include <unordered_set>

namespace mi = boost::multi_index;

namespace scendere
{
class node;
class block;
class block_sideband;
class election;
class election_scheduler;
class vote;
class transaction;
class confirmation_height_processor;
class stat;

class cementable_account final
{
public:
	cementable_account (scendere::account const & account_a, std::size_t blocks_uncemented_a);
	scendere::account account;
	uint64_t blocks_uncemented{ 0 };
};

class election_timepoint final
{
public:
	std::chrono::steady_clock::time_point time;
	scendere::qualified_root root;
};

class expired_optimistic_election_info final
{
public:
	expired_optimistic_election_info (std::chrono::steady_clock::time_point, scendere::account);

	std::chrono::steady_clock::time_point expired_time;
	scendere::account account;
	bool election_started{ false };
};

class frontiers_confirmation_info
{
public:
	bool can_start_elections () const;

	std::size_t max_elections{ 0 };
	bool aggressive_mode{ false };
};

class election_insertion_result final
{
public:
	std::shared_ptr<scendere::election> election;
	bool inserted{ false };
};

// Core class for determining consensus
// Holds all active blocks i.e. recently added blocks that need confirmation
class active_transactions final
{
	class conflict_info final
	{
	public:
		scendere::qualified_root root;
		std::shared_ptr<scendere::election> election;
		scendere::epoch epoch;
		scendere::uint128_t previous_balance;
	};

	friend class scendere::election;

	// clang-format off
	class tag_account {};
	class tag_random_access {};
	class tag_root {};
	class tag_sequence {};
	class tag_uncemented {};
	class tag_arrival {};
	class tag_hash {};
	class tag_expired_time {};
	class tag_election_started {};
	// clang-format on

public:
	// clang-format off
	using ordered_roots = boost::multi_index_container<conflict_info,
	mi::indexed_by<
		mi::random_access<mi::tag<tag_random_access>>,
		mi::hashed_unique<mi::tag<tag_root>,
			mi::member<conflict_info, scendere::qualified_root, &conflict_info::root>>>>;
	// clang-format on
	ordered_roots roots;
	using roots_iterator = active_transactions::ordered_roots::index_iterator<tag_root>::type;

	explicit active_transactions (scendere::node &, scendere::confirmation_height_processor &);
	~active_transactions ();
	// Distinguishes replay votes, cannot be determined if the block is not in any election
	scendere::vote_code vote (std::shared_ptr<scendere::vote> const &);
	// Is the root of this block in the roots container
	bool active (scendere::block const &);
	bool active (scendere::qualified_root const &);
	std::shared_ptr<scendere::election> election (scendere::qualified_root const &) const;
	std::shared_ptr<scendere::block> winner (scendere::block_hash const &) const;
	// Returns a list of elections sorted by difficulty
	std::vector<std::shared_ptr<scendere::election>> list_active (std::size_t = std::numeric_limits<std::size_t>::max ());
	void erase (scendere::block const &);
	void erase_hash (scendere::block_hash const &);
	void erase_oldest ();
	bool empty ();
	std::size_t size ();
	void stop ();
	bool publish (std::shared_ptr<scendere::block> const &);
	boost::optional<scendere::election_status_type> confirm_block (scendere::transaction const &, std::shared_ptr<scendere::block> const &);
	void block_cemented_callback (std::shared_ptr<scendere::block> const &);
	void block_already_cemented_callback (scendere::block_hash const &);

	int64_t vacancy () const;
	std::function<void ()> vacancy_update{ [] () {} };

	std::unordered_map<scendere::block_hash, std::shared_ptr<scendere::election>> blocks;
	std::deque<scendere::election_status> list_recently_cemented ();
	std::deque<scendere::election_status> recently_cemented;

	void add_recently_cemented (scendere::election_status const &);
	void add_recently_confirmed (scendere::qualified_root const &, scendere::block_hash const &);
	void erase_recently_confirmed (scendere::block_hash const &);
	void add_inactive_votes_cache (scendere::unique_lock<scendere::mutex> &, scendere::block_hash const &, scendere::account const &, uint64_t const);
	// Inserts an election if conditions are met
	void trigger_inactive_votes_cache_election (std::shared_ptr<scendere::block> const &);
	scendere::inactive_cache_information find_inactive_votes_cache (scendere::block_hash const &);
	void erase_inactive_votes_cache (scendere::block_hash const &);
	scendere::election_scheduler & scheduler;
	scendere::confirmation_height_processor & confirmation_height_processor;
	scendere::node & node;
	mutable scendere::mutex mutex{ mutex_identifier (mutexes::active) };
	std::size_t priority_cementable_frontiers_size ();
	std::size_t priority_wallet_cementable_frontiers_size ();
	std::size_t inactive_votes_cache_size ();
	std::size_t election_winner_details_size ();
	void add_election_winner_details (scendere::block_hash const &, std::shared_ptr<scendere::election> const &);
	void remove_election_winner_details (scendere::block_hash const &);

	scendere::vote_generator generator;
	scendere::vote_generator final_generator;

#ifdef MEMORY_POOL_DISABLED
	using allocator = std::allocator<scendere::inactive_cache_information>;
#else
	using allocator = boost::fast_pool_allocator<scendere::inactive_cache_information>;
#endif

	// clang-format off
	using ordered_cache = boost::multi_index_container<scendere::inactive_cache_information,
	mi::indexed_by<
		mi::ordered_non_unique<mi::tag<tag_arrival>,
			mi::member<scendere::inactive_cache_information, std::chrono::steady_clock::time_point, &scendere::inactive_cache_information::arrival>>,
		mi::hashed_unique<mi::tag<tag_hash>,
			mi::member<scendere::inactive_cache_information, scendere::block_hash, &scendere::inactive_cache_information::hash>>>, allocator>;
	// clang-format on

private:
	scendere::mutex election_winner_details_mutex{ mutex_identifier (mutexes::election_winner_details) };

	std::unordered_map<scendere::block_hash, std::shared_ptr<scendere::election>> election_winner_details;

	// Call action with confirmed block, may be different than what we started with
	// clang-format off
	scendere::election_insertion_result insert_impl (scendere::unique_lock<scendere::mutex> &, std::shared_ptr<scendere::block> const&, boost::optional<scendere::uint128_t> const & = boost::none, scendere::election_behavior = scendere::election_behavior::normal, std::function<void(std::shared_ptr<scendere::block>const&)> const & = nullptr);
	// clang-format on
	void request_loop ();
	void request_confirm (scendere::unique_lock<scendere::mutex> &);
	void erase (scendere::qualified_root const &);
	// Erase all blocks from active and, if not confirmed, clear digests from network filters
	void cleanup_election (scendere::unique_lock<scendere::mutex> & lock_a, scendere::election const &);
	// Returns a list of elections sorted by difficulty, mutex must be locked
	std::vector<std::shared_ptr<scendere::election>> list_active_impl (std::size_t) const;

	scendere::condition_variable condition;
	bool started{ false };
	std::atomic<bool> stopped{ false };

	// Maximum time an election can be kept active if it is extending the container
	std::chrono::seconds const election_time_to_live;

	static std::size_t constexpr recently_confirmed_size{ 65536 };
	using recent_confirmation = std::pair<scendere::qualified_root, scendere::block_hash>;
	// clang-format off
	boost::multi_index_container<recent_confirmation,
	mi::indexed_by<
		mi::sequenced<mi::tag<tag_sequence>>,
		mi::hashed_unique<mi::tag<tag_root>,
			mi::member<recent_confirmation, scendere::qualified_root, &recent_confirmation::first>>,
		mi::hashed_unique<mi::tag<tag_hash>,
			mi::member<recent_confirmation, scendere::block_hash, &recent_confirmation::second>>>>
	recently_confirmed;
	using prioritize_num_uncemented = boost::multi_index_container<scendere::cementable_account,
	mi::indexed_by<
		mi::hashed_unique<mi::tag<tag_account>,
			mi::member<scendere::cementable_account, scendere::account, &scendere::cementable_account::account>>,
		mi::ordered_non_unique<mi::tag<tag_uncemented>,
			mi::member<scendere::cementable_account, uint64_t, &scendere::cementable_account::blocks_uncemented>,
			std::greater<uint64_t>>>>;

	boost::multi_index_container<scendere::expired_optimistic_election_info,
	mi::indexed_by<
		mi::ordered_non_unique<mi::tag<tag_expired_time>,
			mi::member<expired_optimistic_election_info, std::chrono::steady_clock::time_point, &expired_optimistic_election_info::expired_time>>,
		mi::hashed_unique<mi::tag<tag_account>,
			mi::member<expired_optimistic_election_info, scendere::account, &expired_optimistic_election_info::account>>,
		mi::ordered_non_unique<mi::tag<tag_election_started>,
			mi::member<expired_optimistic_election_info, bool, &expired_optimistic_election_info::election_started>, std::greater<bool>>>>
	expired_optimistic_election_infos;
	// clang-format on
	std::atomic<uint64_t> expired_optimistic_election_infos_size{ 0 };

	// Frontiers confirmation
	scendere::frontiers_confirmation_info get_frontiers_confirmation_info ();
	void confirm_prioritized_frontiers (scendere::transaction const &, uint64_t, uint64_t &);
	void confirm_expired_frontiers_pessimistically (scendere::transaction const &, uint64_t, uint64_t &);
	void frontiers_confirmation (scendere::unique_lock<scendere::mutex> &);
	bool insert_election_from_frontiers_confirmation (std::shared_ptr<scendere::block> const &, scendere::account const &, scendere::uint128_t, scendere::election_behavior);
	scendere::account next_frontier_account{};
	std::chrono::steady_clock::time_point next_frontier_check{ std::chrono::steady_clock::now () };
	constexpr static std::size_t max_active_elections_frontier_insertion{ 1000 };
	prioritize_num_uncemented priority_wallet_cementable_frontiers;
	prioritize_num_uncemented priority_cementable_frontiers;
	std::unordered_set<scendere::wallet_id> wallet_ids_already_iterated;
	std::unordered_map<scendere::wallet_id, scendere::account> next_wallet_id_accounts;
	bool skip_wallets{ false };
	std::atomic<unsigned> optimistic_elections_count{ 0 };
	void prioritize_frontiers_for_confirmation (scendere::transaction const &, std::chrono::milliseconds, std::chrono::milliseconds);
	bool prioritize_account_for_confirmation (prioritize_num_uncemented &, std::size_t &, scendere::account const &, scendere::account_info const &, uint64_t);
	unsigned max_optimistic ();
	void set_next_frontier_check (bool);
	void add_expired_optimistic_election (scendere::election const &);
	bool should_do_frontiers_confirmation () const;
	static std::size_t constexpr max_priority_cementable_frontiers{ 100000 };
	static std::size_t constexpr confirmed_frontiers_max_pending_size{ 10000 };
	static std::chrono::minutes constexpr expired_optimistic_election_info_cutoff{ 30 };
	ordered_cache inactive_votes_cache;
	scendere::inactive_cache_status inactive_votes_bootstrap_check (scendere::unique_lock<scendere::mutex> &, std::vector<std::pair<scendere::account, uint64_t>> const &, scendere::block_hash const &, scendere::inactive_cache_status const &);
	scendere::inactive_cache_status inactive_votes_bootstrap_check (scendere::unique_lock<scendere::mutex> &, scendere::account const &, scendere::block_hash const &, scendere::inactive_cache_status const &);
	scendere::inactive_cache_status inactive_votes_bootstrap_check_impl (scendere::unique_lock<scendere::mutex> &, scendere::uint128_t const &, std::size_t, scendere::block_hash const &, scendere::inactive_cache_status const &);
	scendere::inactive_cache_information find_inactive_votes_cache_impl (scendere::block_hash const &);
	boost::thread thread;

	friend class election;
	friend class election_scheduler;
	friend std::unique_ptr<container_info_component> collect_container_info (active_transactions &, std::string const &);

	friend class active_transactions_vote_replays_Test;
	friend class frontiers_confirmation_prioritize_frontiers_Test;
	friend class frontiers_confirmation_prioritize_frontiers_max_optimistic_elections_Test;
	friend class confirmation_height_prioritize_frontiers_overwrite_Test;
	friend class active_transactions_confirmation_consistency_Test;
	friend class node_deferred_dependent_elections_Test;
	friend class active_transactions_pessimistic_elections_Test;
	friend class frontiers_confirmation_expired_optimistic_elections_removal_Test;
};

bool purge_singleton_inactive_votes_cache_pool_memory ();
std::unique_ptr<container_info_component> collect_container_info (active_transactions & active_transactions, std::string const & name);
}
