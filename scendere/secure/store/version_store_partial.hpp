#pragma once

#include <scendere/secure/store_partial.hpp>

namespace scendere
{
template <typename Val, typename Derived_Store>
class store_partial;

template <typename Val, typename Derived_Store>
void release_assert_success (store_partial<Val, Derived_Store> const &, int const);

template <typename Val, typename Derived_Store>
class version_store_partial : public version_store
{
protected:
	scendere::store_partial<Val, Derived_Store> & store;

public:
	explicit version_store_partial (scendere::store_partial<Val, Derived_Store> & store_a) :
		store (store_a){};

	void put (scendere::write_transaction const & transaction_a, int version_a) override
	{
		scendere::uint256_union version_key (1);
		scendere::uint256_union version_value (version_a);
		auto status (store.put (transaction_a, tables::meta, scendere::db_val<Val> (version_key), scendere::db_val<Val> (version_value)));
		release_assert_success (store, status);
	}

	int get (scendere::transaction const & transaction_a) const override
	{
		scendere::uint256_union version_key (1);
		scendere::db_val<Val> data;
		auto status = store.get (transaction_a, tables::meta, scendere::db_val<Val> (version_key), data);
		int result (store.minimum_version);
		if (store.success (status))
		{
			scendere::uint256_union version_value (data);
			debug_assert (version_value.qwords[2] == 0 && version_value.qwords[1] == 0 && version_value.qwords[0] == 0);
			result = version_value.number ().convert_to<int> ();
		}
		return result;
	}
};

}
