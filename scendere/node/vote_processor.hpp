#pragma once

#include <scendere/lib/numbers.hpp>
#include <scendere/lib/utility.hpp>
#include <scendere/secure/common.hpp>

#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_set>

namespace scendere
{
class signature_checker;
class active_transactions;
class store;
class node_observers;
class stats;
class node_config;
class logger_mt;
class online_reps;
class rep_crawler;
class ledger;
class network_params;
class node_flags;
class stat;

class transaction;
namespace transport
{
	class channel;
}

class vote_processor final
{
public:
	explicit vote_processor (scendere::signature_checker & checker_a, scendere::active_transactions & active_a, scendere::node_observers & observers_a, scendere::stat & stats_a, scendere::node_config & config_a, scendere::node_flags & flags_a, scendere::logger_mt & logger_a, scendere::online_reps & online_reps_a, scendere::rep_crawler & rep_crawler_a, scendere::ledger & ledger_a, scendere::network_params & network_params_a);
	/** Returns false if the vote was processed */
	bool vote (std::shared_ptr<scendere::vote> const &, std::shared_ptr<scendere::transport::channel> const &);
	/** Note: node.active.mutex lock is required */
	scendere::vote_code vote_blocking (std::shared_ptr<scendere::vote> const &, std::shared_ptr<scendere::transport::channel> const &, bool = false);
	void verify_votes (std::deque<std::pair<std::shared_ptr<scendere::vote>, std::shared_ptr<scendere::transport::channel>>> const &);
	void flush ();
	/** Block until the currently active processing cycle finishes */
	void flush_active ();
	std::size_t size ();
	bool empty ();
	bool half_full ();
	void calculate_weights ();
	void stop ();
	std::atomic<uint64_t> total_processed{ 0 };

private:
	void process_loop ();

	scendere::signature_checker & checker;
	scendere::active_transactions & active;
	scendere::node_observers & observers;
	scendere::stat & stats;
	scendere::node_config & config;
	scendere::logger_mt & logger;
	scendere::online_reps & online_reps;
	scendere::rep_crawler & rep_crawler;
	scendere::ledger & ledger;
	scendere::network_params & network_params;
	std::size_t max_votes;
	std::deque<std::pair<std::shared_ptr<scendere::vote>, std::shared_ptr<scendere::transport::channel>>> votes;
	/** Representatives levels for random early detection */
	std::unordered_set<scendere::account> representatives_1;
	std::unordered_set<scendere::account> representatives_2;
	std::unordered_set<scendere::account> representatives_3;
	scendere::condition_variable condition;
	scendere::mutex mutex{ mutex_identifier (mutexes::vote_processor) };
	bool started;
	bool stopped;
	bool is_active;
	std::thread thread;

	friend std::unique_ptr<container_info_component> collect_container_info (vote_processor & vote_processor, std::string const & name);
	friend class vote_processor_weights_Test;
};

std::unique_ptr<container_info_component> collect_container_info (vote_processor & vote_processor, std::string const & name);
}
