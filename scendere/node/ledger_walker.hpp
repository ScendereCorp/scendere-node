// TODO: keep this until diskhash builds fine on Windows
#ifndef _WIN32

#pragma once

#include <scendere/lib/numbers.hpp>

#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <stack>
#include <unordered_set>

#include <diskhash.hpp>

namespace scendere
{
class block;
class ledger;
class transaction;

/** Walks the ledger starting from a start block and applying a depth-first search algorithm */
class ledger_walker final
{
public:
	using should_visit_callback = std::function<bool (std::shared_ptr<scendere::block> const &)>;
	using visitor_callback = std::function<void (std::shared_ptr<scendere::block> const &)>;

	explicit ledger_walker (scendere::ledger const & ledger_a);

	/** Start traversing (in a backwards direction -- towards genesis) from \p start_block_hash_a until \p should_visit_callback_a returns false, calling \p visitor_callback_a at each block. Prefer 'walk' instead, if possible. */
	void walk_backward (scendere::block_hash const & start_block_hash_a, should_visit_callback const & should_visit_callback_a, visitor_callback const & visitor_callback_a);

	/** Start traversing (in a forward direction -- towards end_block_hash_a) from first block (genesis onwards) where \p should_visit_a returns true until \p end_block_hash_a, calling \p visitor_callback at each block. Prefer this one, instead of 'walk_backwards', if possible. */
	void walk (scendere::block_hash const & end_block_hash_a, should_visit_callback const & should_visit_callback_a, visitor_callback const & visitor_callback_a);

	/** Methods similar to walk_backward and walk, but that do not offer the possibility of providing a user-defined should_visit_callback function. */
	void walk_backward (scendere::block_hash const & start_block_hash_a, visitor_callback const & visitor_callback_a);
	void walk (scendere::block_hash const & end_block_hash_a, visitor_callback const & visitor_callback_a);

	/** How many blocks will be held in the in-memory hash before using the disk hash for walking. */
	// TODO TSB: make this 65536
	static constexpr std::size_t in_memory_block_count = 0;

private:
	scendere::ledger const & ledger;
	bool use_in_memory_walked_blocks;
	std::unordered_set<scendere::block_hash> walked_blocks;
	std::optional<dht::DiskHash<bool>> walked_blocks_disk;
	std::stack<scendere::block_hash> blocks_to_walk;

	void enqueue_block (scendere::block_hash block_hash_a);
	void enqueue_block (std::shared_ptr<scendere::block> const & block_a);
	bool add_to_walked_blocks (scendere::block_hash const & block_hash_a);
	bool add_to_walked_blocks_disk (scendere::block_hash const & block_hash_a);
	void clear_queue ();
	std::shared_ptr<scendere::block> dequeue_block (scendere::transaction const & transaction_a);

	friend class ledger_walker_genesis_account_longer_Test;
};

}

#endif // _WIN32 -- TODO: keep this until diskhash builds fine on Windows
