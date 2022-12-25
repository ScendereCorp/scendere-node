#include <scendere/crypto_lib/random_pool.hpp>
#include <scendere/node/bootstrap/bootstrap.hpp>
#include <scendere/node/bootstrap/bootstrap_attempt.hpp>
#include <scendere/node/bootstrap/bootstrap_bulk_push.hpp>
#include <scendere/node/bootstrap/bootstrap_frontier.hpp>
#include <scendere/node/common.hpp>
#include <scendere/node/node.hpp>
#include <scendere/node/transport/tcp.hpp>
#include <scendere/node/websocket.hpp>

#include <boost/format.hpp>

#include <algorithm>

constexpr unsigned scendere::bootstrap_limits::requeued_pulls_limit;
constexpr unsigned scendere::bootstrap_limits::requeued_pulls_limit_dev;

scendere::bootstrap_attempt::bootstrap_attempt (std::shared_ptr<scendere::node> const & node_a, scendere::bootstrap_mode mode_a, uint64_t incremental_id_a, std::string id_a) :
	node (node_a),
	incremental_id (incremental_id_a),
	id (id_a),
	mode (mode_a)
{
	if (id.empty ())
	{
		id = scendere::hardened_constants::get ().random_128.to_string ();
	}

	node->logger.always_log (boost::str (boost::format ("Starting %1% bootstrap attempt with ID %2%") % mode_text () % id));
	node->bootstrap_initiator.notify_listeners (true);
	if (node->websocket_server)
	{
		scendere::websocket::message_builder builder;
		node->websocket_server->broadcast (builder.bootstrap_started (id, mode_text ()));
	}
}

scendere::bootstrap_attempt::~bootstrap_attempt ()
{
	node->logger.always_log (boost::str (boost::format ("Exiting %1% bootstrap attempt with ID %2%") % mode_text () % id));
	node->bootstrap_initiator.notify_listeners (false);
	if (node->websocket_server)
	{
		scendere::websocket::message_builder builder;
		node->websocket_server->broadcast (builder.bootstrap_exited (id, mode_text (), attempt_start, total_blocks));
	}
}

bool scendere::bootstrap_attempt::should_log ()
{
	scendere::lock_guard<scendere::mutex> guard (next_log_mutex);
	auto result (false);
	auto now (std::chrono::steady_clock::now ());
	if (next_log < now)
	{
		result = true;
		next_log = now + std::chrono::seconds (15);
	}
	return result;
}

bool scendere::bootstrap_attempt::still_pulling ()
{
	debug_assert (!mutex.try_lock ());
	auto running (!stopped);
	auto still_pulling (pulling > 0);
	return running && still_pulling;
}

void scendere::bootstrap_attempt::pull_started ()
{
	{
		scendere::lock_guard<scendere::mutex> guard (mutex);
		++pulling;
	}
	condition.notify_all ();
}

void scendere::bootstrap_attempt::pull_finished ()
{
	{
		scendere::lock_guard<scendere::mutex> guard (mutex);
		--pulling;
	}
	condition.notify_all ();
}

void scendere::bootstrap_attempt::stop ()
{
	{
		scendere::lock_guard<scendere::mutex> lock (mutex);
		stopped = true;
	}
	condition.notify_all ();
	node->bootstrap_initiator.connections->clear_pulls (incremental_id);
}

std::string scendere::bootstrap_attempt::mode_text ()
{
	std::string mode_text;
	if (mode == scendere::bootstrap_mode::legacy)
	{
		mode_text = "legacy";
	}
	else if (mode == scendere::bootstrap_mode::lazy)
	{
		mode_text = "lazy";
	}
	else if (mode == scendere::bootstrap_mode::wallet_lazy)
	{
		mode_text = "wallet_lazy";
	}
	return mode_text;
}

void scendere::bootstrap_attempt::add_frontier (scendere::pull_info const &)
{
	debug_assert (mode == scendere::bootstrap_mode::legacy);
}

void scendere::bootstrap_attempt::add_bulk_push_target (scendere::block_hash const &, scendere::block_hash const &)
{
	debug_assert (mode == scendere::bootstrap_mode::legacy);
}

bool scendere::bootstrap_attempt::request_bulk_push_target (std::pair<scendere::block_hash, scendere::block_hash> &)
{
	debug_assert (mode == scendere::bootstrap_mode::legacy);
	return true;
}

void scendere::bootstrap_attempt::set_start_account (scendere::account const &)
{
	debug_assert (mode == scendere::bootstrap_mode::legacy);
}

bool scendere::bootstrap_attempt::process_block (std::shared_ptr<scendere::block> const & block_a, scendere::account const & known_account_a, uint64_t pull_blocks_processed, scendere::bulk_pull::count_t max_blocks, bool block_expected, unsigned retry_limit)
{
	bool stop_pull (false);
	// If block already exists in the ledger, then we can avoid next part of long account chain
	if (pull_blocks_processed % scendere::bootstrap_limits::pull_count_per_check == 0 && node->ledger.block_or_pruned_exists (block_a->hash ()))
	{
		stop_pull = true;
	}
	else
	{
		scendere::unchecked_info info (block_a, known_account_a, scendere::signature_verification::unknown);
		node->block_processor.add (info);
	}
	return stop_pull;
}

bool scendere::bootstrap_attempt::lazy_start (scendere::hash_or_account const &, bool)
{
	debug_assert (mode == scendere::bootstrap_mode::lazy);
	return false;
}

void scendere::bootstrap_attempt::lazy_add (scendere::pull_info const &)
{
	debug_assert (mode == scendere::bootstrap_mode::lazy);
}

void scendere::bootstrap_attempt::lazy_requeue (scendere::block_hash const &, scendere::block_hash const &)
{
	debug_assert (mode == scendere::bootstrap_mode::lazy);
}

uint32_t scendere::bootstrap_attempt::lazy_batch_size ()
{
	debug_assert (mode == scendere::bootstrap_mode::lazy);
	return node->network_params.bootstrap.lazy_min_pull_blocks;
}

bool scendere::bootstrap_attempt::lazy_processed_or_exists (scendere::block_hash const &)
{
	debug_assert (mode == scendere::bootstrap_mode::lazy);
	return false;
}

bool scendere::bootstrap_attempt::lazy_has_expired () const
{
	debug_assert (mode == scendere::bootstrap_mode::lazy);
	return true;
}

void scendere::bootstrap_attempt::requeue_pending (scendere::account const &)
{
	debug_assert (mode == scendere::bootstrap_mode::wallet_lazy);
}

void scendere::bootstrap_attempt::wallet_start (std::deque<scendere::account> &)
{
	debug_assert (mode == scendere::bootstrap_mode::wallet_lazy);
}

std::size_t scendere::bootstrap_attempt::wallet_size ()
{
	debug_assert (mode == scendere::bootstrap_mode::wallet_lazy);
	return 0;
}
