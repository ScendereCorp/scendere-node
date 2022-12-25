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
class pending_store_partial : public pending_store
{
private:
	scendere::store_partial<Val, Derived_Store> & store;

	friend void release_assert_success<Val, Derived_Store> (store_partial<Val, Derived_Store> const &, int const);

public:
	explicit pending_store_partial (scendere::store_partial<Val, Derived_Store> & store_a) :
		store (store_a){};

	void put (scendere::write_transaction const & transaction_a, scendere::pending_key const & key_a, scendere::pending_info const & pending_info_a) override
	{
		scendere::db_val<Val> pending (pending_info_a);
		auto status = store.put (transaction_a, tables::pending, key_a, pending);
		release_assert_success (store, status);
	}

	void del (scendere::write_transaction const & transaction_a, scendere::pending_key const & key_a) override
	{
		auto status = store.del (transaction_a, tables::pending, key_a);
		release_assert_success (store, status);
	}

	bool get (scendere::transaction const & transaction_a, scendere::pending_key const & key_a, scendere::pending_info & pending_a) override
	{
		scendere::db_val<Val> value;
		scendere::db_val<Val> key (key_a);
		auto status1 = store.get (transaction_a, tables::pending, key, value);
		release_assert (store.success (status1) || store.not_found (status1));
		bool result (true);
		if (store.success (status1))
		{
			scendere::bufferstream stream (reinterpret_cast<uint8_t const *> (value.data ()), value.size ());
			result = pending_a.deserialize (stream);
		}
		return result;
	}

	bool exists (scendere::transaction const & transaction_a, scendere::pending_key const & key_a) override
	{
		auto iterator (begin (transaction_a, key_a));
		return iterator != end () && scendere::pending_key (iterator->first) == key_a;
	}

	bool any (scendere::transaction const & transaction_a, scendere::account const & account_a) override
	{
		auto iterator (begin (transaction_a, scendere::pending_key (account_a, 0)));
		return iterator != end () && scendere::pending_key (iterator->first).account == account_a;
	}

	scendere::store_iterator<scendere::pending_key, scendere::pending_info> begin (scendere::transaction const & transaction_a, scendere::pending_key const & key_a) const override
	{
		return store.template make_iterator<scendere::pending_key, scendere::pending_info> (transaction_a, tables::pending, scendere::db_val<Val> (key_a));
	}

	scendere::store_iterator<scendere::pending_key, scendere::pending_info> begin (scendere::transaction const & transaction_a) const override
	{
		return store.template make_iterator<scendere::pending_key, scendere::pending_info> (transaction_a, tables::pending);
	}

	scendere::store_iterator<scendere::pending_key, scendere::pending_info> end () const override
	{
		return scendere::store_iterator<scendere::pending_key, scendere::pending_info> (nullptr);
	}

	void for_each_par (std::function<void (scendere::read_transaction const &, scendere::store_iterator<scendere::pending_key, scendere::pending_info>, scendere::store_iterator<scendere::pending_key, scendere::pending_info>)> const & action_a) const override
	{
		parallel_traversal<scendere::uint512_t> (
		[&action_a, this] (scendere::uint512_t const & start, scendere::uint512_t const & end, bool const is_last) {
			scendere::uint512_union union_start (start);
			scendere::uint512_union union_end (end);
			scendere::pending_key key_start (union_start.uint256s[0].number (), union_start.uint256s[1].number ());
			scendere::pending_key key_end (union_end.uint256s[0].number (), union_end.uint256s[1].number ());
			auto transaction (this->store.tx_begin_read ());
			action_a (transaction, this->begin (transaction, key_start), !is_last ? this->begin (transaction, key_end) : this->end ());
		});
	}
};

}
