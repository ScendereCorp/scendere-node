#include <scendere/lib/threading.hpp>
#include <scendere/node/bootstrap/bootstrap.hpp>
#include <scendere/node/bootstrap/bootstrap_lazy.hpp>
#include <scendere/node/bootstrap/bootstrap_legacy.hpp>
#include <scendere/node/common.hpp>
#include <scendere/node/node.hpp>

#include <boost/format.hpp>

#include <algorithm>

scendere::bootstrap_initiator::bootstrap_initiator (scendere::node & node_a) :
	node (node_a)
{
	connections = std::make_shared<scendere::bootstrap_connections> (node);
	bootstrap_initiator_threads.push_back (boost::thread ([this] () {
		scendere::thread_role::set (scendere::thread_role::name::bootstrap_connections);
		connections->run ();
	}));
	for (std::size_t i = 0; i < node.config.bootstrap_initiator_threads; ++i)
	{
		bootstrap_initiator_threads.push_back (boost::thread ([this] () {
			scendere::thread_role::set (scendere::thread_role::name::bootstrap_initiator);
			run_bootstrap ();
		}));
	}
}

scendere::bootstrap_initiator::~bootstrap_initiator ()
{
	stop ();
}

void scendere::bootstrap_initiator::bootstrap (bool force, std::string id_a, uint32_t const frontiers_age_a, scendere::account const & start_account_a)
{
	if (force)
	{
		stop_attempts ();
	}
	scendere::unique_lock<scendere::mutex> lock (mutex);
	if (!stopped && find_attempt (scendere::bootstrap_mode::legacy) == nullptr)
	{
		node.stats.inc (scendere::stat::type::bootstrap, frontiers_age_a == std::numeric_limits<uint32_t>::max () ? scendere::stat::detail::initiate : scendere::stat::detail::initiate_legacy_age, scendere::stat::dir::out);
		auto legacy_attempt (std::make_shared<scendere::bootstrap_attempt_legacy> (node.shared (), attempts.incremental++, id_a, frontiers_age_a, start_account_a));
		attempts_list.push_back (legacy_attempt);
		attempts.add (legacy_attempt);
		lock.unlock ();
		condition.notify_all ();
	}
}

void scendere::bootstrap_initiator::bootstrap (scendere::endpoint const & endpoint_a, bool add_to_peers, std::string id_a)
{
	if (add_to_peers)
	{
		if (!node.flags.disable_udp)
		{
			node.network.udp_channels.insert (scendere::transport::map_endpoint_to_v6 (endpoint_a), node.network_params.network.protocol_version);
		}
		else if (!node.flags.disable_tcp_realtime)
		{
			node.network.merge_peer (scendere::transport::map_endpoint_to_v6 (endpoint_a));
		}
	}
	if (!stopped)
	{
		stop_attempts ();
		node.stats.inc (scendere::stat::type::bootstrap, scendere::stat::detail::initiate, scendere::stat::dir::out);
		scendere::lock_guard<scendere::mutex> lock (mutex);
		auto legacy_attempt (std::make_shared<scendere::bootstrap_attempt_legacy> (node.shared (), attempts.incremental++, id_a, std::numeric_limits<uint32_t>::max (), 0));
		attempts_list.push_back (legacy_attempt);
		attempts.add (legacy_attempt);
		if (!node.network.excluded_peers.check (scendere::transport::map_endpoint_to_tcp (endpoint_a)))
		{
			connections->add_connection (endpoint_a);
		}
	}
	condition.notify_all ();
}

bool scendere::bootstrap_initiator::bootstrap_lazy (scendere::hash_or_account const & hash_or_account_a, bool force, bool confirmed, std::string id_a)
{
	bool key_inserted (false);
	auto lazy_attempt (current_lazy_attempt ());
	if (lazy_attempt == nullptr || force)
	{
		if (force)
		{
			stop_attempts ();
		}
		node.stats.inc (scendere::stat::type::bootstrap, scendere::stat::detail::initiate_lazy, scendere::stat::dir::out);
		scendere::lock_guard<scendere::mutex> lock (mutex);
		if (!stopped && find_attempt (scendere::bootstrap_mode::lazy) == nullptr)
		{
			lazy_attempt = std::make_shared<scendere::bootstrap_attempt_lazy> (node.shared (), attempts.incremental++, id_a.empty () ? hash_or_account_a.to_string () : id_a);
			attempts_list.push_back (lazy_attempt);
			attempts.add (lazy_attempt);
			key_inserted = lazy_attempt->lazy_start (hash_or_account_a, confirmed);
		}
	}
	else
	{
		key_inserted = lazy_attempt->lazy_start (hash_or_account_a, confirmed);
	}
	condition.notify_all ();
	return key_inserted;
}

void scendere::bootstrap_initiator::bootstrap_wallet (std::deque<scendere::account> & accounts_a)
{
	debug_assert (!accounts_a.empty ());
	auto wallet_attempt (current_wallet_attempt ());
	node.stats.inc (scendere::stat::type::bootstrap, scendere::stat::detail::initiate_wallet_lazy, scendere::stat::dir::out);
	if (wallet_attempt == nullptr)
	{
		scendere::lock_guard<scendere::mutex> lock (mutex);
		std::string id (!accounts_a.empty () ? accounts_a[0].to_account () : "");
		wallet_attempt = std::make_shared<scendere::bootstrap_attempt_wallet> (node.shared (), attempts.incremental++, id);
		attempts_list.push_back (wallet_attempt);
		attempts.add (wallet_attempt);
		wallet_attempt->wallet_start (accounts_a);
	}
	else
	{
		wallet_attempt->wallet_start (accounts_a);
	}
	condition.notify_all ();
}

void scendere::bootstrap_initiator::run_bootstrap ()
{
	scendere::unique_lock<scendere::mutex> lock (mutex);
	while (!stopped)
	{
		if (has_new_attempts ())
		{
			auto attempt (new_attempt ());
			lock.unlock ();
			if (attempt != nullptr)
			{
				attempt->run ();
				remove_attempt (attempt);
			}
			lock.lock ();
		}
		else
		{
			condition.wait (lock);
		}
	}
}

void scendere::bootstrap_initiator::lazy_requeue (scendere::block_hash const & hash_a, scendere::block_hash const & previous_a)
{
	auto lazy_attempt (current_lazy_attempt ());
	if (lazy_attempt != nullptr)
	{
		lazy_attempt->lazy_requeue (hash_a, previous_a);
	}
}

void scendere::bootstrap_initiator::add_observer (std::function<void (bool)> const & observer_a)
{
	scendere::lock_guard<scendere::mutex> lock (observers_mutex);
	observers.push_back (observer_a);
}

bool scendere::bootstrap_initiator::in_progress ()
{
	scendere::lock_guard<scendere::mutex> lock (mutex);
	return !attempts_list.empty ();
}

std::shared_ptr<scendere::bootstrap_attempt> scendere::bootstrap_initiator::find_attempt (scendere::bootstrap_mode mode_a)
{
	for (auto & i : attempts_list)
	{
		if (i->mode == mode_a)
		{
			return i;
		}
	}
	return nullptr;
}

void scendere::bootstrap_initiator::remove_attempt (std::shared_ptr<scendere::bootstrap_attempt> attempt_a)
{
	scendere::unique_lock<scendere::mutex> lock (mutex);
	auto attempt (std::find (attempts_list.begin (), attempts_list.end (), attempt_a));
	if (attempt != attempts_list.end ())
	{
		auto attempt_ptr (*attempt);
		attempts.remove (attempt_ptr->incremental_id);
		attempts_list.erase (attempt);
		debug_assert (attempts.size () == attempts_list.size ());
		lock.unlock ();
		attempt_ptr->stop ();
	}
	else
	{
		lock.unlock ();
	}
	condition.notify_all ();
}

std::shared_ptr<scendere::bootstrap_attempt> scendere::bootstrap_initiator::new_attempt ()
{
	for (auto & i : attempts_list)
	{
		if (!i->started.exchange (true))
		{
			return i;
		}
	}
	return nullptr;
}

bool scendere::bootstrap_initiator::has_new_attempts ()
{
	for (auto & i : attempts_list)
	{
		if (!i->started)
		{
			return true;
		}
	}
	return false;
}

std::shared_ptr<scendere::bootstrap_attempt> scendere::bootstrap_initiator::current_attempt ()
{
	scendere::lock_guard<scendere::mutex> lock (mutex);
	return find_attempt (scendere::bootstrap_mode::legacy);
}

std::shared_ptr<scendere::bootstrap_attempt> scendere::bootstrap_initiator::current_lazy_attempt ()
{
	scendere::lock_guard<scendere::mutex> lock (mutex);
	return find_attempt (scendere::bootstrap_mode::lazy);
}

std::shared_ptr<scendere::bootstrap_attempt> scendere::bootstrap_initiator::current_wallet_attempt ()
{
	scendere::lock_guard<scendere::mutex> lock (mutex);
	return find_attempt (scendere::bootstrap_mode::wallet_lazy);
}

void scendere::bootstrap_initiator::stop_attempts ()
{
	scendere::unique_lock<scendere::mutex> lock (mutex);
	std::vector<std::shared_ptr<scendere::bootstrap_attempt>> copy_attempts;
	copy_attempts.swap (attempts_list);
	attempts.clear ();
	lock.unlock ();
	for (auto & i : copy_attempts)
	{
		i->stop ();
	}
}

void scendere::bootstrap_initiator::stop ()
{
	if (!stopped.exchange (true))
	{
		stop_attempts ();
		connections->stop ();
		condition.notify_all ();

		for (auto & thread : bootstrap_initiator_threads)
		{
			if (thread.joinable ())
			{
				thread.join ();
			}
		}
	}
}

void scendere::bootstrap_initiator::notify_listeners (bool in_progress_a)
{
	scendere::lock_guard<scendere::mutex> lock (observers_mutex);
	for (auto & i : observers)
	{
		i (in_progress_a);
	}
}

std::unique_ptr<scendere::container_info_component> scendere::collect_container_info (bootstrap_initiator & bootstrap_initiator, std::string const & name)
{
	std::size_t count;
	std::size_t cache_count;
	{
		scendere::lock_guard<scendere::mutex> guard (bootstrap_initiator.observers_mutex);
		count = bootstrap_initiator.observers.size ();
	}
	{
		scendere::lock_guard<scendere::mutex> guard (bootstrap_initiator.cache.pulls_cache_mutex);
		cache_count = bootstrap_initiator.cache.cache.size ();
	}

	auto sizeof_element = sizeof (decltype (bootstrap_initiator.observers)::value_type);
	auto sizeof_cache_element = sizeof (decltype (bootstrap_initiator.cache.cache)::value_type);
	auto composite = std::make_unique<container_info_composite> (name);
	composite->add_component (std::make_unique<container_info_leaf> (container_info{ "observers", count, sizeof_element }));
	composite->add_component (std::make_unique<container_info_leaf> (container_info{ "pulls_cache", cache_count, sizeof_cache_element }));
	return composite;
}

void scendere::pulls_cache::add (scendere::pull_info const & pull_a)
{
	if (pull_a.processed > 500)
	{
		scendere::lock_guard<scendere::mutex> guard (pulls_cache_mutex);
		// Clean old pull
		if (cache.size () > cache_size_max)
		{
			cache.erase (cache.begin ());
		}
		debug_assert (cache.size () <= cache_size_max);
		scendere::uint512_union head_512 (pull_a.account_or_head, pull_a.head_original);
		auto existing (cache.get<account_head_tag> ().find (head_512));
		if (existing == cache.get<account_head_tag> ().end ())
		{
			// Insert new pull
			auto inserted (cache.emplace (scendere::cached_pulls{ std::chrono::steady_clock::now (), head_512, pull_a.head }));
			(void)inserted;
			debug_assert (inserted.second);
		}
		else
		{
			// Update existing pull
			cache.get<account_head_tag> ().modify (existing, [pull_a] (scendere::cached_pulls & cache_a) {
				cache_a.time = std::chrono::steady_clock::now ();
				cache_a.new_head = pull_a.head;
			});
		}
	}
}

void scendere::pulls_cache::update_pull (scendere::pull_info & pull_a)
{
	scendere::lock_guard<scendere::mutex> guard (pulls_cache_mutex);
	scendere::uint512_union head_512 (pull_a.account_or_head, pull_a.head_original);
	auto existing (cache.get<account_head_tag> ().find (head_512));
	if (existing != cache.get<account_head_tag> ().end ())
	{
		pull_a.head = existing->new_head;
	}
}

void scendere::pulls_cache::remove (scendere::pull_info const & pull_a)
{
	scendere::lock_guard<scendere::mutex> guard (pulls_cache_mutex);
	scendere::uint512_union head_512 (pull_a.account_or_head, pull_a.head_original);
	cache.get<account_head_tag> ().erase (head_512);
}

void scendere::bootstrap_attempts::add (std::shared_ptr<scendere::bootstrap_attempt> attempt_a)
{
	scendere::lock_guard<scendere::mutex> lock (bootstrap_attempts_mutex);
	attempts.emplace (attempt_a->incremental_id, attempt_a);
}

void scendere::bootstrap_attempts::remove (uint64_t incremental_id_a)
{
	scendere::lock_guard<scendere::mutex> lock (bootstrap_attempts_mutex);
	attempts.erase (incremental_id_a);
}

void scendere::bootstrap_attempts::clear ()
{
	scendere::lock_guard<scendere::mutex> lock (bootstrap_attempts_mutex);
	attempts.clear ();
}

std::shared_ptr<scendere::bootstrap_attempt> scendere::bootstrap_attempts::find (uint64_t incremental_id_a)
{
	scendere::lock_guard<scendere::mutex> lock (bootstrap_attempts_mutex);
	auto find_attempt (attempts.find (incremental_id_a));
	if (find_attempt != attempts.end ())
	{
		return find_attempt->second;
	}
	else
	{
		return nullptr;
	}
}

std::size_t scendere::bootstrap_attempts::size ()
{
	scendere::lock_guard<scendere::mutex> lock (bootstrap_attempts_mutex);
	return attempts.size ();
}
