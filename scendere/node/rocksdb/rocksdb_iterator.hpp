#pragma once

#include <scendere/secure/store.hpp>

#include <rocksdb/db.h>
#include <rocksdb/filter_policy.h>
#include <rocksdb/options.h>
#include <rocksdb/slice.h>
#include <rocksdb/utilities/optimistic_transaction_db.h>
#include <rocksdb/utilities/transaction.h>

namespace
{
inline bool is_read (scendere::transaction const & transaction_a)
{
	return (dynamic_cast<scendere::read_transaction const *> (&transaction_a) != nullptr);
}

inline rocksdb::ReadOptions & snapshot_options (scendere::transaction const & transaction_a)
{
	debug_assert (is_read (transaction_a));
	return *static_cast<rocksdb::ReadOptions *> (transaction_a.get_handle ());
}
}

namespace scendere
{
using rocksdb_val = db_val<rocksdb::Slice>;

template <typename T, typename U>
class rocksdb_iterator : public store_iterator_impl<T, U>
{
public:
	rocksdb_iterator () = default;

	rocksdb_iterator (rocksdb::DB * db, scendere::transaction const & transaction_a, rocksdb::ColumnFamilyHandle * handle_a, rocksdb_val const * val_a, bool const direction_asc)
	{
		// Don't fill the block cache for any blocks read as a result of an iterator
		if (is_read (transaction_a))
		{
			auto read_options = snapshot_options (transaction_a);
			read_options.fill_cache = false;
			cursor.reset (db->NewIterator (read_options, handle_a));
		}
		else
		{
			rocksdb::ReadOptions ropts;
			ropts.fill_cache = false;
			cursor.reset (tx (transaction_a)->GetIterator (ropts, handle_a));
		}

		if (val_a)
		{
			cursor->Seek (*val_a);
		}
		else if (direction_asc)
		{
			cursor->SeekToFirst ();
		}
		else
		{
			cursor->SeekToLast ();
		}

		if (cursor->Valid ())
		{
			current.first = cursor->key ();
			current.second = cursor->value ();
		}
		else
		{
			clear ();
		}
	}

	rocksdb_iterator (rocksdb::DB * db, scendere::transaction const & transaction_a, rocksdb::ColumnFamilyHandle * handle_a) :
		rocksdb_iterator (db, transaction_a, handle_a, nullptr)
	{
	}

	rocksdb_iterator (scendere::rocksdb_iterator<T, U> && other_a)
	{
		cursor = other_a.cursor;
		other_a.cursor = nullptr;
		current = other_a.current;
	}

	rocksdb_iterator (scendere::rocksdb_iterator<T, U> const &) = delete;

	scendere::store_iterator_impl<T, U> & operator++ () override
	{
		cursor->Next ();
		if (cursor->Valid ())
		{
			current.first = cursor->key ();
			current.second = cursor->value ();

			if (current.first.size () != sizeof (T))
			{
				clear ();
			}
		}
		else
		{
			clear ();
		}

		return *this;
	}

	scendere::store_iterator_impl<T, U> & operator-- () override
	{
		cursor->Prev ();
		if (cursor->Valid ())
		{
			current.first = cursor->key ();
			current.second = cursor->value ();

			if (current.first.size () != sizeof (T))
			{
				clear ();
			}
		}
		else
		{
			clear ();
		}

		return *this;
	}

	std::pair<scendere::rocksdb_val, scendere::rocksdb_val> * operator-> ()
	{
		return &current;
	}

	bool operator== (scendere::store_iterator_impl<T, U> const & base_a) const override
	{
		auto const other_a (boost::polymorphic_downcast<scendere::rocksdb_iterator<T, U> const *> (&base_a));

		if (!current.first.data () && !other_a->current.first.data ())
		{
			return true;
		}
		else if (!current.first.data () || !other_a->current.first.data ())
		{
			return false;
		}

		auto result (std::memcmp (current.first.data (), other_a->current.first.data (), current.first.size ()) == 0);
		debug_assert (!result || (current.first.size () == other_a->current.first.size ()));
		debug_assert (!result || std::memcmp (current.second.data (), other_a->current.second.data (), current.second.size ()) == 0);
		debug_assert (!result || (current.second.size () == other_a->current.second.size ()));
		return result;
	}

	bool is_end_sentinal () const override
	{
		return current.first.size () == 0;
	}

	void fill (std::pair<T, U> & value_a) const override
	{
		{
			if (current.first.size () != 0)
			{
				value_a.first = static_cast<T> (current.first);
			}
			else
			{
				value_a.first = T ();
			}
			if (current.second.size () != 0)
			{
				value_a.second = static_cast<U> (current.second);
			}
			else
			{
				value_a.second = U ();
			}
		}
	}
	void clear ()
	{
		current.first = scendere::rocksdb_val{};
		current.second = scendere::rocksdb_val{};
		debug_assert (is_end_sentinal ());
	}
	scendere::rocksdb_iterator<T, U> & operator= (scendere::rocksdb_iterator<T, U> && other_a)
	{
		cursor = std::move (other_a.cursor);
		current = other_a.current;
		return *this;
	}
	scendere::store_iterator_impl<T, U> & operator= (scendere::store_iterator_impl<T, U> const &) = delete;

	std::unique_ptr<rocksdb::Iterator> cursor;
	std::pair<scendere::rocksdb_val, scendere::rocksdb_val> current;

private:
	rocksdb::Transaction * tx (scendere::transaction const & transaction_a) const
	{
		return static_cast<rocksdb::Transaction *> (transaction_a.get_handle ());
	}
};
}
