#pragma once

#include <scendere/lib/numbers.hpp>
#include <scendere/node/inactive_cache_status.hpp>

#include <chrono>

namespace scendere
{
class inactive_cache_information final
{
public:
	inactive_cache_information () = default;
	inactive_cache_information (std::chrono::steady_clock::time_point arrival, scendere::block_hash hash, scendere::account initial_rep_a, uint64_t initial_timestamp_a, scendere::inactive_cache_status status) :
		arrival (arrival),
		hash (hash),
		status (status)
	{
		voters.reserve (8);
		voters.emplace_back (initial_rep_a, initial_timestamp_a);
	}

	std::chrono::steady_clock::time_point arrival;
	scendere::block_hash hash;
	scendere::inactive_cache_status status;
	std::vector<std::pair<scendere::account, uint64_t>> voters;

	bool needs_eval () const
	{
		return !status.bootstrap_started || !status.election_started || !status.confirmed;
	}

	std::string to_string () const;
};

}
