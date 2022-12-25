#pragma once

#include <scendere/lib/locks.hpp>
#include <scendere/lib/numbers.hpp>
#include <scendere/lib/utility.hpp>
#include <scendere/node/wallet.hpp>
#include <scendere/secure/common.hpp>

#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/sequenced_index.hpp>
#include <boost/multi_index_container.hpp>

#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

namespace mi = boost::multi_index;

namespace scendere
{
class ledger;
class network;
class node_config;
class stat;
class vote_processor;
class wallets;
namespace transport
{
	class channel;
}

class vote_spacing final
{
	class entry
	{
	public:
		scendere::root root;
		std::chrono::steady_clock::time_point time;
		scendere::block_hash hash;
	};

	boost::multi_index_container<entry,
	mi::indexed_by<
	mi::hashed_non_unique<mi::tag<class tag_root>,
	mi::member<entry, scendere::root, &entry::root>>,
	mi::ordered_non_unique<mi::tag<class tag_time>,
	mi::member<entry, std::chrono::steady_clock::time_point, &entry::time>>>>
	recent;
	std::chrono::milliseconds const delay;
	void trim ();

public:
	vote_spacing (std::chrono::milliseconds const & delay) :
		delay{ delay }
	{
	}
	bool votable (scendere::root const & root_a, scendere::block_hash const & hash_a) const;
	void flag (scendere::root const & root_a, scendere::block_hash const & hash_a);
	std::size_t size () const;
};

class local_vote_history final
{
	class local_vote final
	{
	public:
		local_vote (scendere::root const & root_a, scendere::block_hash const & hash_a, std::shared_ptr<scendere::vote> const & vote_a) :
			root (root_a),
			hash (hash_a),
			vote (vote_a)
		{
		}
		scendere::root root;
		scendere::block_hash hash;
		std::shared_ptr<scendere::vote> vote;
	};

public:
	local_vote_history (scendere::voting_constants const & constants) :
		constants{ constants }
	{
	}
	void add (scendere::root const & root_a, scendere::block_hash const & hash_a, std::shared_ptr<scendere::vote> const & vote_a);
	void erase (scendere::root const & root_a);

	std::vector<std::shared_ptr<scendere::vote>> votes (scendere::root const & root_a, scendere::block_hash const & hash_a, bool const is_final_a = false) const;
	bool exists (scendere::root const &) const;
	std::size_t size () const;

private:
	// clang-format off
	boost::multi_index_container<local_vote,
	mi::indexed_by<
		mi::hashed_non_unique<mi::tag<class tag_root>,
			mi::member<local_vote, scendere::root, &local_vote::root>>,
		mi::sequenced<mi::tag<class tag_sequence>>>>
	history;
	// clang-format on

	scendere::voting_constants const & constants;
	void clean ();
	std::vector<std::shared_ptr<scendere::vote>> votes (scendere::root const & root_a) const;
	// Only used in Debug
	bool consistency_check (scendere::root const &) const;
	mutable scendere::mutex mutex;

	friend std::unique_ptr<container_info_component> collect_container_info (local_vote_history & history, std::string const & name);
	friend class local_vote_history_basic_Test;
};

std::unique_ptr<container_info_component> collect_container_info (local_vote_history & history, std::string const & name);

class vote_generator final
{
private:
	using candidate_t = std::pair<scendere::root, scendere::block_hash>;
	using request_t = std::pair<std::vector<candidate_t>, std::shared_ptr<scendere::transport::channel>>;

public:
	vote_generator (scendere::node_config const & config_a, scendere::ledger & ledger_a, scendere::wallets & wallets_a, scendere::vote_processor & vote_processor_a, scendere::local_vote_history & history_a, scendere::network & network_a, scendere::stat & stats_a, bool is_final_a);
	/** Queue items for vote generation, or broadcast votes already in cache */
	void add (scendere::root const &, scendere::block_hash const &);
	/** Queue blocks for vote generation, returning the number of successful candidates.*/
	std::size_t generate (std::vector<std::shared_ptr<scendere::block>> const & blocks_a, std::shared_ptr<scendere::transport::channel> const & channel_a);
	void set_reply_action (std::function<void (std::shared_ptr<scendere::vote> const &, std::shared_ptr<scendere::transport::channel> const &)>);
	void stop ();

private:
	void run ();
	void broadcast (scendere::unique_lock<scendere::mutex> &);
	void reply (scendere::unique_lock<scendere::mutex> &, request_t &&);
	void vote (std::vector<scendere::block_hash> const &, std::vector<scendere::root> const &, std::function<void (std::shared_ptr<scendere::vote> const &)> const &);
	void broadcast_action (std::shared_ptr<scendere::vote> const &) const;
	std::function<void (std::shared_ptr<scendere::vote> const &, std::shared_ptr<scendere::transport::channel> &)> reply_action; // must be set only during initialization by using set_reply_action
	scendere::node_config const & config;
	scendere::ledger & ledger;
	scendere::wallets & wallets;
	scendere::vote_processor & vote_processor;
	scendere::local_vote_history & history;
	scendere::vote_spacing spacing;
	scendere::network & network;
	scendere::stat & stats;
	mutable scendere::mutex mutex;
	scendere::condition_variable condition;
	static std::size_t constexpr max_requests{ 2048 };
	std::deque<request_t> requests;
	std::deque<candidate_t> candidates;
	std::atomic<bool> stopped{ false };
	bool started{ false };
	std::thread thread;
	bool is_final;

	friend std::unique_ptr<container_info_component> collect_container_info (vote_generator & vote_generator, std::string const & name);
};

std::unique_ptr<container_info_component> collect_container_info (vote_generator & generator, std::string const & name);

class vote_generator_session final
{
public:
	vote_generator_session (vote_generator & vote_generator_a);
	void add (scendere::root const &, scendere::block_hash const &);
	void flush ();

private:
	scendere::vote_generator & generator;
	std::vector<std::pair<scendere::root, scendere::block_hash>> items;
};
}
