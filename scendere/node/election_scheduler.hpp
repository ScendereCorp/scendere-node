#pragma once

#include <scendere/lib/numbers.hpp>
#include <scendere/node/active_transactions.hpp>
#include <scendere/node/prioritization.hpp>

#include <boost/optional.hpp>

#include <condition_variable>
#include <deque>
#include <memory>
#include <thread>

namespace scendere
{
class block;
class node;
class election_scheduler final
{
public:
	election_scheduler (scendere::node & node);
	~election_scheduler ();
	// Manualy start an election for a block
	// Call action with confirmed block, may be different than what we started with
	void manual (std::shared_ptr<scendere::block> const &, boost::optional<scendere::uint128_t> const & = boost::none, scendere::election_behavior = scendere::election_behavior::normal, std::function<void (std::shared_ptr<scendere::block> const &)> const & = nullptr);
	// Activates the first unconfirmed block of \p account_a
	void activate (scendere::account const &, scendere::transaction const &);
	void stop ();
	// Blocks until no more elections can be activated or there are no more elections to activate
	void flush ();
	void notify ();
	std::size_t size () const;
	bool empty () const;
	std::size_t priority_queue_size () const;
	std::unique_ptr<container_info_component> collect_container_info (std::string const &);

private:
	void run ();
	bool empty_locked () const;
	bool priority_queue_predicate () const;
	bool manual_queue_predicate () const;
	bool overfill_predicate () const;
	scendere::prioritization priority;
	std::deque<std::tuple<std::shared_ptr<scendere::block>, boost::optional<scendere::uint128_t>, scendere::election_behavior, std::function<void (std::shared_ptr<scendere::block>)>>> manual_queue;
	scendere::node & node;
	bool stopped;
	scendere::condition_variable condition;
	mutable scendere::mutex mutex;
	std::thread thread;
};
}