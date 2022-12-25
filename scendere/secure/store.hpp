#pragma once

#include <scendere/crypto_lib/random_pool.hpp>
#include <scendere/lib/diagnosticsconfig.hpp>
#include <scendere/lib/lmdbconfig.hpp>
#include <scendere/lib/logger_mt.hpp>
#include <scendere/lib/memory.hpp>
#include <scendere/lib/rocksdbconfig.hpp>
#include <scendere/secure/buffer.hpp>
#include <scendere/secure/common.hpp>
#include <scendere/secure/versioning.hpp>

#include <boost/endian/conversion.hpp>
#include <boost/polymorphic_cast.hpp>

#include <stack>

namespace scendere
{
// Move to versioning with a specific version if required for a future upgrade
template <typename T>
class block_w_sideband_v18
{
public:
	std::shared_ptr<T> block;
	scendere::block_sideband_v18 sideband;
};

class block_w_sideband
{
public:
	std::shared_ptr<scendere::block> block;
	scendere::block_sideband sideband;
};

/**
 * Encapsulates database specific container
 */
template <typename Val>
class db_val
{
public:
	db_val (Val const & value_a) :
		value (value_a)
	{
	}

	db_val () :
		db_val (0, nullptr)
	{
	}

	db_val (std::nullptr_t) :
		db_val (0, this)
	{
	}

	db_val (scendere::uint128_union const & val_a) :
		db_val (sizeof (val_a), const_cast<scendere::uint128_union *> (&val_a))
	{
	}

	db_val (scendere::uint256_union const & val_a) :
		db_val (sizeof (val_a), const_cast<scendere::uint256_union *> (&val_a))
	{
	}

	db_val (scendere::uint512_union const & val_a) :
		db_val (sizeof (val_a), const_cast<scendere::uint512_union *> (&val_a))
	{
	}

	db_val (scendere::qualified_root const & val_a) :
		db_val (sizeof (val_a), const_cast<scendere::qualified_root *> (&val_a))
	{
	}

	db_val (scendere::account_info const & val_a) :
		db_val (val_a.db_size (), const_cast<scendere::account_info *> (&val_a))
	{
	}

	db_val (scendere::account_info_v14 const & val_a) :
		db_val (val_a.db_size (), const_cast<scendere::account_info_v14 *> (&val_a))
	{
	}

	db_val (scendere::pending_info const & val_a) :
		db_val (val_a.db_size (), const_cast<scendere::pending_info *> (&val_a))
	{
		static_assert (std::is_standard_layout<scendere::pending_info>::value, "Standard layout is required");
	}

	db_val (scendere::pending_info_v14 const & val_a) :
		db_val (val_a.db_size (), const_cast<scendere::pending_info_v14 *> (&val_a))
	{
		static_assert (std::is_standard_layout<scendere::pending_info_v14>::value, "Standard layout is required");
	}

	db_val (scendere::pending_key const & val_a) :
		db_val (sizeof (val_a), const_cast<scendere::pending_key *> (&val_a))
	{
		static_assert (std::is_standard_layout<scendere::pending_key>::value, "Standard layout is required");
	}

	db_val (scendere::unchecked_info const & val_a) :
		buffer (std::make_shared<std::vector<uint8_t>> ())
	{
		{
			scendere::vectorstream stream (*buffer);
			val_a.serialize (stream);
		}
		convert_buffer_to_value ();
	}

	db_val (scendere::unchecked_key const & val_a) :
		db_val (sizeof (val_a), const_cast<scendere::unchecked_key *> (&val_a))
	{
		static_assert (std::is_standard_layout<scendere::unchecked_key>::value, "Standard layout is required");
	}

	db_val (scendere::confirmation_height_info const & val_a) :
		buffer (std::make_shared<std::vector<uint8_t>> ())
	{
		{
			scendere::vectorstream stream (*buffer);
			val_a.serialize (stream);
		}
		convert_buffer_to_value ();
	}

	db_val (scendere::block_info const & val_a) :
		db_val (sizeof (val_a), const_cast<scendere::block_info *> (&val_a))
	{
		static_assert (std::is_standard_layout<scendere::block_info>::value, "Standard layout is required");
	}

	db_val (scendere::endpoint_key const & val_a) :
		db_val (sizeof (val_a), const_cast<scendere::endpoint_key *> (&val_a))
	{
		static_assert (std::is_standard_layout<scendere::endpoint_key>::value, "Standard layout is required");
	}

	db_val (std::shared_ptr<scendere::block> const & val_a) :
		buffer (std::make_shared<std::vector<uint8_t>> ())
	{
		{
			scendere::vectorstream stream (*buffer);
			scendere::serialize_block (stream, *val_a);
		}
		convert_buffer_to_value ();
	}

	db_val (uint64_t val_a) :
		buffer (std::make_shared<std::vector<uint8_t>> ())
	{
		{
			boost::endian::native_to_big_inplace (val_a);
			scendere::vectorstream stream (*buffer);
			scendere::write (stream, val_a);
		}
		convert_buffer_to_value ();
	}

	explicit operator scendere::account_info () const
	{
		scendere::account_info result;
		debug_assert (size () == result.db_size ());
		std::copy (reinterpret_cast<uint8_t const *> (data ()), reinterpret_cast<uint8_t const *> (data ()) + result.db_size (), reinterpret_cast<uint8_t *> (&result));
		return result;
	}

	explicit operator scendere::account_info_v14 () const
	{
		scendere::account_info_v14 result;
		debug_assert (size () == result.db_size ());
		std::copy (reinterpret_cast<uint8_t const *> (data ()), reinterpret_cast<uint8_t const *> (data ()) + result.db_size (), reinterpret_cast<uint8_t *> (&result));
		return result;
	}

	explicit operator scendere::block_info () const
	{
		scendere::block_info result;
		debug_assert (size () == sizeof (result));
		static_assert (sizeof (scendere::block_info::account) + sizeof (scendere::block_info::balance) == sizeof (result), "Packed class");
		std::copy (reinterpret_cast<uint8_t const *> (data ()), reinterpret_cast<uint8_t const *> (data ()) + sizeof (result), reinterpret_cast<uint8_t *> (&result));
		return result;
	}

	explicit operator scendere::pending_info_v14 () const
	{
		scendere::pending_info_v14 result;
		debug_assert (size () == result.db_size ());
		std::copy (reinterpret_cast<uint8_t const *> (data ()), reinterpret_cast<uint8_t const *> (data ()) + result.db_size (), reinterpret_cast<uint8_t *> (&result));
		return result;
	}

	explicit operator scendere::pending_info () const
	{
		scendere::pending_info result;
		debug_assert (size () == result.db_size ());
		std::copy (reinterpret_cast<uint8_t const *> (data ()), reinterpret_cast<uint8_t const *> (data ()) + result.db_size (), reinterpret_cast<uint8_t *> (&result));
		return result;
	}

	explicit operator scendere::pending_key () const
	{
		scendere::pending_key result;
		debug_assert (size () == sizeof (result));
		static_assert (sizeof (scendere::pending_key::account) + sizeof (scendere::pending_key::hash) == sizeof (result), "Packed class");
		std::copy (reinterpret_cast<uint8_t const *> (data ()), reinterpret_cast<uint8_t const *> (data ()) + sizeof (result), reinterpret_cast<uint8_t *> (&result));
		return result;
	}

	explicit operator scendere::confirmation_height_info () const
	{
		scendere::bufferstream stream (reinterpret_cast<uint8_t const *> (data ()), size ());
		scendere::confirmation_height_info result;
		bool error (result.deserialize (stream));
		(void)error;
		debug_assert (!error);
		return result;
	}

	explicit operator scendere::unchecked_info () const
	{
		scendere::bufferstream stream (reinterpret_cast<uint8_t const *> (data ()), size ());
		scendere::unchecked_info result;
		bool error (result.deserialize (stream));
		(void)error;
		debug_assert (!error);
		return result;
	}

	explicit operator scendere::unchecked_key () const
	{
		scendere::unchecked_key result;
		debug_assert (size () == sizeof (result));
		static_assert (sizeof (scendere::unchecked_key::previous) + sizeof (scendere::pending_key::hash) == sizeof (result), "Packed class");
		std::copy (reinterpret_cast<uint8_t const *> (data ()), reinterpret_cast<uint8_t const *> (data ()) + sizeof (result), reinterpret_cast<uint8_t *> (&result));
		return result;
	}

	explicit operator scendere::uint128_union () const
	{
		return convert<scendere::uint128_union> ();
	}

	explicit operator scendere::amount () const
	{
		return convert<scendere::amount> ();
	}

	explicit operator scendere::block_hash () const
	{
		return convert<scendere::block_hash> ();
	}

	explicit operator scendere::public_key () const
	{
		return convert<scendere::public_key> ();
	}

	explicit operator scendere::qualified_root () const
	{
		return convert<scendere::qualified_root> ();
	}

	explicit operator scendere::uint256_union () const
	{
		return convert<scendere::uint256_union> ();
	}

	explicit operator scendere::uint512_union () const
	{
		return convert<scendere::uint512_union> ();
	}

	explicit operator std::array<char, 64> () const
	{
		scendere::bufferstream stream (reinterpret_cast<uint8_t const *> (data ()), size ());
		std::array<char, 64> result;
		auto error = scendere::try_read (stream, result);
		(void)error;
		debug_assert (!error);
		return result;
	}

	explicit operator scendere::endpoint_key () const
	{
		scendere::endpoint_key result;
		std::copy (reinterpret_cast<uint8_t const *> (data ()), reinterpret_cast<uint8_t const *> (data ()) + sizeof (result), reinterpret_cast<uint8_t *> (&result));
		return result;
	}

	template <class Block>
	explicit operator block_w_sideband_v18<Block> () const
	{
		scendere::bufferstream stream (reinterpret_cast<uint8_t const *> (data ()), size ());
		auto error (false);
		block_w_sideband_v18<Block> block_w_sideband;
		block_w_sideband.block = std::make_shared<Block> (error, stream);
		release_assert (!error);

		error = block_w_sideband.sideband.deserialize (stream, block_w_sideband.block->type ());
		release_assert (!error);

		return block_w_sideband;
	}

	explicit operator block_w_sideband () const
	{
		scendere::bufferstream stream (reinterpret_cast<uint8_t const *> (data ()), size ());
		scendere::block_w_sideband block_w_sideband;
		block_w_sideband.block = (scendere::deserialize_block (stream));
		auto error = block_w_sideband.sideband.deserialize (stream, block_w_sideband.block->type ());
		release_assert (!error);
		block_w_sideband.block->sideband_set (block_w_sideband.sideband);
		return block_w_sideband;
	}

	explicit operator state_block_w_sideband_v14 () const
	{
		scendere::bufferstream stream (reinterpret_cast<uint8_t const *> (data ()), size ());
		auto error (false);
		scendere::state_block_w_sideband_v14 block_w_sideband;
		block_w_sideband.state_block = std::make_shared<scendere::state_block> (error, stream);
		debug_assert (!error);

		block_w_sideband.sideband.type = scendere::block_type::state;
		error = block_w_sideband.sideband.deserialize (stream);
		debug_assert (!error);

		return block_w_sideband;
	}

	explicit operator std::nullptr_t () const
	{
		return nullptr;
	}

	explicit operator scendere::no_value () const
	{
		return no_value::dummy;
	}

	explicit operator std::shared_ptr<scendere::block> () const
	{
		scendere::bufferstream stream (reinterpret_cast<uint8_t const *> (data ()), size ());
		std::shared_ptr<scendere::block> result (scendere::deserialize_block (stream));
		return result;
	}

	template <typename Block>
	std::shared_ptr<Block> convert_to_block () const
	{
		scendere::bufferstream stream (reinterpret_cast<uint8_t const *> (data ()), size ());
		auto error (false);
		auto result (std::make_shared<Block> (error, stream));
		debug_assert (!error);
		return result;
	}

	explicit operator std::shared_ptr<scendere::send_block> () const
	{
		return convert_to_block<scendere::send_block> ();
	}

	explicit operator std::shared_ptr<scendere::receive_block> () const
	{
		return convert_to_block<scendere::receive_block> ();
	}

	explicit operator std::shared_ptr<scendere::open_block> () const
	{
		return convert_to_block<scendere::open_block> ();
	}

	explicit operator std::shared_ptr<scendere::change_block> () const
	{
		return convert_to_block<scendere::change_block> ();
	}

	explicit operator std::shared_ptr<scendere::state_block> () const
	{
		return convert_to_block<scendere::state_block> ();
	}

	explicit operator std::shared_ptr<scendere::vote> () const
	{
		scendere::bufferstream stream (reinterpret_cast<uint8_t const *> (data ()), size ());
		auto error (false);
		auto result (scendere::make_shared<scendere::vote> (error, stream));
		debug_assert (!error);
		return result;
	}

	explicit operator uint64_t () const
	{
		uint64_t result;
		scendere::bufferstream stream (reinterpret_cast<uint8_t const *> (data ()), size ());
		auto error (scendere::try_read (stream, result));
		(void)error;
		debug_assert (!error);
		boost::endian::big_to_native_inplace (result);
		return result;
	}

	operator Val * () const
	{
		// Allow passing a temporary to a non-c++ function which doesn't have constness
		return const_cast<Val *> (&value);
	}

	operator Val const & () const
	{
		return value;
	}

	// Must be specialized
	void * data () const;
	size_t size () const;
	db_val (size_t size_a, void * data_a);
	void convert_buffer_to_value ();

	Val value;
	std::shared_ptr<std::vector<uint8_t>> buffer;

private:
	template <typename T>
	T convert () const
	{
		T result;
		debug_assert (size () == sizeof (result));
		std::copy (reinterpret_cast<uint8_t const *> (data ()), reinterpret_cast<uint8_t const *> (data ()) + sizeof (result), result.bytes.data ());
		return result;
	}
};

class transaction;
class store;

/**
 * Determine the representative for this block
 */
class representative_visitor final : public scendere::block_visitor
{
public:
	representative_visitor (scendere::transaction const & transaction_a, scendere::store & store_a);
	~representative_visitor () = default;
	void compute (scendere::block_hash const & hash_a);
	void send_block (scendere::send_block const & block_a) override;
	void receive_block (scendere::receive_block const & block_a) override;
	void open_block (scendere::open_block const & block_a) override;
	void change_block (scendere::change_block const & block_a) override;
	void state_block (scendere::state_block const & block_a) override;
	scendere::transaction const & transaction;
	scendere::store & store;
	scendere::block_hash current;
	scendere::block_hash result;
};
template <typename T, typename U>
class store_iterator_impl
{
public:
	virtual ~store_iterator_impl () = default;
	virtual scendere::store_iterator_impl<T, U> & operator++ () = 0;
	virtual scendere::store_iterator_impl<T, U> & operator-- () = 0;
	virtual bool operator== (scendere::store_iterator_impl<T, U> const & other_a) const = 0;
	virtual bool is_end_sentinal () const = 0;
	virtual void fill (std::pair<T, U> &) const = 0;
	scendere::store_iterator_impl<T, U> & operator= (scendere::store_iterator_impl<T, U> const &) = delete;
	bool operator== (scendere::store_iterator_impl<T, U> const * other_a) const
	{
		return (other_a != nullptr && *this == *other_a) || (other_a == nullptr && is_end_sentinal ());
	}
	bool operator!= (scendere::store_iterator_impl<T, U> const & other_a) const
	{
		return !(*this == other_a);
	}
};
/**
 * Iterates the key/value pairs of a transaction
 */
template <typename T, typename U>
class store_iterator final
{
public:
	store_iterator (std::nullptr_t)
	{
	}
	store_iterator (std::unique_ptr<scendere::store_iterator_impl<T, U>> impl_a) :
		impl (std::move (impl_a))
	{
		impl->fill (current);
	}
	store_iterator (scendere::store_iterator<T, U> && other_a) :
		current (std::move (other_a.current)),
		impl (std::move (other_a.impl))
	{
	}
	scendere::store_iterator<T, U> & operator++ ()
	{
		++*impl;
		impl->fill (current);
		return *this;
	}
	scendere::store_iterator<T, U> & operator-- ()
	{
		--*impl;
		impl->fill (current);
		return *this;
	}
	scendere::store_iterator<T, U> & operator= (scendere::store_iterator<T, U> && other_a) noexcept
	{
		impl = std::move (other_a.impl);
		current = std::move (other_a.current);
		return *this;
	}
	scendere::store_iterator<T, U> & operator= (scendere::store_iterator<T, U> const &) = delete;
	std::pair<T, U> * operator-> ()
	{
		return &current;
	}
	bool operator== (scendere::store_iterator<T, U> const & other_a) const
	{
		return (impl == nullptr && other_a.impl == nullptr) || (impl != nullptr && *impl == other_a.impl.get ()) || (other_a.impl != nullptr && *other_a.impl == impl.get ());
	}
	bool operator!= (scendere::store_iterator<T, U> const & other_a) const
	{
		return !(*this == other_a);
	}

private:
	std::pair<T, U> current;
	std::unique_ptr<scendere::store_iterator_impl<T, U>> impl;
};

// Keep this in alphabetical order
enum class tables
{
	accounts,
	blocks,
	confirmation_height,
	default_unused, // RocksDB only
	final_votes,
	frontiers,
	meta,
	online_weight,
	peers,
	pending,
	pruned,
	unchecked,
	vote
};

class transaction_impl
{
public:
	virtual ~transaction_impl () = default;
	virtual void * get_handle () const = 0;
};

class read_transaction_impl : public transaction_impl
{
public:
	virtual void reset () = 0;
	virtual void renew () = 0;
};

class write_transaction_impl : public transaction_impl
{
public:
	virtual void commit () = 0;
	virtual void renew () = 0;
	virtual bool contains (scendere::tables table_a) const = 0;
};

class transaction
{
public:
	virtual ~transaction () = default;
	virtual void * get_handle () const = 0;
};

/**
 * RAII wrapper of a read MDB_txn where the constructor starts the transaction
 * and the destructor aborts it.
 */
class read_transaction final : public transaction
{
public:
	explicit read_transaction (std::unique_ptr<scendere::read_transaction_impl> read_transaction_impl);
	void * get_handle () const override;
	void reset () const;
	void renew () const;
	void refresh () const;

private:
	std::unique_ptr<scendere::read_transaction_impl> impl;
};

/**
 * RAII wrapper of a read-write MDB_txn where the constructor starts the transaction
 * and the destructor commits it.
 */
class write_transaction final : public transaction
{
public:
	explicit write_transaction (std::unique_ptr<scendere::write_transaction_impl> write_transaction_impl);
	void * get_handle () const override;
	void commit ();
	void renew ();
	void refresh ();
	bool contains (scendere::tables table_a) const;

private:
	std::unique_ptr<scendere::write_transaction_impl> impl;
};

class ledger_cache;

/**
 * Manages frontier storage and iteration
 */
class frontier_store
{
public:
	virtual void put (scendere::write_transaction const &, scendere::block_hash const &, scendere::account const &) = 0;
	virtual scendere::account get (scendere::transaction const &, scendere::block_hash const &) const = 0;
	virtual void del (scendere::write_transaction const &, scendere::block_hash const &) = 0;
	virtual scendere::store_iterator<scendere::block_hash, scendere::account> begin (scendere::transaction const &) const = 0;
	virtual scendere::store_iterator<scendere::block_hash, scendere::account> begin (scendere::transaction const &, scendere::block_hash const &) const = 0;
	virtual scendere::store_iterator<scendere::block_hash, scendere::account> end () const = 0;
	virtual void for_each_par (std::function<void (scendere::read_transaction const &, scendere::store_iterator<scendere::block_hash, scendere::account>, scendere::store_iterator<scendere::block_hash, scendere::account>)> const & action_a) const = 0;
};

/**
 * Manages account storage and iteration
 */
class account_store
{
public:
	virtual void put (scendere::write_transaction const &, scendere::account const &, scendere::account_info const &) = 0;
	virtual bool get (scendere::transaction const &, scendere::account const &, scendere::account_info &) = 0;
	virtual void del (scendere::write_transaction const &, scendere::account const &) = 0;
	virtual bool exists (scendere::transaction const &, scendere::account const &) = 0;
	virtual size_t count (scendere::transaction const &) = 0;
	virtual scendere::store_iterator<scendere::account, scendere::account_info> begin (scendere::transaction const &, scendere::account const &) const = 0;
	virtual scendere::store_iterator<scendere::account, scendere::account_info> begin (scendere::transaction const &) const = 0;
	virtual scendere::store_iterator<scendere::account, scendere::account_info> rbegin (scendere::transaction const &) const = 0;
	virtual scendere::store_iterator<scendere::account, scendere::account_info> end () const = 0;
	virtual void for_each_par (std::function<void (scendere::read_transaction const &, scendere::store_iterator<scendere::account, scendere::account_info>, scendere::store_iterator<scendere::account, scendere::account_info>)> const &) const = 0;
};

/**
 * Manages pending storage and iteration
 */
class pending_store
{
public:
	virtual void put (scendere::write_transaction const &, scendere::pending_key const &, scendere::pending_info const &) = 0;
	virtual void del (scendere::write_transaction const &, scendere::pending_key const &) = 0;
	virtual bool get (scendere::transaction const &, scendere::pending_key const &, scendere::pending_info &) = 0;
	virtual bool exists (scendere::transaction const &, scendere::pending_key const &) = 0;
	virtual bool any (scendere::transaction const &, scendere::account const &) = 0;
	virtual scendere::store_iterator<scendere::pending_key, scendere::pending_info> begin (scendere::transaction const &, scendere::pending_key const &) const = 0;
	virtual scendere::store_iterator<scendere::pending_key, scendere::pending_info> begin (scendere::transaction const &) const = 0;
	virtual scendere::store_iterator<scendere::pending_key, scendere::pending_info> end () const = 0;
	virtual void for_each_par (std::function<void (scendere::read_transaction const &, scendere::store_iterator<scendere::pending_key, scendere::pending_info>, scendere::store_iterator<scendere::pending_key, scendere::pending_info>)> const & action_a) const = 0;
};

/**
 * Manages peer storage and iteration
 */
class peer_store
{
public:
	virtual void put (scendere::write_transaction const & transaction_a, scendere::endpoint_key const & endpoint_a) = 0;
	virtual void del (scendere::write_transaction const & transaction_a, scendere::endpoint_key const & endpoint_a) = 0;
	virtual bool exists (scendere::transaction const & transaction_a, scendere::endpoint_key const & endpoint_a) const = 0;
	virtual size_t count (scendere::transaction const & transaction_a) const = 0;
	virtual void clear (scendere::write_transaction const & transaction_a) = 0;
	virtual scendere::store_iterator<scendere::endpoint_key, scendere::no_value> begin (scendere::transaction const & transaction_a) const = 0;
	virtual scendere::store_iterator<scendere::endpoint_key, scendere::no_value> end () const = 0;
};

/**
 * Manages online weight storage and iteration
 */
class online_weight_store
{
public:
	virtual void put (scendere::write_transaction const &, uint64_t, scendere::amount const &) = 0;
	virtual void del (scendere::write_transaction const &, uint64_t) = 0;
	virtual scendere::store_iterator<uint64_t, scendere::amount> begin (scendere::transaction const &) const = 0;
	virtual scendere::store_iterator<uint64_t, scendere::amount> rbegin (scendere::transaction const &) const = 0;
	virtual scendere::store_iterator<uint64_t, scendere::amount> end () const = 0;
	virtual size_t count (scendere::transaction const &) const = 0;
	virtual void clear (scendere::write_transaction const &) = 0;
};

/**
 * Manages pruned storage and iteration
 */
class pruned_store
{
public:
	virtual void put (scendere::write_transaction const & transaction_a, scendere::block_hash const & hash_a) = 0;
	virtual void del (scendere::write_transaction const & transaction_a, scendere::block_hash const & hash_a) = 0;
	virtual bool exists (scendere::transaction const & transaction_a, scendere::block_hash const & hash_a) const = 0;
	virtual scendere::block_hash random (scendere::transaction const & transaction_a) = 0;
	virtual size_t count (scendere::transaction const & transaction_a) const = 0;
	virtual void clear (scendere::write_transaction const &) = 0;
	virtual scendere::store_iterator<scendere::block_hash, std::nullptr_t> begin (scendere::transaction const & transaction_a, scendere::block_hash const & hash_a) const = 0;
	virtual scendere::store_iterator<scendere::block_hash, std::nullptr_t> begin (scendere::transaction const & transaction_a) const = 0;
	virtual scendere::store_iterator<scendere::block_hash, std::nullptr_t> end () const = 0;
	virtual void for_each_par (std::function<void (scendere::read_transaction const &, scendere::store_iterator<scendere::block_hash, std::nullptr_t>, scendere::store_iterator<scendere::block_hash, std::nullptr_t>)> const & action_a) const = 0;
};

/**
 * Manages confirmation height storage and iteration
 */
class confirmation_height_store
{
public:
	virtual void put (scendere::write_transaction const & transaction_a, scendere::account const & account_a, scendere::confirmation_height_info const & confirmation_height_info_a) = 0;

	/** Retrieves confirmation height info relating to an account.
	 *  The parameter confirmation_height_info_a is always written.
	 *  On error, the confirmation height and frontier hash are set to 0.
	 *  Ruturns true on error, false on success.
	 */
	virtual bool get (scendere::transaction const & transaction_a, scendere::account const & account_a, scendere::confirmation_height_info & confirmation_height_info_a) = 0;

	virtual bool exists (scendere::transaction const & transaction_a, scendere::account const & account_a) const = 0;
	virtual void del (scendere::write_transaction const & transaction_a, scendere::account const & account_a) = 0;
	virtual uint64_t count (scendere::transaction const & transaction_a) = 0;
	virtual void clear (scendere::write_transaction const &, scendere::account const &) = 0;
	virtual void clear (scendere::write_transaction const &) = 0;
	virtual scendere::store_iterator<scendere::account, scendere::confirmation_height_info> begin (scendere::transaction const & transaction_a, scendere::account const & account_a) const = 0;
	virtual scendere::store_iterator<scendere::account, scendere::confirmation_height_info> begin (scendere::transaction const & transaction_a) const = 0;
	virtual scendere::store_iterator<scendere::account, scendere::confirmation_height_info> end () const = 0;
	virtual void for_each_par (std::function<void (scendere::read_transaction const &, scendere::store_iterator<scendere::account, scendere::confirmation_height_info>, scendere::store_iterator<scendere::account, scendere::confirmation_height_info>)> const &) const = 0;
};

/**
 * Manages unchecked storage and iteration
 */
class unchecked_store
{
public:
	using iterator = scendere::store_iterator<scendere::unchecked_key, scendere::unchecked_info>;

	virtual void clear (scendere::write_transaction const &) = 0;
	virtual void put (scendere::write_transaction const &, scendere::hash_or_account const & dependency, scendere::unchecked_info const &) = 0;
	std::pair<iterator, iterator> equal_range (scendere::transaction const & transaction, scendere::block_hash const & dependency);
	std::pair<iterator, iterator> full_range (scendere::transaction const & transaction);
	std::vector<scendere::unchecked_info> get (scendere::transaction const &, scendere::block_hash const &);
	virtual bool exists (scendere::transaction const & transaction_a, scendere::unchecked_key const & unchecked_key_a) = 0;
	virtual void del (scendere::write_transaction const &, scendere::unchecked_key const &) = 0;
	virtual iterator begin (scendere::transaction const &) const = 0;
	virtual iterator lower_bound (scendere::transaction const &, scendere::unchecked_key const &) const = 0;
	virtual iterator end () const = 0;
	virtual size_t count (scendere::transaction const &) = 0;
	virtual void for_each_par (std::function<void (scendere::read_transaction const &, iterator, iterator)> const & action_a) const = 0;
};

/**
 * Manages final vote storage and iteration
 */
class final_vote_store
{
public:
	virtual bool put (scendere::write_transaction const & transaction_a, scendere::qualified_root const & root_a, scendere::block_hash const & hash_a) = 0;
	virtual std::vector<scendere::block_hash> get (scendere::transaction const & transaction_a, scendere::root const & root_a) = 0;
	virtual void del (scendere::write_transaction const & transaction_a, scendere::root const & root_a) = 0;
	virtual size_t count (scendere::transaction const & transaction_a) const = 0;
	virtual void clear (scendere::write_transaction const &, scendere::root const &) = 0;
	virtual void clear (scendere::write_transaction const &) = 0;
	virtual scendere::store_iterator<scendere::qualified_root, scendere::block_hash> begin (scendere::transaction const & transaction_a, scendere::qualified_root const & root_a) const = 0;
	virtual scendere::store_iterator<scendere::qualified_root, scendere::block_hash> begin (scendere::transaction const & transaction_a) const = 0;
	virtual scendere::store_iterator<scendere::qualified_root, scendere::block_hash> end () const = 0;
	virtual void for_each_par (std::function<void (scendere::read_transaction const &, scendere::store_iterator<scendere::qualified_root, scendere::block_hash>, scendere::store_iterator<scendere::qualified_root, scendere::block_hash>)> const & action_a) const = 0;
};

/**
 * Manages version storage
 */
class version_store
{
public:
	virtual void put (scendere::write_transaction const &, int) = 0;
	virtual int get (scendere::transaction const &) const = 0;
};

/**
 * Manages block storage and iteration
 */
class block_store
{
public:
	virtual void put (scendere::write_transaction const &, scendere::block_hash const &, scendere::block const &) = 0;
	virtual void raw_put (scendere::write_transaction const &, std::vector<uint8_t> const &, scendere::block_hash const &) = 0;
	virtual scendere::block_hash successor (scendere::transaction const &, scendere::block_hash const &) const = 0;
	virtual void successor_clear (scendere::write_transaction const &, scendere::block_hash const &) = 0;
	virtual std::shared_ptr<scendere::block> get (scendere::transaction const &, scendere::block_hash const &) const = 0;
	virtual std::shared_ptr<scendere::block> get_no_sideband (scendere::transaction const &, scendere::block_hash const &) const = 0;
	virtual std::shared_ptr<scendere::block> random (scendere::transaction const &) = 0;
	virtual void del (scendere::write_transaction const &, scendere::block_hash const &) = 0;
	virtual bool exists (scendere::transaction const &, scendere::block_hash const &) = 0;
	virtual uint64_t count (scendere::transaction const &) = 0;
	virtual scendere::account account (scendere::transaction const &, scendere::block_hash const &) const = 0;
	virtual scendere::account account_calculated (scendere::block const &) const = 0;
	virtual scendere::store_iterator<scendere::block_hash, block_w_sideband> begin (scendere::transaction const &, scendere::block_hash const &) const = 0;
	virtual scendere::store_iterator<scendere::block_hash, block_w_sideband> begin (scendere::transaction const &) const = 0;
	virtual scendere::store_iterator<scendere::block_hash, block_w_sideband> end () const = 0;
	virtual scendere::uint128_t balance (scendere::transaction const &, scendere::block_hash const &) = 0;
	virtual scendere::uint128_t balance_calculated (std::shared_ptr<scendere::block> const &) const = 0;
	virtual scendere::epoch version (scendere::transaction const &, scendere::block_hash const &) = 0;
	virtual void for_each_par (std::function<void (scendere::read_transaction const &, scendere::store_iterator<scendere::block_hash, block_w_sideband>, scendere::store_iterator<scendere::block_hash, block_w_sideband>)> const & action_a) const = 0;
	virtual uint64_t account_height (scendere::transaction const & transaction_a, scendere::block_hash const & hash_a) const = 0;
};

class unchecked_map;
/**
 * Store manager
 */
class store
{
public:
	// clang-format off
	explicit store (
		scendere::block_store &,
		scendere::frontier_store &,
		scendere::account_store &,
		scendere::pending_store &,
		scendere::unchecked_store &,
		scendere::online_weight_store &,
		scendere::pruned_store &,
		scendere::peer_store &,
		scendere::confirmation_height_store &,
		scendere::final_vote_store &,
		scendere::version_store &
	);
	// clang-format on
	virtual ~store () = default;
	virtual void initialize (scendere::write_transaction const &, scendere::ledger_cache &) = 0;
	virtual bool root_exists (scendere::transaction const &, scendere::root const &) = 0;

	block_store & block;
	frontier_store & frontier;
	account_store & account;
	pending_store & pending;

private:
	unchecked_store & unchecked;

public:
	online_weight_store & online_weight;
	pruned_store & pruned;
	peer_store & peer;
	confirmation_height_store & confirmation_height;
	final_vote_store & final_vote;
	version_store & version;

	virtual unsigned max_block_write_batch_num () const = 0;

	virtual bool copy_db (boost::filesystem::path const & destination) = 0;
	virtual void rebuild_db (scendere::write_transaction const & transaction_a) = 0;

	/** Not applicable to all sub-classes */
	virtual void serialize_mdb_tracker (boost::property_tree::ptree &, std::chrono::milliseconds, std::chrono::milliseconds){};
	virtual void serialize_memory_stats (boost::property_tree::ptree &) = 0;

	virtual bool init_error () const = 0;

	/** Start read-write transaction */
	virtual scendere::write_transaction tx_begin_write (std::vector<scendere::tables> const & tables_to_lock = {}, std::vector<scendere::tables> const & tables_no_lock = {}) = 0;

	/** Start read-only transaction */
	virtual scendere::read_transaction tx_begin_read () const = 0;

	virtual std::string vendor_get () const = 0;

	friend class unchecked_map;
};

std::unique_ptr<scendere::store> make_store (scendere::logger_mt & logger, boost::filesystem::path const & path, scendere::ledger_constants & constants, bool open_read_only = false, bool add_db_postfix = false, scendere::rocksdb_config const & rocksdb_config = scendere::rocksdb_config{}, scendere::txn_tracking_config const & txn_tracking_config_a = scendere::txn_tracking_config{}, std::chrono::milliseconds block_processor_batch_max_time_a = std::chrono::milliseconds (5000), scendere::lmdb_config const & lmdb_config_a = scendere::lmdb_config{}, bool backup_before_upgrade = false);
}

namespace std
{
template <>
struct hash<::scendere::tables>
{
	size_t operator() (::scendere::tables const & table_a) const
	{
		return static_cast<size_t> (table_a);
	}
};
}
