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
class peer_store_partial : public peer_store
{
private:
	scendere::store_partial<Val, Derived_Store> & store;

	friend void release_assert_success<Val, Derived_Store> (store_partial<Val, Derived_Store> const &, int const);

public:
	explicit peer_store_partial (scendere::store_partial<Val, Derived_Store> & store_a) :
		store (store_a){};

	void put (scendere::write_transaction const & transaction_a, scendere::endpoint_key const & endpoint_a) override
	{
		auto status = store.put_key (transaction_a, tables::peers, endpoint_a);
		release_assert_success (store, status);
	}

	void del (scendere::write_transaction const & transaction_a, scendere::endpoint_key const & endpoint_a) override
	{
		auto status (store.del (transaction_a, tables::peers, endpoint_a));
		release_assert_success (store, status);
	}

	bool exists (scendere::transaction const & transaction_a, scendere::endpoint_key const & endpoint_a) const override
	{
		return store.exists (transaction_a, tables::peers, scendere::db_val<Val> (endpoint_a));
	}

	size_t count (scendere::transaction const & transaction_a) const override
	{
		return store.count (transaction_a, tables::peers);
	}

	void clear (scendere::write_transaction const & transaction_a) override
	{
		auto status = store.drop (transaction_a, tables::peers);
		release_assert_success (store, status);
	}

	scendere::store_iterator<scendere::endpoint_key, scendere::no_value> begin (scendere::transaction const & transaction_a) const override
	{
		return store.template make_iterator<scendere::endpoint_key, scendere::no_value> (transaction_a, tables::peers);
	}

	scendere::store_iterator<scendere::endpoint_key, scendere::no_value> end () const override
	{
		return scendere::store_iterator<scendere::endpoint_key, scendere::no_value> (nullptr);
	}
};

}
