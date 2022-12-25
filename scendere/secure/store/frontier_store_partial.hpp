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
void release_assert_success (store_partial<Val, Derived_Store> const & store, int const status);

template <typename Val, typename Derived_Store>
class frontier_store_partial : public frontier_store
{
private:
	scendere::store_partial<Val, Derived_Store> & store;

	friend void release_assert_success<Val, Derived_Store> (store_partial<Val, Derived_Store> const &, int const);

public:
	explicit frontier_store_partial (scendere::store_partial<Val, Derived_Store> & store_a) :
		store (store_a){};

	void put (scendere::write_transaction const & transaction_a, scendere::block_hash const & block_a, scendere::account const & account_a) override
	{
		scendere::db_val<Val> account (account_a);
		auto status (store.put (transaction_a, tables::frontiers, block_a, account));
		release_assert_success (store, status);
	}

	scendere::account get (scendere::transaction const & transaction_a, scendere::block_hash const & block_a) const override
	{
		scendere::db_val<Val> value;
		auto status (store.get (transaction_a, tables::frontiers, scendere::db_val<Val> (block_a), value));
		release_assert (store.success (status) || store.not_found (status));
		scendere::account result{};
		if (store.success (status))
		{
			result = static_cast<scendere::account> (value);
		}
		return result;
	}

	void del (scendere::write_transaction const & transaction_a, scendere::block_hash const & block_a) override
	{
		auto status (store.del (transaction_a, tables::frontiers, block_a));
		release_assert_success (store, status);
	}

	scendere::store_iterator<scendere::block_hash, scendere::account> begin (scendere::transaction const & transaction_a) const override
	{
		return store.template make_iterator<scendere::block_hash, scendere::account> (transaction_a, tables::frontiers);
	}

	scendere::store_iterator<scendere::block_hash, scendere::account> begin (scendere::transaction const & transaction_a, scendere::block_hash const & hash_a) const override
	{
		return store.template make_iterator<scendere::block_hash, scendere::account> (transaction_a, tables::frontiers, scendere::db_val<Val> (hash_a));
	}

	scendere::store_iterator<scendere::block_hash, scendere::account> end () const override
	{
		return scendere::store_iterator<scendere::block_hash, scendere::account> (nullptr);
	}

	void for_each_par (std::function<void (scendere::read_transaction const &, scendere::store_iterator<scendere::block_hash, scendere::account>, scendere::store_iterator<scendere::block_hash, scendere::account>)> const & action_a) const override
	{
		parallel_traversal<scendere::uint256_t> (
		[&action_a, this] (scendere::uint256_t const & start, scendere::uint256_t const & end, bool const is_last) {
			auto transaction (this->store.tx_begin_read ());
			action_a (transaction, this->begin (transaction, start), !is_last ? this->begin (transaction, end) : this->end ());
		});
	}
};

}
