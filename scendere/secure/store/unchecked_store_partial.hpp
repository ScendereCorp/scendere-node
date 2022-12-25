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
class unchecked_store_partial : public unchecked_store
{
private:
	scendere::store_partial<Val, Derived_Store> & store;

	friend void release_assert_success<Val, Derived_Store> (store_partial<Val, Derived_Store> const &, int const);

public:
	unchecked_store_partial (scendere::store_partial<Val, Derived_Store> & store_a) :
		store (store_a){};

	void clear (scendere::write_transaction const & transaction_a) override
	{
		auto status = store.drop (transaction_a, tables::unchecked);
		release_assert_success (store, status);
	}

	void put (scendere::write_transaction const & transaction_a, scendere::hash_or_account const & dependency, scendere::unchecked_info const & info_a) override
	{
		scendere::db_val<Val> info (info_a);
		auto status (store.put (transaction_a, tables::unchecked, scendere::unchecked_key{ dependency, info_a.block->hash () }, info));
		release_assert_success (store, status);
	}

	bool exists (scendere::transaction const & transaction_a, scendere::unchecked_key const & unchecked_key_a) override
	{
		scendere::db_val<Val> value;
		auto status (store.get (transaction_a, tables::unchecked, scendere::db_val<Val> (unchecked_key_a), value));
		release_assert (store.success (status) || store.not_found (status));
		return (store.success (status));
	}

	void del (scendere::write_transaction const & transaction_a, scendere::unchecked_key const & key_a) override
	{
		auto status (store.del (transaction_a, tables::unchecked, key_a));
		release_assert_success (store, status);
	}

	scendere::store_iterator<scendere::unchecked_key, scendere::unchecked_info> end () const override
	{
		return scendere::store_iterator<scendere::unchecked_key, scendere::unchecked_info> (nullptr);
	}

	scendere::store_iterator<scendere::unchecked_key, scendere::unchecked_info> begin (scendere::transaction const & transaction_a) const override
	{
		return store.template make_iterator<scendere::unchecked_key, scendere::unchecked_info> (transaction_a, tables::unchecked);
	}

	scendere::store_iterator<scendere::unchecked_key, scendere::unchecked_info> lower_bound (scendere::transaction const & transaction_a, scendere::unchecked_key const & key_a) const override
	{
		return store.template make_iterator<scendere::unchecked_key, scendere::unchecked_info> (transaction_a, tables::unchecked, scendere::db_val<Val> (key_a));
	}

	size_t count (scendere::transaction const & transaction_a) override
	{
		return store.count (transaction_a, tables::unchecked);
	}

	void for_each_par (std::function<void (scendere::read_transaction const &, scendere::store_iterator<scendere::unchecked_key, scendere::unchecked_info>, scendere::store_iterator<scendere::unchecked_key, scendere::unchecked_info>)> const & action_a) const override
	{
		parallel_traversal<scendere::uint512_t> (
		[&action_a, this] (scendere::uint512_t const & start, scendere::uint512_t const & end, bool const is_last) {
			scendere::unchecked_key key_start (start);
			scendere::unchecked_key key_end (end);
			auto transaction (this->store.tx_begin_read ());
			action_a (transaction, this->lower_bound (transaction, key_start), !is_last ? this->lower_bound (transaction, key_end) : this->end ());
		});
	}
};

}
