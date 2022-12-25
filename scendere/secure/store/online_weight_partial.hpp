#pragma once

#include <scendere/secure/store_partial.hpp>

namespace scendere
{
template <typename Val, typename Derived_Store>
class store_partial;

template <typename Val, typename Derived_Store>
void release_assert_success (store_partial<Val, Derived_Store> const &, int const);

template <typename Val, typename Derived_Store>
class online_weight_store_partial : public online_weight_store
{
private:
	scendere::store_partial<Val, Derived_Store> & store;

	friend void release_assert_success<Val, Derived_Store> (store_partial<Val, Derived_Store> const &, int const);

public:
	explicit online_weight_store_partial (scendere::store_partial<Val, Derived_Store> & store_a) :
		store (store_a){};

	void put (scendere::write_transaction const & transaction_a, uint64_t time_a, scendere::amount const & amount_a) override
	{
		scendere::db_val<Val> value (amount_a);
		auto status (store.put (transaction_a, tables::online_weight, time_a, value));
		release_assert_success (store, status);
	}

	void del (scendere::write_transaction const & transaction_a, uint64_t time_a) override
	{
		auto status (store.del (transaction_a, tables::online_weight, time_a));
		release_assert_success (store, status);
	}

	scendere::store_iterator<uint64_t, scendere::amount> begin (scendere::transaction const & transaction_a) const override
	{
		return store.template make_iterator<uint64_t, scendere::amount> (transaction_a, tables::online_weight);
	}

	scendere::store_iterator<uint64_t, scendere::amount> rbegin (scendere::transaction const & transaction_a) const override
	{
		return store.template make_iterator<uint64_t, scendere::amount> (transaction_a, tables::online_weight, false);
	}

	scendere::store_iterator<uint64_t, scendere::amount> end () const override
	{
		return scendere::store_iterator<uint64_t, scendere::amount> (nullptr);
	}

	size_t count (scendere::transaction const & transaction_a) const override
	{
		return store.count (transaction_a, tables::online_weight);
	}

	void clear (scendere::write_transaction const & transaction_a) override
	{
		auto status (store.drop (transaction_a, tables::online_weight));
		release_assert_success (store, status);
	}
};

}
