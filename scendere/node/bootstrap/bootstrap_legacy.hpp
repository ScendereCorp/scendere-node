#pragma once

#include <scendere/node/bootstrap/bootstrap_attempt.hpp>

#include <boost/property_tree/ptree_fwd.hpp>

#include <atomic>
#include <deque>
#include <memory>
#include <vector>

namespace scendere
{
class node;

/**
 * Legacy bootstrap session. This is made up of 3 phases: frontier requests, bootstrap pulls, bootstrap pushes.
 */
class bootstrap_attempt_legacy : public bootstrap_attempt
{
public:
	explicit bootstrap_attempt_legacy (std::shared_ptr<scendere::node> const & node_a, uint64_t const incremental_id_a, std::string const & id_a, uint32_t const frontiers_age_a, scendere::account const & start_account_a);
	void run () override;
	bool consume_future (std::future<bool> &);
	void stop () override;
	bool request_frontier (scendere::unique_lock<scendere::mutex> &, bool = false);
	void request_push (scendere::unique_lock<scendere::mutex> &);
	void add_frontier (scendere::pull_info const &) override;
	void add_bulk_push_target (scendere::block_hash const &, scendere::block_hash const &) override;
	bool request_bulk_push_target (std::pair<scendere::block_hash, scendere::block_hash> &) override;
	void set_start_account (scendere::account const &) override;
	void run_start (scendere::unique_lock<scendere::mutex> &);
	void get_information (boost::property_tree::ptree &) override;
	scendere::tcp_endpoint endpoint_frontier_request;
	std::weak_ptr<scendere::frontier_req_client> frontiers;
	std::weak_ptr<scendere::bulk_push_client> push;
	std::deque<scendere::pull_info> frontier_pulls;
	std::vector<std::pair<scendere::block_hash, scendere::block_hash>> bulk_push_targets;
	scendere::account start_account{};
	std::atomic<unsigned> account_count{ 0 };
	uint32_t frontiers_age;
};
}
