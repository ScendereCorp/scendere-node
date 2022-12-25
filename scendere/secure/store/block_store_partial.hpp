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
class block_predecessor_set;

template <typename Val, typename Derived_Store>
void release_assert_success (store_partial<Val, Derived_Store> const &, int const);

template <typename Val, typename Derived_Store>
class block_store_partial : public block_store
{
protected:
	scendere::store_partial<Val, Derived_Store> & store;

	friend class scendere::block_predecessor_set<Val, Derived_Store>;

public:
	explicit block_store_partial (scendere::store_partial<Val, Derived_Store> & store_a) :
		store (store_a){};

	void put (scendere::write_transaction const & transaction_a, scendere::block_hash const & hash_a, scendere::block const & block_a) override
	{
		debug_assert (block_a.sideband ().successor.is_zero () || exists (transaction_a, block_a.sideband ().successor));
		std::vector<uint8_t> vector;
		{
			scendere::vectorstream stream (vector);
			scendere::serialize_block (stream, block_a);
			block_a.sideband ().serialize (stream, block_a.type ());
		}
		raw_put (transaction_a, vector, hash_a);
		scendere::block_predecessor_set<Val, Derived_Store> predecessor (transaction_a, *this);
		block_a.visit (predecessor);
		debug_assert (block_a.previous ().is_zero () || successor (transaction_a, block_a.previous ()) == hash_a);
	}

	void raw_put (scendere::write_transaction const & transaction_a, std::vector<uint8_t> const & data, scendere::block_hash const & hash_a) override
	{
		scendere::db_val<Val> value{ data.size (), (void *)data.data () };
		auto status = store.put (transaction_a, tables::blocks, hash_a, value);
		release_assert_success (store, status);
	}

	scendere::block_hash successor (scendere::transaction const & transaction_a, scendere::block_hash const & hash_a) const override
	{
		auto value (block_raw_get (transaction_a, hash_a));
		scendere::block_hash result;
		if (value.size () != 0)
		{
			debug_assert (value.size () >= result.bytes.size ());
			auto type = block_type_from_raw (value.data ());
			scendere::bufferstream stream (reinterpret_cast<uint8_t const *> (value.data ()) + block_successor_offset (transaction_a, value.size (), type), result.bytes.size ());
			auto error (scendere::try_read (stream, result.bytes));
			(void)error;
			debug_assert (!error);
		}
		else
		{
			result.clear ();
		}
		return result;
	}

	void successor_clear (scendere::write_transaction const & transaction_a, scendere::block_hash const & hash_a) override
	{
		auto value (block_raw_get (transaction_a, hash_a));
		debug_assert (value.size () != 0);
		auto type = block_type_from_raw (value.data ());
		std::vector<uint8_t> data (static_cast<uint8_t *> (value.data ()), static_cast<uint8_t *> (value.data ()) + value.size ());
		std::fill_n (data.begin () + block_successor_offset (transaction_a, value.size (), type), sizeof (scendere::block_hash), uint8_t{ 0 });
		raw_put (transaction_a, data, hash_a);
	}

	std::shared_ptr<scendere::block> get (scendere::transaction const & transaction_a, scendere::block_hash const & hash_a) const override
	{
		auto value (block_raw_get (transaction_a, hash_a));
		std::shared_ptr<scendere::block> result;
		if (value.size () != 0)
		{
			scendere::bufferstream stream (reinterpret_cast<uint8_t const *> (value.data ()), value.size ());
			scendere::block_type type;
			auto error (try_read (stream, type));
			release_assert (!error);
			result = scendere::deserialize_block (stream, type);
			release_assert (result != nullptr);
			scendere::block_sideband sideband;
			error = (sideband.deserialize (stream, type));
			release_assert (!error);
			result->sideband_set (sideband);
		}
		return result;
	}

	std::shared_ptr<scendere::block> get_no_sideband (scendere::transaction const & transaction_a, scendere::block_hash const & hash_a) const override
	{
		auto value (block_raw_get (transaction_a, hash_a));
		std::shared_ptr<scendere::block> result;
		if (value.size () != 0)
		{
			scendere::bufferstream stream (reinterpret_cast<uint8_t const *> (value.data ()), value.size ());
			result = scendere::deserialize_block (stream);
			debug_assert (result != nullptr);
		}
		return result;
	}

	std::shared_ptr<scendere::block> random (scendere::transaction const & transaction_a) override
	{
		scendere::block_hash hash;
		scendere::random_pool::generate_block (hash.bytes.data (), hash.bytes.size ());
		auto existing = store.template make_iterator<scendere::block_hash, std::shared_ptr<scendere::block>> (transaction_a, tables::blocks, scendere::db_val<Val> (hash));
		auto end (scendere::store_iterator<scendere::block_hash, std::shared_ptr<scendere::block>> (nullptr));
		if (existing == end)
		{
			existing = store.template make_iterator<scendere::block_hash, std::shared_ptr<scendere::block>> (transaction_a, tables::blocks);
		}
		debug_assert (existing != end);
		return existing->second;
	}

	void del (scendere::write_transaction const & transaction_a, scendere::block_hash const & hash_a) override
	{
		auto status = store.del (transaction_a, tables::blocks, hash_a);
		release_assert_success (store, status);
	}

	bool exists (scendere::transaction const & transaction_a, scendere::block_hash const & hash_a) override
	{
		auto junk = block_raw_get (transaction_a, hash_a);
		return junk.size () != 0;
	}

	uint64_t count (scendere::transaction const & transaction_a) override
	{
		return store.count (transaction_a, tables::blocks);
	}

	scendere::account account (scendere::transaction const & transaction_a, scendere::block_hash const & hash_a) const override
	{
		auto block (get (transaction_a, hash_a));
		debug_assert (block != nullptr);
		return account_calculated (*block);
	}

	scendere::account account_calculated (scendere::block const & block_a) const override
	{
		debug_assert (block_a.has_sideband ());
		scendere::account result (block_a.account ());
		if (result.is_zero ())
		{
			result = block_a.sideband ().account;
		}
		debug_assert (!result.is_zero ());
		return result;
	}

	scendere::store_iterator<scendere::block_hash, scendere::block_w_sideband> begin (scendere::transaction const & transaction_a) const override
	{
		return store.template make_iterator<scendere::block_hash, scendere::block_w_sideband> (transaction_a, tables::blocks);
	}

	scendere::store_iterator<scendere::block_hash, scendere::block_w_sideband> begin (scendere::transaction const & transaction_a, scendere::block_hash const & hash_a) const override
	{
		return store.template make_iterator<scendere::block_hash, scendere::block_w_sideband> (transaction_a, tables::blocks, scendere::db_val<Val> (hash_a));
	}

	scendere::store_iterator<scendere::block_hash, scendere::block_w_sideband> end () const override
	{
		return scendere::store_iterator<scendere::block_hash, scendere::block_w_sideband> (nullptr);
	}

	scendere::uint128_t balance (scendere::transaction const & transaction_a, scendere::block_hash const & hash_a) override
	{
		auto block (get (transaction_a, hash_a));
		release_assert (block);
		scendere::uint128_t result (balance_calculated (block));
		return result;
	}

	scendere::uint128_t balance_calculated (std::shared_ptr<scendere::block> const & block_a) const override
	{
		scendere::uint128_t result;
		switch (block_a->type ())
		{
			case scendere::block_type::open:
			case scendere::block_type::receive:
			case scendere::block_type::change:
				result = block_a->sideband ().balance.number ();
				break;
			case scendere::block_type::send:
				result = boost::polymorphic_downcast<scendere::send_block *> (block_a.get ())->hashables.balance.number ();
				break;
			case scendere::block_type::state:
				result = boost::polymorphic_downcast<scendere::state_block *> (block_a.get ())->hashables.balance.number ();
				break;
			case scendere::block_type::invalid:
			case scendere::block_type::not_a_block:
				release_assert (false);
				break;
		}
		return result;
	}

	scendere::epoch version (scendere::transaction const & transaction_a, scendere::block_hash const & hash_a) override
	{
		auto block = get (transaction_a, hash_a);
		if (block && block->type () == scendere::block_type::state)
		{
			return block->sideband ().details.epoch;
		}

		return scendere::epoch::epoch_0;
	}

	void for_each_par (std::function<void (scendere::read_transaction const &, scendere::store_iterator<scendere::block_hash, block_w_sideband>, scendere::store_iterator<scendere::block_hash, block_w_sideband>)> const & action_a) const override
	{
		parallel_traversal<scendere::uint256_t> (
		[&action_a, this] (scendere::uint256_t const & start, scendere::uint256_t const & end, bool const is_last) {
			auto transaction (this->store.tx_begin_read ());
			action_a (transaction, this->begin (transaction, start), !is_last ? this->begin (transaction, end) : this->end ());
		});
	}

	// Converts a block hash to a block height
	uint64_t account_height (scendere::transaction const & transaction_a, scendere::block_hash const & hash_a) const override
	{
		auto block = get (transaction_a, hash_a);
		return block->sideband ().height;
	}

protected:
	scendere::db_val<Val> block_raw_get (scendere::transaction const & transaction_a, scendere::block_hash const & hash_a) const
	{
		scendere::db_val<Val> result;
		auto status = store.get (transaction_a, tables::blocks, hash_a, result);
		release_assert (store.success (status) || store.not_found (status));
		return result;
	}

	size_t block_successor_offset (scendere::transaction const & transaction_a, size_t entry_size_a, scendere::block_type type_a) const
	{
		return entry_size_a - scendere::block_sideband::size (type_a);
	}

	static scendere::block_type block_type_from_raw (void * data_a)
	{
		// The block type is the first byte
		return static_cast<scendere::block_type> ((reinterpret_cast<uint8_t const *> (data_a))[0]);
	}
};

/**
 * Fill in our predecessors
 */
template <typename Val, typename Derived_Store>
class block_predecessor_set : public scendere::block_visitor
{
public:
	block_predecessor_set (scendere::write_transaction const & transaction_a, scendere::block_store_partial<Val, Derived_Store> & block_store_a) :
		transaction (transaction_a),
		block_store (block_store_a)
	{
	}
	virtual ~block_predecessor_set () = default;
	void fill_value (scendere::block const & block_a)
	{
		auto hash (block_a.hash ());
		auto value (block_store.block_raw_get (transaction, block_a.previous ()));
		debug_assert (value.size () != 0);
		auto type = block_store.block_type_from_raw (value.data ());
		std::vector<uint8_t> data (static_cast<uint8_t *> (value.data ()), static_cast<uint8_t *> (value.data ()) + value.size ());
		std::copy (hash.bytes.begin (), hash.bytes.end (), data.begin () + block_store.block_successor_offset (transaction, value.size (), type));
		block_store.raw_put (transaction, data, block_a.previous ());
	}
	void send_block (scendere::send_block const & block_a) override
	{
		fill_value (block_a);
	}
	void receive_block (scendere::receive_block const & block_a) override
	{
		fill_value (block_a);
	}
	void open_block (scendere::open_block const & block_a) override
	{
		// Open blocks don't have a predecessor
	}
	void change_block (scendere::change_block const & block_a) override
	{
		fill_value (block_a);
	}
	void state_block (scendere::state_block const & block_a) override
	{
		if (!block_a.previous ().is_zero ())
		{
			fill_value (block_a);
		}
	}
	scendere::write_transaction const & transaction;
	scendere::block_store_partial<Val, Derived_Store> & block_store;
};
}
