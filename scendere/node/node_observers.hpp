#pragma once

#include <scendere/lib/numbers.hpp>
#include <scendere/lib/utility.hpp>
#include <scendere/node/active_transactions.hpp>
#include <scendere/node/transport/transport.hpp>

namespace scendere
{
class telemetry;
class node_observers final
{
public:
	using blocks_t = scendere::observer_set<scendere::election_status const &, std::vector<scendere::vote_with_weight_info> const &, scendere::account const &, scendere::uint128_t const &, bool, bool>;
	blocks_t blocks;
	scendere::observer_set<bool> wallet;
	scendere::observer_set<std::shared_ptr<scendere::vote>, std::shared_ptr<scendere::transport::channel>, scendere::vote_code> vote;
	scendere::observer_set<scendere::block_hash const &> active_stopped;
	scendere::observer_set<scendere::account const &, bool> account_balance;
	scendere::observer_set<std::shared_ptr<scendere::transport::channel>> endpoint;
	scendere::observer_set<> disconnect;
	scendere::observer_set<scendere::root const &> work_cancel;
	scendere::observer_set<scendere::telemetry_data const &, scendere::endpoint const &> telemetry;
};

std::unique_ptr<container_info_component> collect_container_info (node_observers & node_observers, std::string const & name);
}
