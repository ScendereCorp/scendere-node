#pragma once

#include <scendere/lib/numbers.hpp>
#include <scendere/secure/store.hpp>

#include <lmdb/libraries/liblmdb/lmdb.h>

namespace scendere
{
class wallet_value
{
public:
	wallet_value () = default;
	wallet_value (scendere::db_val<MDB_val> const &);
	wallet_value (scendere::raw_key const &, uint64_t);
	scendere::db_val<MDB_val> val () const;
	scendere::raw_key key;
	uint64_t work;
};
}
