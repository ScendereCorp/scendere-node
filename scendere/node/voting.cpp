#include <scendere/lib/stats.hpp>
#include <scendere/lib/threading.hpp>
#include <scendere/lib/utility.hpp>
#include <scendere/node/network.hpp>
#include <scendere/node/nodeconfig.hpp>
#include <scendere/node/vote_processor.hpp>
#include <scendere/node/voting.hpp>
#include <scendere/node/wallet.hpp>
#include <scendere/secure/ledger.hpp>
#include <scendere/secure/store.hpp>

#include <chrono>

void scendere::vote_spacing::trim ()
{
	recent.get<tag_time> ().erase (recent.get<tag_time> ().begin (), recent.get<tag_time> ().upper_bound (std::chrono::steady_clock::now () - delay));
}

bool scendere::vote_spacing::votable (scendere::root const & root_a, scendere::block_hash const & hash_a) const
{
	bool result = true;
	for (auto range = recent.get<tag_root> ().equal_range (root_a); result && range.first != range.second; ++range.first)
	{
		auto & item = *range.first;
		result = hash_a == item.hash || item.time < std::chrono::steady_clock::now () - delay;
	}
	return result;
}

void scendere::vote_spacing::flag (scendere::root const & root_a, scendere::block_hash const & hash_a)
{
	trim ();
	auto now = std::chrono::steady_clock::now ();
	auto existing = recent.get<tag_root> ().find (root_a);
	if (existing != recent.end ())
	{
		recent.get<tag_root> ().modify (existing, [now] (entry & entry) {
			entry.time = now;
		});
	}
	else
	{
		recent.insert ({ root_a, now, hash_a });
	}
}

std::size_t scendere::vote_spacing::size () const
{
	return recent.size ();
}

bool scendere::local_vote_history::consistency_check (scendere::root const & root_a) const
{
	auto & history_by_root (history.get<tag_root> ());
	auto const range (history_by_root.equal_range (root_a));
	// All cached votes for a root must be for the same hash, this is actively enforced in local_vote_history::add
	auto consistent_same = std::all_of (range.first, range.second, [hash = range.first->hash] (auto const & info_a) { return info_a.hash == hash; });
	std::vector<scendere::account> accounts;
	std::transform (range.first, range.second, std::back_inserter (accounts), [] (auto const & info_a) { return info_a.vote->account; });
	std::sort (accounts.begin (), accounts.end ());
	// All cached votes must be unique by account, this is actively enforced in local_vote_history::add
	auto consistent_unique = accounts.size () == std::unique (accounts.begin (), accounts.end ()) - accounts.begin ();
	auto result = consistent_same && consistent_unique;
	debug_assert (result);
	return result;
}

void scendere::local_vote_history::add (scendere::root const & root_a, scendere::block_hash const & hash_a, std::shared_ptr<scendere::vote> const & vote_a)
{
	scendere::lock_guard<scendere::mutex> guard (mutex);
	clean ();
	auto add_vote (true);
	auto & history_by_root (history.get<tag_root> ());
	// Erase any vote that is not for this hash, or duplicate by account, and if new timestamp is higher
	auto range (history_by_root.equal_range (root_a));
	for (auto i (range.first); i != range.second;)
	{
		if (i->hash != hash_a || (vote_a->account == i->vote->account && i->vote->timestamp () <= vote_a->timestamp ()))
		{
			i = history_by_root.erase (i);
		}
		else if (vote_a->account == i->vote->account && i->vote->timestamp () > vote_a->timestamp ())
		{
			add_vote = false;
			++i;
		}
		else
		{
			++i;
		}
	}
	// Do not add new vote to cache if representative account is same and timestamp is lower
	if (add_vote)
	{
		auto result (history_by_root.emplace (root_a, hash_a, vote_a));
		(void)result;
		debug_assert (result.second);
	}
	debug_assert (consistency_check (root_a));
}

void scendere::local_vote_history::erase (scendere::root const & root_a)
{
	scendere::lock_guard<scendere::mutex> guard (mutex);
	auto & history_by_root (history.get<tag_root> ());
	auto range (history_by_root.equal_range (root_a));
	history_by_root.erase (range.first, range.second);
}

std::vector<std::shared_ptr<scendere::vote>> scendere::local_vote_history::votes (scendere::root const & root_a) const
{
	scendere::lock_guard<scendere::mutex> guard (mutex);
	std::vector<std::shared_ptr<scendere::vote>> result;
	auto range (history.get<tag_root> ().equal_range (root_a));
	std::transform (range.first, range.second, std::back_inserter (result), [] (auto const & entry) { return entry.vote; });
	return result;
}

std::vector<std::shared_ptr<scendere::vote>> scendere::local_vote_history::votes (scendere::root const & root_a, scendere::block_hash const & hash_a, bool const is_final_a) const
{
	scendere::lock_guard<scendere::mutex> guard (mutex);
	std::vector<std::shared_ptr<scendere::vote>> result;
	auto range (history.get<tag_root> ().equal_range (root_a));
	// clang-format off
	scendere::transform_if (range.first, range.second, std::back_inserter (result),
		[&hash_a, is_final_a](auto const & entry) { return entry.hash == hash_a && (!is_final_a || entry.vote->timestamp () == std::numeric_limits<uint64_t>::max ()); },
		[](auto const & entry) { return entry.vote; });
	// clang-format on
	return result;
}

bool scendere::local_vote_history::exists (scendere::root const & root_a) const
{
	scendere::lock_guard<scendere::mutex> guard (mutex);
	return history.get<tag_root> ().find (root_a) != history.get<tag_root> ().end ();
}

void scendere::local_vote_history::clean ()
{
	debug_assert (constants.max_cache > 0);
	auto & history_by_sequence (history.get<tag_sequence> ());
	while (history_by_sequence.size () > constants.max_cache)
	{
		history_by_sequence.erase (history_by_sequence.begin ());
	}
}

std::size_t scendere::local_vote_history::size () const
{
	scendere::lock_guard<scendere::mutex> guard (mutex);
	return history.size ();
}

std::unique_ptr<scendere::container_info_component> scendere::collect_container_info (scendere::local_vote_history & history, std::string const & name)
{
	std::size_t history_count = history.size ();
	auto sizeof_element = sizeof (decltype (history.history)::value_type);
	auto composite = std::make_unique<container_info_composite> (name);
	/* This does not currently loop over each element inside the cache to get the sizes of the votes inside history*/
	composite->add_component (std::make_unique<container_info_leaf> (container_info{ "history", history_count, sizeof_element }));
	return composite;
}

scendere::vote_generator::vote_generator (scendere::node_config const & config_a, scendere::ledger & ledger_a, scendere::wallets & wallets_a, scendere::vote_processor & vote_processor_a, scendere::local_vote_history & history_a, scendere::network & network_a, scendere::stat & stats_a, bool is_final_a) :
	config (config_a),
	ledger (ledger_a),
	wallets (wallets_a),
	vote_processor (vote_processor_a),
	history (history_a),
	spacing{ config_a.network_params.voting.delay },
	network (network_a),
	stats (stats_a),
	thread ([this] () { run (); }),
	is_final (is_final_a)
{
	scendere::unique_lock<scendere::mutex> lock (mutex);
	condition.wait (lock, [&started = started] { return started; });
}

void scendere::vote_generator::add (scendere::root const & root_a, scendere::block_hash const & hash_a)
{
	auto cached_votes (history.votes (root_a, hash_a, is_final));
	if (!cached_votes.empty ())
	{
		for (auto const & vote : cached_votes)
		{
			broadcast_action (vote);
		}
	}
	else
	{
		auto should_vote (false);
		if (is_final)
		{
			auto transaction (ledger.store.tx_begin_write ({ tables::final_votes }));
			auto block (ledger.store.block.get (transaction, hash_a));
			should_vote = block != nullptr && ledger.dependents_confirmed (transaction, *block) && ledger.store.final_vote.put (transaction, block->qualified_root (), hash_a);
			debug_assert (block == nullptr || root_a == block->root ());
		}
		else
		{
			auto transaction (ledger.store.tx_begin_read ());
			auto block (ledger.store.block.get (transaction, hash_a));
			should_vote = block != nullptr && ledger.dependents_confirmed (transaction, *block);
		}
		if (should_vote)
		{
			scendere::unique_lock<scendere::mutex> lock (mutex);
			candidates.emplace_back (root_a, hash_a);
			if (candidates.size () >= scendere::network::confirm_ack_hashes_max)
			{
				lock.unlock ();
				condition.notify_all ();
			}
		}
	}
}

void scendere::vote_generator::stop ()
{
	scendere::unique_lock<scendere::mutex> lock (mutex);
	stopped = true;

	lock.unlock ();
	condition.notify_all ();

	if (thread.joinable ())
	{
		thread.join ();
	}
}

std::size_t scendere::vote_generator::generate (std::vector<std::shared_ptr<scendere::block>> const & blocks_a, std::shared_ptr<scendere::transport::channel> const & channel_a)
{
	request_t::first_type req_candidates;
	{
		auto transaction (ledger.store.tx_begin_read ());
		auto dependents_confirmed = [&transaction, this] (auto const & block_a) {
			return this->ledger.dependents_confirmed (transaction, *block_a);
		};
		auto as_candidate = [] (auto const & block_a) {
			return candidate_t{ block_a->root (), block_a->hash () };
		};
		scendere::transform_if (blocks_a.begin (), blocks_a.end (), std::back_inserter (req_candidates), dependents_confirmed, as_candidate);
	}
	auto const result = req_candidates.size ();
	scendere::lock_guard<scendere::mutex> guard (mutex);
	requests.emplace_back (std::move (req_candidates), channel_a);
	while (requests.size () > max_requests)
	{
		// On a large queue of requests, erase the oldest one
		requests.pop_front ();
		stats.inc (scendere::stat::type::vote_generator, scendere::stat::detail::generator_replies_discarded);
	}
	return result;
}

void scendere::vote_generator::set_reply_action (std::function<void (std::shared_ptr<scendere::vote> const &, std::shared_ptr<scendere::transport::channel> const &)> action_a)
{
	release_assert (!reply_action);
	reply_action = action_a;
}

void scendere::vote_generator::broadcast (scendere::unique_lock<scendere::mutex> & lock_a)
{
	debug_assert (lock_a.owns_lock ());
	std::unordered_set<std::shared_ptr<scendere::vote>> cached_sent;
	std::vector<scendere::block_hash> hashes;
	std::vector<scendere::root> roots;
	hashes.reserve (scendere::network::confirm_ack_hashes_max);
	roots.reserve (scendere::network::confirm_ack_hashes_max);
	while (!candidates.empty () && hashes.size () < scendere::network::confirm_ack_hashes_max)
	{
		auto const & [root, hash] = candidates.front ();
		auto cached_votes = history.votes (root, hash, is_final);
		for (auto const & cached_vote : cached_votes)
		{
			if (cached_sent.insert (cached_vote).second)
			{
				broadcast_action (cached_vote);
			}
		}
		if (cached_votes.empty () && std::find (roots.begin (), roots.end (), root) == roots.end ())
		{
			if (spacing.votable (root, hash))
			{
				roots.push_back (root);
				hashes.push_back (hash);
			}
			else
			{
				stats.inc (scendere::stat::type::vote_generator, scendere::stat::detail::generator_spacing);
			}
		}
		candidates.pop_front ();
	}
	if (!hashes.empty ())
	{
		lock_a.unlock ();
		vote (hashes, roots, [this] (auto const & vote_a) {
			this->broadcast_action (vote_a);
			this->stats.inc (scendere::stat::type::vote_generator, scendere::stat::detail::generator_broadcasts);
		});
		lock_a.lock ();
	}
}

void scendere::vote_generator::reply (scendere::unique_lock<scendere::mutex> & lock_a, request_t && request_a)
{
	lock_a.unlock ();
	std::unordered_set<std::shared_ptr<scendere::vote>> cached_sent;
	auto i (request_a.first.cbegin ());
	auto n (request_a.first.cend ());
	while (i != n && !stopped)
	{
		std::vector<scendere::block_hash> hashes;
		std::vector<scendere::root> roots;
		hashes.reserve (scendere::network::confirm_ack_hashes_max);
		roots.reserve (scendere::network::confirm_ack_hashes_max);
		for (; i != n && hashes.size () < scendere::network::confirm_ack_hashes_max; ++i)
		{
			auto const & [root, hash] = *i;
			auto cached_votes = history.votes (root, hash, is_final);
			for (auto const & cached_vote : cached_votes)
			{
				if (cached_sent.insert (cached_vote).second)
				{
					stats.add (scendere::stat::type::requests, scendere::stat::detail::requests_cached_late_hashes, stat::dir::in, cached_vote->blocks.size ());
					stats.inc (scendere::stat::type::requests, scendere::stat::detail::requests_cached_late_votes, stat::dir::in);
					reply_action (cached_vote, request_a.second);
				}
			}
			if (cached_votes.empty () && std::find (roots.begin (), roots.end (), root) == roots.end ())
			{
				if (spacing.votable (root, hash))
				{
					roots.push_back (root);
					hashes.push_back (hash);
				}
				else
				{
					stats.inc (scendere::stat::type::vote_generator, scendere::stat::detail::generator_spacing);
				}
			}
		}
		if (!hashes.empty ())
		{
			stats.add (scendere::stat::type::requests, scendere::stat::detail::requests_generated_hashes, stat::dir::in, hashes.size ());
			vote (hashes, roots, [this, &channel = request_a.second] (std::shared_ptr<scendere::vote> const & vote_a) {
				this->reply_action (vote_a, channel);
				this->stats.inc (scendere::stat::type::requests, scendere::stat::detail::requests_generated_votes, stat::dir::in);
			});
		}
	}
	stats.inc (scendere::stat::type::vote_generator, scendere::stat::detail::generator_replies);
	lock_a.lock ();
}

void scendere::vote_generator::vote (std::vector<scendere::block_hash> const & hashes_a, std::vector<scendere::root> const & roots_a, std::function<void (std::shared_ptr<scendere::vote> const &)> const & action_a)
{
	debug_assert (hashes_a.size () == roots_a.size ());
	std::vector<std::shared_ptr<scendere::vote>> votes_l;
	wallets.foreach_representative ([this, &hashes_a, &votes_l] (scendere::public_key const & pub_a, scendere::raw_key const & prv_a) {
		auto timestamp = this->is_final ? scendere::vote::timestamp_max : scendere::milliseconds_since_epoch ();
		uint8_t duration = this->is_final ? scendere::vote::duration_max : /*8192ms*/ 0x9;
		votes_l.emplace_back (std::make_shared<scendere::vote> (pub_a, prv_a, timestamp, duration, hashes_a));
	});
	for (auto const & vote_l : votes_l)
	{
		for (std::size_t i (0), n (hashes_a.size ()); i != n; ++i)
		{
			history.add (roots_a[i], hashes_a[i], vote_l);
			spacing.flag (roots_a[i], hashes_a[i]);
		}
		action_a (vote_l);
	}
}

void scendere::vote_generator::broadcast_action (std::shared_ptr<scendere::vote> const & vote_a) const
{
	network.flood_vote_pr (vote_a);
	network.flood_vote (vote_a, 2.0f);
	vote_processor.vote (vote_a, std::make_shared<scendere::transport::channel_loopback> (network.node));
}

void scendere::vote_generator::run ()
{
	scendere::thread_role::set (scendere::thread_role::name::voting);
	scendere::unique_lock<scendere::mutex> lock (mutex);
	started = true;
	lock.unlock ();
	condition.notify_all ();
	lock.lock ();
	while (!stopped)
	{
		if (candidates.size () >= scendere::network::confirm_ack_hashes_max)
		{
			broadcast (lock);
		}
		else if (!requests.empty ())
		{
			auto request (requests.front ());
			requests.pop_front ();
			reply (lock, std::move (request));
		}
		else
		{
			condition.wait_for (lock, config.vote_generator_delay, [this] () { return this->candidates.size () >= scendere::network::confirm_ack_hashes_max; });
			if (candidates.size () >= config.vote_generator_threshold && candidates.size () < scendere::network::confirm_ack_hashes_max)
			{
				condition.wait_for (lock, config.vote_generator_delay, [this] () { return this->candidates.size () >= scendere::network::confirm_ack_hashes_max; });
			}
			if (!candidates.empty ())
			{
				broadcast (lock);
			}
		}
	}
}

scendere::vote_generator_session::vote_generator_session (scendere::vote_generator & vote_generator_a) :
	generator (vote_generator_a)
{
}

void scendere::vote_generator_session::add (scendere::root const & root_a, scendere::block_hash const & hash_a)
{
	debug_assert (scendere::thread_role::get () == scendere::thread_role::name::request_loop);
	items.emplace_back (root_a, hash_a);
}

void scendere::vote_generator_session::flush ()
{
	debug_assert (scendere::thread_role::get () == scendere::thread_role::name::request_loop);
	for (auto const & [root, hash] : items)
	{
		generator.add (root, hash);
	}
}

std::unique_ptr<scendere::container_info_component> scendere::collect_container_info (scendere::vote_generator & vote_generator, std::string const & name)
{
	std::size_t candidates_count = 0;
	std::size_t requests_count = 0;
	{
		scendere::lock_guard<scendere::mutex> guard (vote_generator.mutex);
		candidates_count = vote_generator.candidates.size ();
		requests_count = vote_generator.requests.size ();
	}
	auto sizeof_candidate_element = sizeof (decltype (vote_generator.candidates)::value_type);
	auto sizeof_request_element = sizeof (decltype (vote_generator.requests)::value_type);
	auto composite = std::make_unique<container_info_composite> (name);
	composite->add_component (std::make_unique<container_info_leaf> (container_info{ "candidates", candidates_count, sizeof_candidate_element }));
	composite->add_component (std::make_unique<container_info_leaf> (container_info{ "requests", requests_count, sizeof_request_element }));
	return composite;
}
