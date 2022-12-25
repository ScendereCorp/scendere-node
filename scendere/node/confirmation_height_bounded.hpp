#pragma once

#include <scendere/lib/numbers.hpp>
#include <scendere/lib/threading.hpp>
#include <scendere/lib/timer.hpp>
#include <scendere/secure/store.hpp>

#include <boost/circular_buffer.hpp>

namespace scendere
{
class ledger;
class read_transaction;
class logging;
class logger_mt;
class write_database_queue;
class write_guard;

class confirmation_height_bounded final
{
public:
	confirmation_height_bounded (scendere::ledger &, scendere::write_database_queue &, std::chrono::milliseconds, scendere::logging const &, scendere::logger_mt &, std::atomic<bool> &, uint64_t &, std::function<void (std::vector<std::shared_ptr<scendere::block>> const &)> const &, std::function<void (scendere::block_hash const &)> const &, std::function<uint64_t ()> const &);
	bool pending_empty () const;
	void clear_process_vars ();
	void process (std::shared_ptr<scendere::block> original_block);
	void cement_blocks (scendere::write_guard & scoped_write_guard_a);

private:
	class top_and_next_hash final
	{
	public:
		scendere::block_hash top;
		boost::optional<scendere::block_hash> next;
		uint64_t next_height;
	};

	class confirmed_info
	{
	public:
		confirmed_info (uint64_t confirmed_height_a, scendere::block_hash const & iterated_frontier);
		uint64_t confirmed_height;
		scendere::block_hash iterated_frontier;
	};

	class write_details final
	{
	public:
		write_details (scendere::account const &, uint64_t, scendere::block_hash const &, uint64_t, scendere::block_hash const &);
		scendere::account account;
		// This is the first block hash (bottom most) which is not cemented
		uint64_t bottom_height;
		scendere::block_hash bottom_hash;
		// Desired cemented frontier
		uint64_t top_height;
		scendere::block_hash top_hash;
	};

	/** The maximum number of blocks to be read in while iterating over a long account chain */
	uint64_t const batch_read_size = 65536;

	/** The maximum number of various containers to keep the memory bounded */
	uint32_t const max_items{ 131072 };

	// All of the atomic variables here just track the size for use in collect_container_info.
	// This is so that no mutexes are needed during the algorithm itself, which would otherwise be needed
	// for the sake of a rarely used RPC call for debugging purposes. As such the sizes are not being acted
	// upon in any way (does not synchronize with any other data).
	// This allows the load and stores to use relaxed atomic memory ordering.
	std::deque<write_details> pending_writes;
	scendere::relaxed_atomic_integral<uint64_t> pending_writes_size{ 0 };
	uint32_t const pending_writes_max_size{ max_items };
	/* Holds confirmation height/cemented frontier in memory for accounts while iterating */
	std::unordered_map<account, confirmed_info> accounts_confirmed_info;
	scendere::relaxed_atomic_integral<uint64_t> accounts_confirmed_info_size{ 0 };

	class receive_chain_details final
	{
	public:
		receive_chain_details (scendere::account const &, uint64_t, scendere::block_hash const &, scendere::block_hash const &, boost::optional<scendere::block_hash>, uint64_t, scendere::block_hash const &);
		scendere::account account;
		uint64_t height;
		scendere::block_hash hash;
		scendere::block_hash top_level;
		boost::optional<scendere::block_hash> next;
		uint64_t bottom_height;
		scendere::block_hash bottom_most;
	};

	class preparation_data final
	{
	public:
		scendere::transaction const & transaction;
		scendere::block_hash const & top_most_non_receive_block_hash;
		bool already_cemented;
		boost::circular_buffer_space_optimized<scendere::block_hash> & checkpoints;
		decltype (accounts_confirmed_info.begin ()) account_it;
		scendere::confirmation_height_info const & confirmation_height_info;
		scendere::account const & account;
		uint64_t bottom_height;
		scendere::block_hash const & bottom_most;
		boost::optional<receive_chain_details> & receive_details;
		boost::optional<top_and_next_hash> & next_in_receive_chain;
	};

	class receive_source_pair final
	{
	public:
		receive_source_pair (receive_chain_details const &, scendere::block_hash const &);

		receive_chain_details receive_details;
		scendere::block_hash source_hash;
	};

	scendere::timer<std::chrono::milliseconds> timer;

	top_and_next_hash get_next_block (boost::optional<top_and_next_hash> const &, boost::circular_buffer_space_optimized<scendere::block_hash> const &, boost::circular_buffer_space_optimized<receive_source_pair> const & receive_source_pairs, boost::optional<receive_chain_details> &, scendere::block const & original_block);
	scendere::block_hash get_least_unconfirmed_hash_from_top_level (scendere::transaction const &, scendere::block_hash const &, scendere::account const &, scendere::confirmation_height_info const &, uint64_t &);
	void prepare_iterated_blocks_for_cementing (preparation_data &);
	bool iterate (scendere::read_transaction const &, uint64_t, scendere::block_hash const &, boost::circular_buffer_space_optimized<scendere::block_hash> &, scendere::block_hash &, scendere::block_hash const &, boost::circular_buffer_space_optimized<receive_source_pair> &, scendere::account const &);

	scendere::ledger & ledger;
	scendere::write_database_queue & write_database_queue;
	std::chrono::milliseconds batch_separate_pending_min_time;
	scendere::logging const & logging;
	scendere::logger_mt & logger;
	std::atomic<bool> & stopped;
	uint64_t & batch_write_size;
	std::function<void (std::vector<std::shared_ptr<scendere::block>> const &)> notify_observers_callback;
	std::function<void (scendere::block_hash const &)> notify_block_already_cemented_observers_callback;
	std::function<uint64_t ()> awaiting_processing_size_callback;

	friend std::unique_ptr<scendere::container_info_component> collect_container_info (confirmation_height_bounded &, std::string const & name_a);
};

std::unique_ptr<scendere::container_info_component> collect_container_info (confirmation_height_bounded &, std::string const & name_a);
}
