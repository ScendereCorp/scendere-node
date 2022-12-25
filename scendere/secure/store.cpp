#include <scendere/lib/threading.hpp>
#include <scendere/secure/store.hpp>

scendere::representative_visitor::representative_visitor (scendere::transaction const & transaction_a, scendere::store & store_a) :
	transaction (transaction_a),
	store (store_a),
	result (0)
{
}

void scendere::representative_visitor::compute (scendere::block_hash const & hash_a)
{
	current = hash_a;
	while (result.is_zero ())
	{
		auto block (store.block.get (transaction, current));
		debug_assert (block != nullptr);
		block->visit (*this);
	}
}

void scendere::representative_visitor::send_block (scendere::send_block const & block_a)
{
	current = block_a.previous ();
}

void scendere::representative_visitor::receive_block (scendere::receive_block const & block_a)
{
	current = block_a.previous ();
}

void scendere::representative_visitor::open_block (scendere::open_block const & block_a)
{
	result = block_a.hash ();
}

void scendere::representative_visitor::change_block (scendere::change_block const & block_a)
{
	result = block_a.hash ();
}

void scendere::representative_visitor::state_block (scendere::state_block const & block_a)
{
	result = block_a.hash ();
}

scendere::read_transaction::read_transaction (std::unique_ptr<scendere::read_transaction_impl> read_transaction_impl) :
	impl (std::move (read_transaction_impl))
{
}

void * scendere::read_transaction::get_handle () const
{
	return impl->get_handle ();
}

void scendere::read_transaction::reset () const
{
	impl->reset ();
}

void scendere::read_transaction::renew () const
{
	impl->renew ();
}

void scendere::read_transaction::refresh () const
{
	reset ();
	renew ();
}

scendere::write_transaction::write_transaction (std::unique_ptr<scendere::write_transaction_impl> write_transaction_impl) :
	impl (std::move (write_transaction_impl))
{
	/*
	 * For IO threads, we do not want them to block on creating write transactions.
	 */
	debug_assert (scendere::thread_role::get () != scendere::thread_role::name::io);
}

void * scendere::write_transaction::get_handle () const
{
	return impl->get_handle ();
}

void scendere::write_transaction::commit ()
{
	impl->commit ();
}

void scendere::write_transaction::renew ()
{
	impl->renew ();
}

void scendere::write_transaction::refresh ()
{
	impl->commit ();
	impl->renew ();
}

bool scendere::write_transaction::contains (scendere::tables table_a) const
{
	return impl->contains (table_a);
}

// clang-format off
scendere::store::store (
	scendere::block_store & block_store_a,
	scendere::frontier_store & frontier_store_a,
	scendere::account_store & account_store_a,
	scendere::pending_store & pending_store_a,
	scendere::unchecked_store & unchecked_store_a,
	scendere::online_weight_store & online_weight_store_a,
	scendere::pruned_store & pruned_store_a,
	scendere::peer_store & peer_store_a,
	scendere::confirmation_height_store & confirmation_height_store_a,
	scendere::final_vote_store & final_vote_store_a,
	scendere::version_store & version_store_a
) :
	block (block_store_a),
	frontier (frontier_store_a),
	account (account_store_a),
	pending (pending_store_a),
	unchecked (unchecked_store_a),
	online_weight (online_weight_store_a),
	pruned (pruned_store_a),
	peer (peer_store_a),
	confirmation_height (confirmation_height_store_a),
	final_vote (final_vote_store_a),
	version (version_store_a)
{
}
// clang-format on

auto scendere::unchecked_store::equal_range (scendere::transaction const & transaction, scendere::block_hash const & dependency) -> std::pair<iterator, iterator>
{
	scendere::unchecked_key begin_l{ dependency, 0 };
	scendere::unchecked_key end_l{ scendere::block_hash{ dependency.number () + 1 }, 0 };
	// Adjust for edge case where number () + 1 wraps around.
	auto end_iter = begin_l.previous < end_l.previous ? lower_bound (transaction, end_l) : end ();
	return std::make_pair (lower_bound (transaction, begin_l), std::move (end_iter));
}

auto scendere::unchecked_store::full_range (scendere::transaction const & transaction) -> std::pair<iterator, iterator>
{
	return std::make_pair (begin (transaction), end ());
}

std::vector<scendere::unchecked_info> scendere::unchecked_store::get (scendere::transaction const & transaction, scendere::block_hash const & dependency)
{
	auto range = equal_range (transaction, dependency);
	std::vector<scendere::unchecked_info> result;
	auto & i = range.first;
	auto & n = range.second;
	for (; i != n; ++i)
	{
		auto const & key = i->first;
		auto const & value = i->second;
		debug_assert (key.hash == value.block->hash ());
		result.push_back (value);
	}
	return result;
}
