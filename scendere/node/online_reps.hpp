#pragma once

#include <scendere/lib/numbers.hpp>
#include <scendere/lib/utility.hpp>
#include <scendere/secure/common.hpp>

#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index_container.hpp>

#include <memory>
#include <unordered_set>
#include <vector>

namespace scendere
{
class ledger;
class node_config;
class transaction;

/** Track online representatives and trend online weight */
class online_reps final
{
public:
	online_reps (scendere::ledger & ledger_a, scendere::node_config const & config_a);
	/** Add voting account \p rep_account to the set of online representatives */
	void observe (scendere::account const & rep_account);
	/** Called periodically to sample online weight */
	void sample ();
	/** Returns the trended online stake */
	scendere::uint128_t trended () const;
	/** Returns the current online stake */
	scendere::uint128_t online () const;
	/** Returns the quorum required for confirmation*/
	scendere::uint128_t delta () const;
	/** List of online representatives, both the currently sampling ones and the ones observed in the previous sampling period */
	std::vector<scendere::account> list ();
	void clear ();
	static unsigned constexpr online_weight_quorum = 34; //TO-CHANGE

private:
	class rep_info
	{
	public:
		std::chrono::steady_clock::time_point time;
		scendere::account account;
	};
	class tag_time
	{
	};
	class tag_account
	{
	};
	scendere::uint128_t calculate_trend (scendere::transaction &) const;
	scendere::uint128_t calculate_online () const;
	mutable scendere::mutex mutex;
	scendere::ledger & ledger;
	scendere::node_config const & config;
	boost::multi_index_container<rep_info,
	boost::multi_index::indexed_by<
	boost::multi_index::ordered_non_unique<boost::multi_index::tag<tag_time>,
	boost::multi_index::member<rep_info, std::chrono::steady_clock::time_point, &rep_info::time>>,
	boost::multi_index::hashed_unique<boost::multi_index::tag<tag_account>,
	boost::multi_index::member<rep_info, scendere::account, &rep_info::account>>>>
	reps;
	scendere::uint128_t trended_m;
	scendere::uint128_t online_m;
	scendere::uint128_t minimum;

	friend class election_quorum_minimum_update_weight_before_quorum_checks_Test;
	friend std::unique_ptr<container_info_component> collect_container_info (online_reps & online_reps, std::string const & name);
};

std::unique_ptr<container_info_component> collect_container_info (online_reps & online_reps, std::string const & name);
}