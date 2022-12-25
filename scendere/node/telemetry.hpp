#pragma once

#include <scendere/lib/utility.hpp>
#include <scendere/node/common.hpp>
#include <scendere/secure/common.hpp>

#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index_container.hpp>

#include <functional>
#include <memory>

namespace mi = boost::multi_index;

namespace scendere
{
class network;
class stat;
class ledger;
class thread_pool;
class unchecked_map;
namespace transport
{
	class channel;
}

/*
 * Holds a response from a telemetry request
 */
class telemetry_data_response
{
public:
	scendere::telemetry_data telemetry_data;
	scendere::endpoint endpoint;
	bool error{ true };
};

class telemetry_info final
{
public:
	telemetry_info () = default;
	telemetry_info (scendere::endpoint const & endpoint, scendere::telemetry_data const & data, std::chrono::steady_clock::time_point last_response, bool undergoing_request);
	bool awaiting_first_response () const;

	scendere::endpoint endpoint;
	scendere::telemetry_data data;
	std::chrono::steady_clock::time_point last_response;
	bool undergoing_request{ false };
	uint64_t round{ 0 };
};

/*
 * This class requests node telemetry metrics from peers and invokes any callbacks which have been aggregated.
 * All calls to get_metrics return cached data, it does not do any requests, these are periodically done in
 * ongoing_req_all_peers. This can be disabled with the disable_ongoing_telemetry_requests node flag.
 * Calls to get_metrics_single_peer_async will wait until a response is made if it is not within the cache
 * cut off.
 */
class telemetry : public std::enable_shared_from_this<telemetry>
{
public:
	telemetry (scendere::network &, scendere::thread_pool &, scendere::observer_set<scendere::telemetry_data const &, scendere::endpoint const &> &, scendere::stat &, scendere::network_params &, bool);
	void start ();
	void stop ();

	/*
	 * Received telemetry metrics from this peer
	 */
	void set (scendere::telemetry_ack const &, scendere::transport::channel const &);

	/*
	 * This returns what ever is in the cache
	 */
	std::unordered_map<scendere::endpoint, scendere::telemetry_data> get_metrics ();

	/*
	 * This makes a telemetry request to the specific channel.
	 * Error is set for: no response received, no payload received, invalid signature or unsound metrics in message (e.g different genesis block) 
	 */
	void get_metrics_single_peer_async (std::shared_ptr<scendere::transport::channel> const &, std::function<void (telemetry_data_response const &)> const &);

	/*
	 * A blocking version of get_metrics_single_peer_async
	 */
	telemetry_data_response get_metrics_single_peer (std::shared_ptr<scendere::transport::channel> const &);

	/*
	 * Return the number of node metrics collected
	 */
	std::size_t telemetry_data_size ();

	/*
	 * Returns the time for the cache, response and a small buffer for alarm operations to be scheduled and completed
	 */
	std::chrono::milliseconds cache_plus_buffer_cutoff_time () const;

private:
	class tag_endpoint
	{
	};
	class tag_last_updated
	{
	};

	scendere::network & network;
	scendere::thread_pool & workers;
	scendere::observer_set<scendere::telemetry_data const &, scendere::endpoint const &> & observers;
	scendere::stat & stats;
	/* Important that this is a reference to the node network_params for tests which want to modify genesis block */
	scendere::network_params & network_params;
	bool disable_ongoing_requests;

	std::atomic<bool> stopped{ false };

	scendere::mutex mutex{ mutex_identifier (mutexes::telemetry) };
	// clang-format off
	// This holds the last telemetry data received from peers or can be a placeholder awaiting the first response (check with awaiting_first_response ())
	boost::multi_index_container<scendere::telemetry_info,
	mi::indexed_by<
		mi::hashed_unique<mi::tag<tag_endpoint>,
			mi::member<scendere::telemetry_info, scendere::endpoint, &scendere::telemetry_info::endpoint>>,
		mi::ordered_non_unique<mi::tag<tag_last_updated>,
			mi::member<scendere::telemetry_info, std::chrono::steady_clock::time_point, &scendere::telemetry_info::last_response>>>> recent_or_initial_request_telemetry_data;
	// clang-format on

	// Anything older than this requires requesting metrics from other nodes.
	std::chrono::seconds const cache_cutoff{ scendere::telemetry_cache_cutoffs::network_to_time (network_params.network) };

	// The maximum time spent waiting for a response to a telemetry request
	std::chrono::seconds const response_time_cutoff{ network_params.network.is_dev_network () ? (is_sanitizer_build || scendere::running_within_valgrind () ? 6 : 3) : 10 };

	std::unordered_map<scendere::endpoint, std::vector<std::function<void (telemetry_data_response const &)>>> callbacks;

	void ongoing_req_all_peers (std::chrono::milliseconds);

	void fire_request_message (std::shared_ptr<scendere::transport::channel> const &);
	void channel_processed (scendere::endpoint const &, bool);
	void flush_callbacks_async (scendere::endpoint const &, bool);
	void invoke_callbacks (scendere::endpoint const &, bool);

	bool within_cache_cutoff (scendere::telemetry_info const &) const;
	bool within_cache_plus_buffer_cutoff (telemetry_info const &) const;
	bool verify_message (scendere::telemetry_ack const &, scendere::transport::channel const &);
	friend std::unique_ptr<scendere::container_info_component> collect_container_info (telemetry &, std::string const &);
	friend class telemetry_remove_peer_invalid_signature_Test;
};

std::unique_ptr<scendere::container_info_component> collect_container_info (telemetry & telemetry, std::string const & name);

scendere::telemetry_data consolidate_telemetry_data (std::vector<telemetry_data> const & telemetry_data);
scendere::telemetry_data local_telemetry_data (scendere::ledger const & ledger_a, scendere::network &, scendere::unchecked_map const &, uint64_t, scendere::network_params const &, std::chrono::steady_clock::time_point, uint64_t, scendere::keypair const &);
}
