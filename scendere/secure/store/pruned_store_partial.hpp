#pragma once

#include <scendere/secure/store_partial.hpp>

namespace
{
template <typename T>
void parallel_traversal (std::function<void (T const &, T const &, bool const)> const & action);
}

namespace scendere
{
template <typename Val, typename Derived_Store>
class store_partial;

template <typename Val, typename Derived_Store>
void release_assert_success (store_partial<Val, Derived_Store> const &, int const);

template <typename Val, typename Derived_Store>
class pruned_store_partial : public pruned_store
{
private:
	scendere::store_partial<Val, Derived_Store> & store;

	friend void release_assert_success<Val, Derived_Store> (store_partial<Val, Derived_Store> const &, int const);

public:
	explicit pruned_store_partial (scendere::store_partial<Val, Derived_Store> & store_a) :
		store (store_a){};

	void put (scendere::write_transaction const & transaction_a, scendere::block_hash const & hash_a) override
	{
		auto status = store.put_key (transaction_a, tables::pruned, hash_a);
		release_assert_success (store, status);
	}

	void del (scendere::write_transaction const & transaction_a, scendere::block_hash const & hash_a) override
	{
		auto status = store.del (transaction_a, tables::pruned, hash_a);
		release_assert_success (store, status);
	}

	bool exists (scendere::transaction const & transaction_a, scendere::block_hash const & hash_a) const override
	{
		return store.exists (transaction_a, tables::pruned, scendere::db_val<Val> (hash_a));
	}

	scendere::block_hash random (scendere::transaction const & transaction_a) override
	{
		scendere::block_hash random_hash;
		scendere::random_pool::generate_block (random_hash.bytes.data (), random_hash.bytes.size ());
		auto existing = store.template make_iterator<scendere::block_hash, scendere::db_val<Val>> (transaction_a, tables::pruned, scendere::db_val<Val> (random_hash));
		auto end (scendere::store_iterator<scendere::block_hash, scendere::db_val<Val>> (nullptr));
		if (existing == end)
		{
			existing = store.template make_iterator<scendere::block_hash, scendere::db_val<Val>> (transaction_a, tables::pruned);
		}
		return existing != end ? existing->first : 0;
	}

	size_t count (scendere::transaction const & transaction_a) const override
	{
		return store.count (transaction_a, tables::pruned);
	}

	void clear (scendere::write_transaction const & transaction_a) override
	{
		auto status = store.drop (transaction_a, tables::pruned);
		release_assert_success (store, status);
	}

	scendere::store_iterator<scendere::block_hash, std::nullptr_t> begin (scendere::transaction const & transaction_a, scendere::block_hash const & hash_a) const override
	{
		return store.template make_iterator<scendere::block_hash, std::nullptr_t> (transaction_a, tables::pruned, scendere::db_val<Val> (hash_a));
	}

	scendere::store_iterator<scendere::block_hash, std::nullptr_t> begin (scendere::transaction const & transaction_a) const override
	{
		return store.template make_iterator<scendere::block_hash, std::nullptr_t> (transaction_a, tables::pruned);
	}

	scendere::store_iterator<scendere::block_hash, std::nullptr_t> end () const override
	{
		return scendere::store_iterator<scendere::block_hash, std::nullptr_t> (nullptr);
	}

	void for_each_par (std::function<void (scendere::read_transaction const &, scendere::store_iterator<scendere::block_hash, std::nullptr_t>, scendere::store_iterator<scendere::block_hash, std::nullptr_t>)> const & action_a) const override
	{
		parallel_traversal<scendere::uint256_t> (
		[&action_a, this] (scendere::uint256_t const & start, scendere::uint256_t const & end, bool const is_last) {
			auto transaction (this->store.tx_begin_read ());
			action_a (transaction, this->begin (transaction, start), !is_last ? this->begin (transaction, end) : this->end ());
		});
	}
};

}
