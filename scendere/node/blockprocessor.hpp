#pragma once

#include <scendere/lib/blocks.hpp>
#include <scendere/node/state_block_signature_verification.hpp>
#include <scendere/secure/common.hpp>

#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/sequenced_index.hpp>
#include <boost/multi_index_container.hpp>

#include <chrono>
#include <memory>
#include <thread>
#include <unordered_set>

namespace scendere
{
class node;
class read_transaction;
class transaction;
class write_transaction;
class write_database_queue;

enum class block_origin
{
	local,
	remote
};

class block_post_events final
{
public:
	explicit block_post_events (std::function<scendere::read_transaction ()> &&);
	~block_post_events ();
	std::deque<std::function<void (scendere::read_transaction const &)>> events;

private:
	std::function<scendere::read_transaction ()> get_transaction;
};

/**
 * Processing blocks is a potentially long IO operation.
 * This class isolates block insertion from other operations like servicing network operations
 */
class block_processor final
{
public:
	explicit block_processor (scendere::node &, scendere::write_database_queue &);
	~block_processor ();
	void stop ();
	void flush ();
	std::size_t size ();
	bool full ();
	bool half_full ();
	void add_local (scendere::unchecked_info const & info_a);
	void add (scendere::unchecked_info const &);
	void add (std::shared_ptr<scendere::block> const &);
	void force (std::shared_ptr<scendere::block> const &);
	void wait_write ();
	bool should_log ();
	bool have_blocks_ready ();
	bool have_blocks ();
	void process_blocks ();
	scendere::process_return process_one (scendere::write_transaction const &, block_post_events &, scendere::unchecked_info, bool const = false, scendere::block_origin const = scendere::block_origin::remote);
	scendere::process_return process_one (scendere::write_transaction const &, block_post_events &, std::shared_ptr<scendere::block> const &);
	std::atomic<bool> flushing{ false };
	// Delay required for average network propagartion before requesting confirmation
	static std::chrono::milliseconds constexpr confirmation_request_delay{ 1500 };

private:
	void queue_unchecked (scendere::write_transaction const &, scendere::hash_or_account const &);
	void process_batch (scendere::unique_lock<scendere::mutex> &);
	void process_live (scendere::transaction const &, scendere::block_hash const &, std::shared_ptr<scendere::block> const &, scendere::process_return const &, scendere::block_origin const = scendere::block_origin::remote);
	void requeue_invalid (scendere::block_hash const &, scendere::unchecked_info const &);
	void process_verified_state_blocks (std::deque<scendere::state_block_signature_verification::value_type> &, std::vector<int> const &, std::vector<scendere::block_hash> const &, std::vector<scendere::signature> const &);
	bool stopped{ false };
	bool active{ false };
	bool awaiting_write{ false };
	std::chrono::steady_clock::time_point next_log;
	std::deque<scendere::unchecked_info> blocks;
	std::deque<std::shared_ptr<scendere::block>> forced;
	scendere::condition_variable condition;
	scendere::node & node;
	scendere::write_database_queue & write_database_queue;
	scendere::mutex mutex{ mutex_identifier (mutexes::block_processor) };
	scendere::state_block_signature_verification state_block_signature_verification;
	std::thread processing_thread;

	friend std::unique_ptr<container_info_component> collect_container_info (block_processor & block_processor, std::string const & name);
};
std::unique_ptr<scendere::container_info_component> collect_container_info (block_processor & block_processor, std::string const & name);
}
