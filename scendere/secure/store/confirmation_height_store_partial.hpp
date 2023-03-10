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
class confirmation_height_store_partial : public confirmation_height_store
{
private:
	scendere::store_partial<Val, Derived_Store> & store;

	friend void release_assert_success<Val, Derived_Store> (store_partial<Val, Derived_Store> const &, int const);

public:
	explicit confirmation_height_store_partial (scendere::store_partial<Val, Derived_Store> & store_a) :
		store (store_a){};

	void put (scendere::write_transaction const & transaction_a, scendere::account const & account_a, scendere::confirmation_height_info const & confirmation_height_info_a) override
	{
		scendere::db_val<Val> confirmation_height_info (confirmation_height_info_a);
		auto status = store.put (transaction_a, tables::confirmation_height, account_a, confirmation_height_info);
		release_assert_success (store, status);
	}

	bool get (scendere::transaction const & transaction_a, scendere::account const & account_a, scendere::confirmation_height_info & confirmation_height_info_a) override
	{
		scendere::db_val<Val> value;
		auto status = store.get (transaction_a, tables::confirmation_height, scendere::db_val<Val> (account_a), value);
		release_assert (store.success (status) || store.not_found (status));
		bool result (true);
		if (store.success (status))
		{
			scendere::bufferstream stream (reinterpret_cast<uint8_t const *> (value.data ()), value.size ());
			result = confirmation_height_info_a.deserialize (stream);
		}
		if (result)
		{
			confirmation_height_info_a.height = 0;
			confirmation_height_info_a.frontier = 0;
		}

		return result;
	}

	bool exists (scendere::transaction const & transaction_a, scendere::account const & account_a) const override
	{
		return store.exists (transaction_a, tables::confirmation_height, scendere::db_val<Val> (account_a));
	}

	void del (scendere::write_transaction const & transaction_a, scendere::account const & account_a) override
	{
		auto status (store.del (transaction_a, tables::confirmation_height, scendere::db_val<Val> (account_a)));
		release_assert_success (store, status);
	}

	uint64_t count (scendere::transaction const & transaction_a) override
	{
		return store.count (transaction_a, tables::confirmation_height);
	}

	void clear (scendere::write_transaction const & transaction_a, scendere::account const & account_a) override
	{
		del (transaction_a, account_a);
	}

	void clear (scendere::write_transaction const & transaction_a) override
	{
		store.drop (transaction_a, scendere::tables::confirmation_height);
	}

	scendere::store_iterator<scendere::account, scendere::confirmation_height_info> begin (scendere::transaction const & transaction_a, scendere::account const & account_a) const override
	{
		return store.template make_iterator<scendere::account, scendere::confirmation_height_info> (transaction_a, tables::confirmation_height, scendere::db_val<Val> (account_a));
	}

	scendere::store_iterator<scendere::account, scendere::confirmation_height_info> begin (scendere::transaction const & transaction_a) const override
	{
		return store.template make_iterator<scendere::account, scendere::confirmation_height_info> (transaction_a, tables::confirmation_height);
	}

	scendere::store_iterator<scendere::account, scendere::confirmation_height_info> end () const override
	{
		return scendere::store_iterator<scendere::account, scendere::confirmation_height_info> (nullptr);
	}

	void for_each_par (std::function<void (scendere::read_transaction const &, scendere::store_iterator<scendere::account, scendere::confirmation_height_info>, scendere::store_iterator<scendere::account, scendere::confirmation_height_info>)> const & action_a) const override
	{
		parallel_traversal<scendere::uint256_t> (
		[&action_a, this] (scendere::uint256_t const & start, scendere::uint256_t const & end, bool const is_last) {
			auto transaction (this->store.tx_begin_read ());
			action_a (transaction, this->begin (transaction, start), !is_last ? this->begin (transaction, end) : this->end ());
		});
	}
};

}
