#pragma once

#include <scendere/lib/config.hpp>
#include <scendere/lib/stats.hpp>
#include <scendere/lib/work.hpp>
#include <scendere/node/active_transactions.hpp>
#include <scendere/node/blockprocessor.hpp>
#include <scendere/node/bootstrap/bootstrap.hpp>
#include <scendere/node/bootstrap/bootstrap_attempt.hpp>
#include <scendere/node/bootstrap/bootstrap_server.hpp>
#include <scendere/node/confirmation_height_processor.hpp>
#include <scendere/node/distributed_work_factory.hpp>
#include <scendere/node/election.hpp>
#include <scendere/node/election_scheduler.hpp>
#include <scendere/node/gap_cache.hpp>
#include <scendere/node/network.hpp>
#include <scendere/node/node_observers.hpp>
#include <scendere/node/nodeconfig.hpp>
#include <scendere/node/online_reps.hpp>
#include <scendere/node/portmapping.hpp>
#include <scendere/node/repcrawler.hpp>
#include <scendere/node/request_aggregator.hpp>
#include <scendere/node/signatures.hpp>
#include <scendere/node/telemetry.hpp>
#include <scendere/node/unchecked_map.hpp>
#include <scendere/node/vote_processor.hpp>
#include <scendere/node/wallet.hpp>
#include <scendere/node/write_database_queue.hpp>
#include <scendere/secure/ledger.hpp>
#include <scendere/secure/utility.hpp>

#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/sequenced_index.hpp>
#include <boost/multi_index_container.hpp>
#include <boost/program_options.hpp>
#include <boost/thread/latch.hpp>

#include <atomic>
#include <memory>
#include <vector>

namespace scendere
{
namespace websocket
{
	class listener;
}
class node;
class telemetry;
class work_pool;
class block_arrival_info final
{
public:
	std::chrono::steady_clock::time_point arrival;
	scendere::block_hash hash;
};
// This class tracks blocks that are probably live because they arrived in a UDP packet
// This gives a fairly reliable way to differentiate between blocks being inserted via bootstrap or new, live blocks.
class block_arrival final
{
public:
	// Return `true' to indicated an error if the block has already been inserted
	bool add (scendere::block_hash const &);
	bool recent (scendere::block_hash const &);
	// clang-format off
	class tag_sequence {};
	class tag_hash {};
	boost::multi_index_container<scendere::block_arrival_info,
		boost::multi_index::indexed_by<
			boost::multi_index::sequenced<boost::multi_index::tag<tag_sequence>>,
			boost::multi_index::hashed_unique<boost::multi_index::tag<tag_hash>,
				boost::multi_index::member<scendere::block_arrival_info, scendere::block_hash, &scendere::block_arrival_info::hash>>>>
	arrival;
	// clang-format on
	scendere::mutex mutex{ mutex_identifier (mutexes::block_arrival) };
	static std::size_t constexpr arrival_size_min = 8 * 1024;
	static std::chrono::seconds constexpr arrival_time_min = std::chrono::seconds (300);
};

std::unique_ptr<container_info_component> collect_container_info (block_arrival & block_arrival, std::string const & name);

std::unique_ptr<container_info_component> collect_container_info (rep_crawler & rep_crawler, std::string const & name);

class node final : public std::enable_shared_from_this<scendere::node>
{
public:
	node (boost::asio::io_context &, uint16_t, boost::filesystem::path const &, scendere::logging const &, scendere::work_pool &, scendere::node_flags = scendere::node_flags (), unsigned seq = 0);
	node (boost::asio::io_context &, boost::filesystem::path const &, scendere::node_config const &, scendere::work_pool &, scendere::node_flags = scendere::node_flags (), unsigned seq = 0);
	~node ();
	template <typename T>
	void background (T action_a)
	{
		io_ctx.post (action_a);
	}
	bool copy_with_compaction (boost::filesystem::path const &);
	void keepalive (std::string const &, uint16_t);
	void start ();
	void stop ();
	std::shared_ptr<scendere::node> shared ();
	int store_version ();
	void receive_confirmed (scendere::transaction const & block_transaction_a, scendere::block_hash const & hash_a, scendere::account const & destination_a);
	void process_confirmed_data (scendere::transaction const &, std::shared_ptr<scendere::block> const &, scendere::block_hash const &, scendere::account &, scendere::uint128_t &, bool &, bool &, scendere::account &);
	void process_confirmed (scendere::election_status const &, uint64_t = 0);
	void process_active (std::shared_ptr<scendere::block> const &);
	[[nodiscard]] scendere::process_return process (scendere::block &);
	scendere::process_return process_local (std::shared_ptr<scendere::block> const &);
	void process_local_async (std::shared_ptr<scendere::block> const &);
	void keepalive_preconfigured (std::vector<std::string> const &);
	scendere::block_hash latest (scendere::account const &);
	scendere::uint128_t balance (scendere::account const &);
	std::shared_ptr<scendere::block> block (scendere::block_hash const &);
	std::pair<scendere::uint128_t, scendere::uint128_t> balance_pending (scendere::account const &, bool only_confirmed);
	scendere::uint128_t weight (scendere::account const &);
	scendere::block_hash rep_block (scendere::account const &);
	scendere::uint128_t minimum_principal_weight ();
	scendere::uint128_t minimum_principal_weight (scendere::uint128_t const &);
	void ongoing_rep_calculation ();
	void ongoing_bootstrap ();
	void ongoing_peer_store ();
	void ongoing_unchecked_cleanup ();
	void ongoing_backlog_population ();
	void backup_wallet ();
	void search_receivable_all ();
	void bootstrap_wallet ();
	void unchecked_cleanup ();
	bool collect_ledger_pruning_targets (std::deque<scendere::block_hash> &, scendere::account &, uint64_t const, uint64_t const, uint64_t const);
	void ledger_pruning (uint64_t const, bool, bool);
	void ongoing_ledger_pruning ();
	int price (scendere::uint128_t const &, int);
	// The default difficulty updates to base only when the first epoch_2 block is processed
	uint64_t default_difficulty (scendere::work_version const) const;
	uint64_t default_receive_difficulty (scendere::work_version const) const;
	uint64_t max_work_generate_difficulty (scendere::work_version const) const;
	bool local_work_generation_enabled () const;
	bool work_generation_enabled () const;
	bool work_generation_enabled (std::vector<std::pair<std::string, uint16_t>> const &) const;
	boost::optional<uint64_t> work_generate_blocking (scendere::block &, uint64_t);
	boost::optional<uint64_t> work_generate_blocking (scendere::work_version const, scendere::root const &, uint64_t, boost::optional<scendere::account> const & = boost::none);
	void work_generate (scendere::work_version const, scendere::root const &, uint64_t, std::function<void (boost::optional<uint64_t>)>, boost::optional<scendere::account> const & = boost::none, bool const = false);
	void add_initial_peers ();
	void block_confirm (std::shared_ptr<scendere::block> const &);
	bool block_confirmed (scendere::block_hash const &);
	bool block_confirmed_or_being_confirmed (scendere::transaction const &, scendere::block_hash const &);
	void do_rpc_callback (boost::asio::ip::tcp::resolver::iterator i_a, std::string const &, uint16_t, std::shared_ptr<std::string> const &, std::shared_ptr<std::string> const &, std::shared_ptr<boost::asio::ip::tcp::resolver> const &);
	void ongoing_online_weight_calculation ();
	void ongoing_online_weight_calculation_queue ();
	bool online () const;
	bool init_error () const;
	bool epoch_upgrader (scendere::raw_key const &, scendere::epoch, uint64_t, uint64_t);
	void set_bandwidth_params (std::size_t limit, double ratio);
	std::pair<uint64_t, decltype (scendere::ledger::bootstrap_weights)> get_bootstrap_weights () const;
	void populate_backlog ();
	uint64_t get_confirmation_height (scendere::transaction const &, scendere::account &);
	scendere::write_database_queue write_database_queue;
	boost::asio::io_context & io_ctx;
	boost::latch node_initialized_latch;
	scendere::node_config config;
	scendere::network_params & network_params;
	scendere::stat stats;
	scendere::thread_pool workers;
	std::shared_ptr<scendere::websocket::listener> websocket_server;
	scendere::node_flags flags;
	scendere::work_pool & work;
	scendere::distributed_work_factory distributed_work;
	scendere::logger_mt logger;
	std::unique_ptr<scendere::store> store_impl;
	scendere::store & store;
	scendere::unchecked_map unchecked;
	std::unique_ptr<scendere::wallets_store> wallets_store_impl;
	scendere::wallets_store & wallets_store;
	scendere::gap_cache gap_cache;
	scendere::ledger ledger;
	scendere::signature_checker checker;
	scendere::network network;
	std::shared_ptr<scendere::telemetry> telemetry;
	scendere::bootstrap_initiator bootstrap_initiator;
	scendere::bootstrap_listener bootstrap;
	boost::filesystem::path application_path;
	scendere::node_observers observers;
	scendere::port_mapping port_mapping;
	scendere::online_reps online_reps;
	scendere::rep_crawler rep_crawler;
	scendere::vote_processor vote_processor;
	unsigned warmed_up;
	scendere::block_processor block_processor;
	scendere::block_arrival block_arrival;
	scendere::local_vote_history history;
	scendere::keypair node_id;
	scendere::block_uniquer block_uniquer;
	scendere::vote_uniquer vote_uniquer;
	scendere::confirmation_height_processor confirmation_height_processor;
	scendere::active_transactions active;
	scendere::election_scheduler scheduler;
	scendere::request_aggregator aggregator;
	scendere::wallets wallets;
	std::chrono::steady_clock::time_point const startup_time;
	std::chrono::seconds unchecked_cutoff = std::chrono::seconds (7 * 24 * 60 * 60); // Week
	std::atomic<bool> unresponsive_work_peers{ false };
	std::atomic<bool> stopped{ false };
	static double constexpr price_max = 16.0;
	static double constexpr free_cutoff = 1024.0;
	// For tests only
	unsigned node_seq;
	// For tests only
	boost::optional<uint64_t> work_generate_blocking (scendere::block &);
	// For tests only
	boost::optional<uint64_t> work_generate_blocking (scendere::root const &, uint64_t);
	// For tests only
	boost::optional<uint64_t> work_generate_blocking (scendere::root const &);

private:
	void long_inactivity_cleanup ();
	void epoch_upgrader_impl (scendere::raw_key const &, scendere::epoch, uint64_t, uint64_t);
	scendere::locked<std::future<void>> epoch_upgrading;
};

std::unique_ptr<container_info_component> collect_container_info (node & node, std::string const & name);

scendere::node_flags const & inactive_node_flag_defaults ();

class node_wrapper final
{
public:
	node_wrapper (boost::filesystem::path const & path_a, boost::filesystem::path const & config_path_a, scendere::node_flags const & node_flags_a);
	~node_wrapper ();

	scendere::network_params network_params;
	std::shared_ptr<boost::asio::io_context> io_context;
	scendere::work_pool work;
	std::shared_ptr<scendere::node> node;
};

class inactive_node final
{
public:
	inactive_node (boost::filesystem::path const & path_a, scendere::node_flags const & node_flags_a);
	inactive_node (boost::filesystem::path const & path_a, boost::filesystem::path const & config_path_a, scendere::node_flags const & node_flags_a);

	scendere::node_wrapper node_wrapper;
	std::shared_ptr<scendere::node> node;
};
std::unique_ptr<scendere::inactive_node> default_inactive_node (boost::filesystem::path const &, boost::program_options::variables_map const &);
}
