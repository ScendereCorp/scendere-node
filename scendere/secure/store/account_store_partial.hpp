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
class account_store_partial : public account_store
{
private:
	scendere::store_partial<Val, Derived_Store> & store;

	friend void release_assert_success<Val, Derived_Store> (store_partial<Val, Derived_Store> const &, int const);

public:
	explicit account_store_partial (scendere::store_partial<Val, Derived_Store> & store_a) :
		store (store_a){};

	void put (scendere::write_transaction const & transaction_a, scendere::account const & account_a, scendere::account_info const & info_a) override
	{
		// Check we are still in sync with other tables
		scendere::db_val<Val> info (info_a);
		auto status = store.put (transaction_a, tables::accounts, account_a, info);
		release_assert_success (store, status);
	}

	bool get (scendere::transaction const & transaction_a, scendere::account const & account_a, scendere::account_info & info_a) override
	{
		scendere::db_val<Val> value;
		scendere::db_val<Val> account (account_a);
		auto status1 (store.get (transaction_a, tables::accounts, account, value));
		release_assert (store.success (status1) || store.not_found (status1));
		bool result (true);
		if (store.success (status1))
		{
			scendere::bufferstream stream (reinterpret_cast<uint8_t const *> (value.data ()), value.size ());
			result = info_a.deserialize (stream);
		}
		return result;
	}

	void del (scendere::write_transaction const & transaction_a, scendere::account const & account_a) override
	{
		auto status = store.del (transaction_a, tables::accounts, account_a);
		release_assert_success (store, status);
	}

	bool exists (scendere::transaction const & transaction_a, scendere::account const & account_a) override
	{
		auto iterator (begin (transaction_a, account_a));
		return iterator != end () && scendere::account (iterator->first) == account_a;
	}

	size_t count (scendere::transaction const & transaction_a) override
	{
		return store.count (transaction_a, tables::accounts);
	}

	scendere::store_iterator<scendere::account, scendere::account_info> begin (scendere::transaction const & transaction_a, scendere::account const & account_a) const override
	{
		return store.template make_iterator<scendere::account, scendere::account_info> (transaction_a, tables::accounts, scendere::db_val<Val> (account_a));
	}

	scendere::store_iterator<scendere::account, scendere::account_info> begin (scendere::transaction const & transaction_a) const override
	{
		return store.template make_iterator<scendere::account, scendere::account_info> (transaction_a, tables::accounts);
	}

	scendere::store_iterator<scendere::account, scendere::account_info> rbegin (scendere::transaction const & transaction_a) const override
	{
		return store.template make_iterator<scendere::account, scendere::account_info> (transaction_a, tables::accounts, false);
	}

	scendere::store_iterator<scendere::account, scendere::account_info> end () const override
	{
		return scendere::store_iterator<scendere::account, scendere::account_info> (nullptr);
	}

	void for_each_par (std::function<void (scendere::read_transaction const &, scendere::store_iterator<scendere::account, scendere::account_info>, scendere::store_iterator<scendere::account, scendere::account_info>)> const & action_a) const override
	{
		parallel_traversal<scendere::uint256_t> (
		[&action_a, this] (scendere::uint256_t const & start, scendere::uint256_t const & end, bool const is_last) {
			auto transaction (this->store.tx_begin_read ());
			action_a (transaction, this->begin (transaction, start), !is_last ? this->begin (transaction, end) : this->end ());
		});
	}
};

}
