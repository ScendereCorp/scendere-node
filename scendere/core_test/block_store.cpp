#include <scendere/crypto_lib/random_pool.hpp>
#include <scendere/lib/lmdbconfig.hpp>
#include <scendere/lib/logger_mt.hpp>
#include <scendere/lib/stats.hpp>
#include <scendere/lib/utility.hpp>
#include <scendere/lib/work.hpp>
#include <scendere/node/common.hpp>
#include <scendere/node/lmdb/lmdb.hpp>
#include <scendere/node/rocksdb/rocksdb.hpp>
#include <scendere/secure/ledger.hpp>
#include <scendere/secure/utility.hpp>
#include <scendere/secure/versioning.hpp>
#include <scendere/test_common/system.hpp>
#include <scendere/test_common/testutil.hpp>

#include <gtest/gtest.h>

#include <boost/filesystem.hpp>

#include <fstream>
#include <unordered_set>

#include <stdlib.h>

using namespace std::chrono_literals;

namespace
{
void modify_account_info_to_v14 (scendere::mdb_store & store, scendere::transaction const & transaction_a, scendere::account const & account_a, uint64_t confirmation_height, scendere::block_hash const & rep_block);
void modify_confirmation_height_to_v15 (scendere::mdb_store & store, scendere::transaction const & transaction, scendere::account const & account, uint64_t confirmation_height);
void write_sideband_v14 (scendere::mdb_store & store_a, scendere::transaction & transaction_a, scendere::block const & block_a, MDB_dbi db_a);
void write_sideband_v15 (scendere::mdb_store & store_a, scendere::transaction & transaction_a, scendere::block const & block_a);
void write_block_w_sideband_v18 (scendere::mdb_store & store_a, MDB_dbi database, scendere::write_transaction & transaction_a, scendere::block const & block_a);
}

TEST (block_store, construction)
{
	scendere::logger_mt logger;
	auto store = scendere::make_store (logger, scendere::unique_path (), scendere::dev::constants);
	ASSERT_TRUE (!store->init_error ());
}

TEST (block_store, block_details)
{
	scendere::block_details details_send (scendere::epoch::epoch_0, true, false, false);
	ASSERT_TRUE (details_send.is_send);
	ASSERT_FALSE (details_send.is_receive);
	ASSERT_FALSE (details_send.is_epoch);
	ASSERT_EQ (scendere::epoch::epoch_0, details_send.epoch);

	scendere::block_details details_receive (scendere::epoch::epoch_1, false, true, false);
	ASSERT_FALSE (details_receive.is_send);
	ASSERT_TRUE (details_receive.is_receive);
	ASSERT_FALSE (details_receive.is_epoch);
	ASSERT_EQ (scendere::epoch::epoch_1, details_receive.epoch);

	scendere::block_details details_epoch (scendere::epoch::epoch_2, false, false, true);
	ASSERT_FALSE (details_epoch.is_send);
	ASSERT_FALSE (details_epoch.is_receive);
	ASSERT_TRUE (details_epoch.is_epoch);
	ASSERT_EQ (scendere::epoch::epoch_2, details_epoch.epoch);

	scendere::block_details details_none (scendere::epoch::unspecified, false, false, false);
	ASSERT_FALSE (details_none.is_send);
	ASSERT_FALSE (details_none.is_receive);
	ASSERT_FALSE (details_none.is_epoch);
	ASSERT_EQ (scendere::epoch::unspecified, details_none.epoch);
}

TEST (block_store, block_details_serialization)
{
	scendere::block_details details1;
	details1.epoch = scendere::epoch::epoch_2;
	details1.is_epoch = false;
	details1.is_receive = true;
	details1.is_send = false;
	std::vector<uint8_t> vector;
	{
		scendere::vectorstream stream1 (vector);
		details1.serialize (stream1);
	}
	scendere::bufferstream stream2 (vector.data (), vector.size ());
	scendere::block_details details2;
	ASSERT_FALSE (details2.deserialize (stream2));
	ASSERT_EQ (details1, details2);
}

TEST (block_store, sideband_serialization)
{
	scendere::block_sideband sideband1;
	sideband1.account = 1;
	sideband1.balance = 2;
	sideband1.height = 3;
	sideband1.successor = 4;
	sideband1.timestamp = 5;
	std::vector<uint8_t> vector;
	{
		scendere::vectorstream stream1 (vector);
		sideband1.serialize (stream1, scendere::block_type::receive);
	}
	scendere::bufferstream stream2 (vector.data (), vector.size ());
	scendere::block_sideband sideband2;
	ASSERT_FALSE (sideband2.deserialize (stream2, scendere::block_type::receive));
	ASSERT_EQ (sideband1.account, sideband2.account);
	ASSERT_EQ (sideband1.balance, sideband2.balance);
	ASSERT_EQ (sideband1.height, sideband2.height);
	ASSERT_EQ (sideband1.successor, sideband2.successor);
	ASSERT_EQ (sideband1.timestamp, sideband2.timestamp);
}

TEST (block_store, add_item)
{
	scendere::logger_mt logger;
	auto store = scendere::make_store (logger, scendere::unique_path (), scendere::dev::constants);
	ASSERT_TRUE (!store->init_error ());
	scendere::open_block block (0, 1, 0, scendere::keypair ().prv, 0, 0);
	block.sideband_set ({});
	auto hash1 (block.hash ());
	auto transaction (store->tx_begin_write ());
	auto latest1 (store->block.get (transaction, hash1));
	ASSERT_EQ (nullptr, latest1);
	ASSERT_FALSE (store->block.exists (transaction, hash1));
	store->block.put (transaction, hash1, block);
	auto latest2 (store->block.get (transaction, hash1));
	ASSERT_NE (nullptr, latest2);
	ASSERT_EQ (block, *latest2);
	ASSERT_TRUE (store->block.exists (transaction, hash1));
	ASSERT_FALSE (store->block.exists (transaction, hash1.number () - 1));
	store->block.del (transaction, hash1);
	auto latest3 (store->block.get (transaction, hash1));
	ASSERT_EQ (nullptr, latest3);
}

TEST (block_store, clear_successor)
{
	scendere::logger_mt logger;
	auto store = scendere::make_store (logger, scendere::unique_path (), scendere::dev::constants);
	ASSERT_TRUE (!store->init_error ());
	scendere::open_block block1 (0, 1, 0, scendere::keypair ().prv, 0, 0);
	block1.sideband_set ({});
	auto transaction (store->tx_begin_write ());
	store->block.put (transaction, block1.hash (), block1);
	scendere::open_block block2 (0, 2, 0, scendere::keypair ().prv, 0, 0);
	block2.sideband_set ({});
	store->block.put (transaction, block2.hash (), block2);
	auto block2_store (store->block.get (transaction, block1.hash ()));
	ASSERT_NE (nullptr, block2_store);
	ASSERT_EQ (0, block2_store->sideband ().successor.number ());
	auto modified_sideband = block2_store->sideband ();
	modified_sideband.successor = block2.hash ();
	block1.sideband_set (modified_sideband);
	store->block.put (transaction, block1.hash (), block1);
	{
		auto block1_store (store->block.get (transaction, block1.hash ()));
		ASSERT_NE (nullptr, block1_store);
		ASSERT_EQ (block2.hash (), block1_store->sideband ().successor);
	}
	store->block.successor_clear (transaction, block1.hash ());
	{
		auto block1_store (store->block.get (transaction, block1.hash ()));
		ASSERT_NE (nullptr, block1_store);
		ASSERT_EQ (0, block1_store->sideband ().successor.number ());
	}
}

TEST (block_store, add_nonempty_block)
{
	scendere::logger_mt logger;
	auto store = scendere::make_store (logger, scendere::unique_path (), scendere::dev::constants);
	ASSERT_TRUE (!store->init_error ());
	scendere::keypair key1;
	scendere::open_block block (0, 1, 0, scendere::keypair ().prv, 0, 0);
	block.sideband_set ({});
	auto hash1 (block.hash ());
	block.signature = scendere::sign_message (key1.prv, key1.pub, hash1);
	auto transaction (store->tx_begin_write ());
	auto latest1 (store->block.get (transaction, hash1));
	ASSERT_EQ (nullptr, latest1);
	store->block.put (transaction, hash1, block);
	auto latest2 (store->block.get (transaction, hash1));
	ASSERT_NE (nullptr, latest2);
	ASSERT_EQ (block, *latest2);
}

TEST (block_store, add_two_items)
{
	scendere::logger_mt logger;
	auto store = scendere::make_store (logger, scendere::unique_path (), scendere::dev::constants);
	ASSERT_TRUE (!store->init_error ());
	scendere::keypair key1;
	scendere::open_block block (0, 1, 1, scendere::keypair ().prv, 0, 0);
	block.sideband_set ({});
	auto hash1 (block.hash ());
	block.signature = scendere::sign_message (key1.prv, key1.pub, hash1);
	auto transaction (store->tx_begin_write ());
	auto latest1 (store->block.get (transaction, hash1));
	ASSERT_EQ (nullptr, latest1);
	scendere::open_block block2 (0, 1, 3, scendere::keypair ().prv, 0, 0);
	block2.sideband_set ({});
	block2.hashables.account = 3;
	auto hash2 (block2.hash ());
	block2.signature = scendere::sign_message (key1.prv, key1.pub, hash2);
	auto latest2 (store->block.get (transaction, hash2));
	ASSERT_EQ (nullptr, latest2);
	store->block.put (transaction, hash1, block);
	store->block.put (transaction, hash2, block2);
	auto latest3 (store->block.get (transaction, hash1));
	ASSERT_NE (nullptr, latest3);
	ASSERT_EQ (block, *latest3);
	auto latest4 (store->block.get (transaction, hash2));
	ASSERT_NE (nullptr, latest4);
	ASSERT_EQ (block2, *latest4);
	ASSERT_FALSE (*latest3 == *latest4);
}

TEST (block_store, add_receive)
{
	scendere::logger_mt logger;
	auto store = scendere::make_store (logger, scendere::unique_path (), scendere::dev::constants);
	ASSERT_TRUE (!store->init_error ());
	scendere::keypair key1;
	scendere::keypair key2;
	scendere::open_block block1 (0, 1, 0, scendere::keypair ().prv, 0, 0);
	block1.sideband_set ({});
	auto transaction (store->tx_begin_write ());
	store->block.put (transaction, block1.hash (), block1);
	scendere::receive_block block (block1.hash (), 1, scendere::keypair ().prv, 2, 3);
	block.sideband_set ({});
	scendere::block_hash hash1 (block.hash ());
	auto latest1 (store->block.get (transaction, hash1));
	ASSERT_EQ (nullptr, latest1);
	store->block.put (transaction, hash1, block);
	auto latest2 (store->block.get (transaction, hash1));
	ASSERT_NE (nullptr, latest2);
	ASSERT_EQ (block, *latest2);
}

TEST (block_store, add_pending)
{
	scendere::logger_mt logger;
	auto store = scendere::make_store (logger, scendere::unique_path (), scendere::dev::constants);
	ASSERT_TRUE (!store->init_error ());
	scendere::keypair key1;
	scendere::pending_key key2 (0, 0);
	scendere::pending_info pending1;
	auto transaction (store->tx_begin_write ());
	ASSERT_TRUE (store->pending.get (transaction, key2, pending1));
	store->pending.put (transaction, key2, pending1);
	scendere::pending_info pending2;
	ASSERT_FALSE (store->pending.get (transaction, key2, pending2));
	ASSERT_EQ (pending1, pending2);
	store->pending.del (transaction, key2);
	ASSERT_TRUE (store->pending.get (transaction, key2, pending2));
}

TEST (block_store, pending_iterator)
{
	scendere::logger_mt logger;
	auto store = scendere::make_store (logger, scendere::unique_path (), scendere::dev::constants);
	ASSERT_TRUE (!store->init_error ());
	auto transaction (store->tx_begin_write ());
	ASSERT_EQ (store->pending.end (), store->pending.begin (transaction));
	store->pending.put (transaction, scendere::pending_key (1, 2), { 2, 3, scendere::epoch::epoch_1 });
	auto current (store->pending.begin (transaction));
	ASSERT_NE (store->pending.end (), current);
	scendere::pending_key key1 (current->first);
	ASSERT_EQ (scendere::account (1), key1.account);
	ASSERT_EQ (scendere::block_hash (2), key1.hash);
	scendere::pending_info pending (current->second);
	ASSERT_EQ (scendere::account (2), pending.source);
	ASSERT_EQ (scendere::amount (3), pending.amount);
	ASSERT_EQ (scendere::epoch::epoch_1, pending.epoch);
}

/**
 * Regression test for Issue 1164
 * This reconstructs the situation where a key is larger in pending than the account being iterated in pending_v1, leaving
 * iteration order up to the value, causing undefined behavior.
 * After the bugfix, the value is compared only if the keys are equal.
 */
TEST (block_store, pending_iterator_comparison)
{
	scendere::logger_mt logger;
	auto store = scendere::make_store (logger, scendere::unique_path (), scendere::dev::constants);
	ASSERT_TRUE (!store->init_error ());
	scendere::stat stats;
	auto transaction (store->tx_begin_write ());
	// Populate pending
	store->pending.put (transaction, scendere::pending_key (scendere::account (3), scendere::block_hash (1)), scendere::pending_info (scendere::account (10), scendere::amount (1), scendere::epoch::epoch_0));
	store->pending.put (transaction, scendere::pending_key (scendere::account (3), scendere::block_hash (4)), scendere::pending_info (scendere::account (10), scendere::amount (0), scendere::epoch::epoch_0));
	// Populate pending_v1
	store->pending.put (transaction, scendere::pending_key (scendere::account (2), scendere::block_hash (2)), scendere::pending_info (scendere::account (10), scendere::amount (2), scendere::epoch::epoch_1));
	store->pending.put (transaction, scendere::pending_key (scendere::account (2), scendere::block_hash (3)), scendere::pending_info (scendere::account (10), scendere::amount (3), scendere::epoch::epoch_1));

	// Iterate account 3 (pending)
	{
		size_t count = 0;
		scendere::account begin (3);
		scendere::account end (begin.number () + 1);
		for (auto i (store->pending.begin (transaction, scendere::pending_key (begin, 0))), n (store->pending.begin (transaction, scendere::pending_key (end, 0))); i != n; ++i, ++count)
		{
			scendere::pending_key key (i->first);
			ASSERT_EQ (key.account, begin);
			ASSERT_LT (count, 3);
		}
		ASSERT_EQ (count, 2);
	}

	// Iterate account 2 (pending_v1)
	{
		size_t count = 0;
		scendere::account begin (2);
		scendere::account end (begin.number () + 1);
		for (auto i (store->pending.begin (transaction, scendere::pending_key (begin, 0))), n (store->pending.begin (transaction, scendere::pending_key (end, 0))); i != n; ++i, ++count)
		{
			scendere::pending_key key (i->first);
			ASSERT_EQ (key.account, begin);
			ASSERT_LT (count, 3);
		}
		ASSERT_EQ (count, 2);
	}
}

TEST (block_store, genesis)
{
	scendere::logger_mt logger;
	auto store = scendere::make_store (logger, scendere::unique_path (), scendere::dev::constants);
	ASSERT_TRUE (!store->init_error ());
	scendere::ledger_cache ledger_cache;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, ledger_cache);
	scendere::account_info info;
	ASSERT_FALSE (store->account.get (transaction, scendere::dev::genesis->account (), info));
	ASSERT_EQ (scendere::dev::genesis->hash (), info.head);
	auto block1 (store->block.get (transaction, info.head));
	ASSERT_NE (nullptr, block1);
	auto receive1 (dynamic_cast<scendere::open_block *> (block1.get ()));
	ASSERT_NE (nullptr, receive1);
	ASSERT_LE (info.modified, scendere::seconds_since_epoch ());
	ASSERT_EQ (info.block_count, 1);
	// Genesis block should be confirmed by default
	scendere::confirmation_height_info confirmation_height_info;
	ASSERT_FALSE (store->confirmation_height.get (transaction, scendere::dev::genesis->account (), confirmation_height_info));
	ASSERT_EQ (confirmation_height_info.height, 1);
	ASSERT_EQ (confirmation_height_info.frontier, scendere::dev::genesis->hash ());
	auto dev_pub_text (scendere::dev::genesis_key.pub.to_string ());
	auto dev_pub_account (scendere::dev::genesis_key.pub.to_account ());
	auto dev_prv_text (scendere::dev::genesis_key.prv.to_string ());
	ASSERT_EQ (scendere::dev::genesis->account (), scendere::dev::genesis_key.pub);
}

// This test checks for basic operations in the unchecked table such as putting a new block, retrieving it, and
// deleting it from the database
TEST (unchecked, simple)
{
	scendere::system system{};
	scendere::logger_mt logger{};
	auto store = scendere::make_store (logger, scendere::unique_path (), scendere::dev::constants);
	scendere::unchecked_map unchecked{ *store, false };
	ASSERT_TRUE (!store->init_error ());
	std::shared_ptr<scendere::block> block = std::make_shared<scendere::send_block> (0, 1, 2, scendere::keypair ().prv, 4, 5);
	// Asserts the block wasn't added yet to the unchecked table
	auto block_listing1 = unchecked.get (store->tx_begin_read (), block->previous ());
	ASSERT_TRUE (block_listing1.empty ());
	// Enqueues a block to be saved on the unchecked table
	unchecked.put (block->previous (), block);
	// Waits for the block to get written in the database
	auto check_block_is_listed = [&] (scendere::transaction const & transaction_a, scendere::block_hash const & block_hash_a) {
		return unchecked.get (transaction_a, block_hash_a).size () > 0;
	};
	ASSERT_TIMELY (5s, check_block_is_listed (store->tx_begin_read (), block->previous ()));
	auto transaction = store->tx_begin_write ();
	// Retrieves the block from the database
	auto block_listing2 = unchecked.get (transaction, block->previous ());
	ASSERT_FALSE (block_listing2.empty ());
	// Asserts the added block is equal to the retrieved one
	ASSERT_EQ (*block, *(block_listing2[0].block));
	// Deletes the block from the database
	unchecked.del (transaction, scendere::unchecked_key (block->previous (), block->hash ()));
	// Asserts the block is deleted
	auto block_listing3 = unchecked.get (transaction, block->previous ());
	ASSERT_TRUE (block_listing3.empty ());
}

// This test ensures the unchecked table is able to receive more than one block
TEST (unchecked, multiple)
{
	scendere::system system{};
	if (scendere::rocksdb_config::using_rocksdb_in_tests ())
	{
		// Don't test this in rocksdb mode
		return;
	}
	scendere::logger_mt logger{};
	auto store = scendere::make_store (logger, scendere::unique_path (), scendere::dev::constants);
	scendere::unchecked_map unchecked{ *store, false };
	ASSERT_TRUE (!store->init_error ());
	std::shared_ptr<scendere::block> block = std::make_shared<scendere::send_block> (4, 1, 2, scendere::keypair ().prv, 4, 5);
	// Asserts the block wasn't added yet to the unchecked table
	auto block_listing1 = unchecked.get (store->tx_begin_read (), block->previous ());
	ASSERT_TRUE (block_listing1.empty ());
	// Enqueues the first block
	unchecked.put (block->previous (), block);
	// Enqueues a second block
	unchecked.put (block->source (), block);
	auto check_block_is_listed = [&] (scendere::transaction const & transaction_a, scendere::block_hash const & block_hash_a) {
		return unchecked.get (transaction_a, block_hash_a).size () > 0;
	};
	// Waits for and asserts the first block gets saved in the database
	ASSERT_TIMELY (5s, check_block_is_listed (store->tx_begin_read (), block->previous ()));
	// Waits for and asserts the second block gets saved in the database
	ASSERT_TIMELY (5s, check_block_is_listed (store->tx_begin_read (), block->source ()));
}

// This test ensures that a block can't occur twice in the unchecked table.
TEST (unchecked, double_put)
{
	scendere::system system{};
	scendere::logger_mt logger{};
	auto store = scendere::make_store (logger, scendere::unique_path (), scendere::dev::constants);
	scendere::unchecked_map unchecked{ *store, false };
	ASSERT_TRUE (!store->init_error ());
	std::shared_ptr<scendere::block> block = std::make_shared<scendere::send_block> (4, 1, 2, scendere::keypair ().prv, 4, 5);
	// Asserts the block wasn't added yet to the unchecked table
	auto block_listing1 = unchecked.get (store->tx_begin_read (), block->previous ());
	ASSERT_TRUE (block_listing1.empty ());
	// Enqueues the block to be saved in the unchecked table
	unchecked.put (block->previous (), block);
	// Enqueues the block again in an attempt to have it there twice
	unchecked.put (block->previous (), block);
	auto check_block_is_listed = [&] (scendere::transaction const & transaction_a, scendere::block_hash const & block_hash_a) {
		return unchecked.get (transaction_a, block_hash_a).size () > 0;
	};
	// Waits for and asserts the block was added at least once
	ASSERT_TIMELY (5s, check_block_is_listed (store->tx_begin_read (), block->previous ()));
	// Asserts the block was added at most once -- this is objective of this test.
	auto block_listing2 = unchecked.get (store->tx_begin_read (), block->previous ());
	ASSERT_EQ (block_listing2.size (), 1);
}

// Tests that recurrent get calls return the correct values
TEST (unchecked, multiple_get)
{
	scendere::system system{};
	scendere::logger_mt logger{};
	auto store = scendere::make_store (logger, scendere::unique_path (), scendere::dev::constants);
	scendere::unchecked_map unchecked{ *store, false };
	ASSERT_TRUE (!store->init_error ());
	// Instantiates three blocks
	std::shared_ptr<scendere::block> block1 = std::make_shared<scendere::send_block> (4, 1, 2, scendere::keypair ().prv, 4, 5);
	std::shared_ptr<scendere::block> block2 = std::make_shared<scendere::send_block> (3, 1, 2, scendere::keypair ().prv, 4, 5);
	std::shared_ptr<scendere::block> block3 = std::make_shared<scendere::send_block> (5, 1, 2, scendere::keypair ().prv, 4, 5);
	// Add the blocks' info to the unchecked table
	unchecked.put (block1->previous (), block1); // unchecked1
	unchecked.put (block1->hash (), block1); // unchecked2
	unchecked.put (block2->previous (), block2); // unchecked3
	unchecked.put (block1->previous (), block2); // unchecked1
	unchecked.put (block1->hash (), block2); // unchecked2
	unchecked.put (block3->previous (), block3);
	unchecked.put (block3->hash (), block3); // unchecked4
	unchecked.put (block1->previous (), block3); // unchecked1

	// count the number of blocks in the unchecked table by counting them one by one
	// we cannot trust the count() method if the backend is rocksdb
	auto count_unchecked_blocks_one_by_one = [&store, &unchecked] () {
		size_t count = 0;
		auto transaction = store->tx_begin_read ();
		for (auto [i, end] = unchecked.full_range (transaction); i != end; ++i)
		{
			++count;
		}
		return count;
	};

	// Waits for the blocks to get saved in the database
	ASSERT_TIMELY (5s, 8 == count_unchecked_blocks_one_by_one ());

	std::vector<scendere::block_hash> unchecked1;
	// Asserts the entries will be found for the provided key
	auto transaction = store->tx_begin_read ();
	auto unchecked1_blocks = unchecked.get (transaction, block1->previous ());
	ASSERT_EQ (unchecked1_blocks.size (), 3);
	for (auto & i : unchecked1_blocks)
	{
		unchecked1.push_back (i.block->hash ());
	}
	// Asserts the payloads where correclty saved
	ASSERT_TRUE (std::find (unchecked1.begin (), unchecked1.end (), block1->hash ()) != unchecked1.end ());
	ASSERT_TRUE (std::find (unchecked1.begin (), unchecked1.end (), block2->hash ()) != unchecked1.end ());
	ASSERT_TRUE (std::find (unchecked1.begin (), unchecked1.end (), block3->hash ()) != unchecked1.end ());
	std::vector<scendere::block_hash> unchecked2;
	// Asserts the entries will be found for the provided key
	auto unchecked2_blocks = unchecked.get (transaction, block1->hash ());
	ASSERT_EQ (unchecked2_blocks.size (), 2);
	for (auto & i : unchecked2_blocks)
	{
		unchecked2.push_back (i.block->hash ());
	}
	// Asserts the payloads where correctly saved
	ASSERT_TRUE (std::find (unchecked2.begin (), unchecked2.end (), block1->hash ()) != unchecked2.end ());
	ASSERT_TRUE (std::find (unchecked2.begin (), unchecked2.end (), block2->hash ()) != unchecked2.end ());
	// Asserts the entry is found by the key and the payload is saved
	auto unchecked3 = unchecked.get (transaction, block2->previous ());
	ASSERT_EQ (unchecked3.size (), 1);
	ASSERT_EQ (unchecked3[0].block->hash (), block2->hash ());
	// Asserts the entry is found by the key and the payload is saved
	auto unchecked4 = unchecked.get (transaction, block3->hash ());
	ASSERT_EQ (unchecked4.size (), 1);
	ASSERT_EQ (unchecked4[0].block->hash (), block3->hash ());
	// Asserts no entry is found for a block that wasn't added
	auto unchecked5 = unchecked.get (transaction, block2->hash ());
	ASSERT_EQ (unchecked5.size (), 0);
}

TEST (block_store, empty_accounts)
{
	scendere::logger_mt logger;
	auto store = scendere::make_store (logger, scendere::unique_path (), scendere::dev::constants);
	ASSERT_TRUE (!store->init_error ());
	auto transaction (store->tx_begin_read ());
	auto begin (store->account.begin (transaction));
	auto end (store->account.end ());
	ASSERT_EQ (end, begin);
}

TEST (block_store, one_block)
{
	scendere::logger_mt logger;
	auto store = scendere::make_store (logger, scendere::unique_path (), scendere::dev::constants);
	ASSERT_TRUE (!store->init_error ());
	scendere::open_block block1 (0, 1, 0, scendere::keypair ().prv, 0, 0);
	block1.sideband_set ({});
	auto transaction (store->tx_begin_write ());
	store->block.put (transaction, block1.hash (), block1);
	ASSERT_TRUE (store->block.exists (transaction, block1.hash ()));
}

TEST (block_store, empty_bootstrap)
{
	scendere::logger_mt logger;
	auto store = scendere::make_store (logger, scendere::unique_path (), scendere::dev::constants);
	scendere::unchecked_map unchecked{ *store, false };
	ASSERT_TRUE (!store->init_error ());
	auto transaction (store->tx_begin_read ());
	auto [begin, end] = unchecked.full_range (transaction);
	ASSERT_EQ (end, begin);
}

TEST (block_store, unchecked_begin_search)
{
	scendere::logger_mt logger;
	auto store = scendere::make_store (logger, scendere::unique_path (), scendere::dev::constants);
	ASSERT_TRUE (!store->init_error ());
	scendere::keypair key0;
	scendere::send_block block1 (0, 1, 2, key0.prv, key0.pub, 3);
	scendere::send_block block2 (5, 6, 7, key0.prv, key0.pub, 8);
}

TEST (block_store, frontier_retrieval)
{
	scendere::logger_mt logger;
	auto store = scendere::make_store (logger, scendere::unique_path (), scendere::dev::constants);
	ASSERT_TRUE (!store->init_error ());
	scendere::account account1{};
	scendere::account_info info1 (0, 0, 0, 0, 0, 0, scendere::epoch::epoch_0);
	auto transaction (store->tx_begin_write ());
	store->confirmation_height.put (transaction, account1, { 0, scendere::block_hash (0) });
	store->account.put (transaction, account1, info1);
	scendere::account_info info2;
	store->account.get (transaction, account1, info2);
	ASSERT_EQ (info1, info2);
}

TEST (block_store, one_account)
{
	scendere::logger_mt logger;
	auto store = scendere::make_store (logger, scendere::unique_path (), scendere::dev::constants);
	ASSERT_TRUE (!store->init_error ());
	scendere::account account{};
	scendere::block_hash hash (0);
	auto transaction (store->tx_begin_write ());
	store->confirmation_height.put (transaction, account, { 20, scendere::block_hash (15) });
	store->account.put (transaction, account, { hash, account, hash, 42, 100, 200, scendere::epoch::epoch_0 });
	auto begin (store->account.begin (transaction));
	auto end (store->account.end ());
	ASSERT_NE (end, begin);
	ASSERT_EQ (account, scendere::account (begin->first));
	scendere::account_info info (begin->second);
	ASSERT_EQ (hash, info.head);
	ASSERT_EQ (42, info.balance.number ());
	ASSERT_EQ (100, info.modified);
	ASSERT_EQ (200, info.block_count);
	scendere::confirmation_height_info confirmation_height_info;
	ASSERT_FALSE (store->confirmation_height.get (transaction, account, confirmation_height_info));
	ASSERT_EQ (20, confirmation_height_info.height);
	ASSERT_EQ (scendere::block_hash (15), confirmation_height_info.frontier);
	++begin;
	ASSERT_EQ (end, begin);
}

TEST (block_store, two_block)
{
	scendere::logger_mt logger;
	auto store = scendere::make_store (logger, scendere::unique_path (), scendere::dev::constants);
	ASSERT_TRUE (!store->init_error ());
	scendere::open_block block1 (0, 1, 1, scendere::keypair ().prv, 0, 0);
	block1.sideband_set ({});
	block1.hashables.account = 1;
	std::vector<scendere::block_hash> hashes;
	std::vector<scendere::open_block> blocks;
	hashes.push_back (block1.hash ());
	blocks.push_back (block1);
	auto transaction (store->tx_begin_write ());
	store->block.put (transaction, hashes[0], block1);
	scendere::open_block block2 (0, 1, 2, scendere::keypair ().prv, 0, 0);
	block2.sideband_set ({});
	hashes.push_back (block2.hash ());
	blocks.push_back (block2);
	store->block.put (transaction, hashes[1], block2);
	ASSERT_TRUE (store->block.exists (transaction, block1.hash ()));
	ASSERT_TRUE (store->block.exists (transaction, block2.hash ()));
}

TEST (block_store, two_account)
{
	scendere::logger_mt logger;
	auto store = scendere::make_store (logger, scendere::unique_path (), scendere::dev::constants);
	ASSERT_TRUE (!store->init_error ());
	scendere::account account1 (1);
	scendere::block_hash hash1 (2);
	scendere::account account2 (3);
	scendere::block_hash hash2 (4);
	auto transaction (store->tx_begin_write ());
	store->confirmation_height.put (transaction, account1, { 20, scendere::block_hash (10) });
	store->account.put (transaction, account1, { hash1, account1, hash1, 42, 100, 300, scendere::epoch::epoch_0 });
	store->confirmation_height.put (transaction, account2, { 30, scendere::block_hash (20) });
	store->account.put (transaction, account2, { hash2, account2, hash2, 84, 200, 400, scendere::epoch::epoch_0 });
	auto begin (store->account.begin (transaction));
	auto end (store->account.end ());
	ASSERT_NE (end, begin);
	ASSERT_EQ (account1, scendere::account (begin->first));
	scendere::account_info info1 (begin->second);
	ASSERT_EQ (hash1, info1.head);
	ASSERT_EQ (42, info1.balance.number ());
	ASSERT_EQ (100, info1.modified);
	ASSERT_EQ (300, info1.block_count);
	scendere::confirmation_height_info confirmation_height_info;
	ASSERT_FALSE (store->confirmation_height.get (transaction, account1, confirmation_height_info));
	ASSERT_EQ (20, confirmation_height_info.height);
	ASSERT_EQ (scendere::block_hash (10), confirmation_height_info.frontier);
	++begin;
	ASSERT_NE (end, begin);
	ASSERT_EQ (account2, scendere::account (begin->first));
	scendere::account_info info2 (begin->second);
	ASSERT_EQ (hash2, info2.head);
	ASSERT_EQ (84, info2.balance.number ());
	ASSERT_EQ (200, info2.modified);
	ASSERT_EQ (400, info2.block_count);
	ASSERT_FALSE (store->confirmation_height.get (transaction, account2, confirmation_height_info));
	ASSERT_EQ (30, confirmation_height_info.height);
	ASSERT_EQ (scendere::block_hash (20), confirmation_height_info.frontier);
	++begin;
	ASSERT_EQ (end, begin);
}

TEST (block_store, latest_find)
{
	scendere::logger_mt logger;
	auto store = scendere::make_store (logger, scendere::unique_path (), scendere::dev::constants);
	ASSERT_TRUE (!store->init_error ());
	scendere::account account1 (1);
	scendere::block_hash hash1 (2);
	scendere::account account2 (3);
	scendere::block_hash hash2 (4);
	auto transaction (store->tx_begin_write ());
	store->confirmation_height.put (transaction, account1, { 0, scendere::block_hash (0) });
	store->account.put (transaction, account1, { hash1, account1, hash1, 100, 0, 300, scendere::epoch::epoch_0 });
	store->confirmation_height.put (transaction, account2, { 0, scendere::block_hash (0) });
	store->account.put (transaction, account2, { hash2, account2, hash2, 200, 0, 400, scendere::epoch::epoch_0 });
	auto first (store->account.begin (transaction));
	auto second (store->account.begin (transaction));
	++second;
	auto find1 (store->account.begin (transaction, 1));
	ASSERT_EQ (first, find1);
	auto find2 (store->account.begin (transaction, 3));
	ASSERT_EQ (second, find2);
	auto find3 (store->account.begin (transaction, 2));
	ASSERT_EQ (second, find3);
}

TEST (mdb_block_store, supported_version_upgrades)
{
	if (scendere::rocksdb_config::using_rocksdb_in_tests ())
	{
		// Don't test this in rocksdb mode
		return;
	}
	// Check that upgrading from an unsupported version is not supported
	auto path (scendere::unique_path ());
	scendere::logger_mt logger;
	{
		scendere::mdb_store store (logger, path, scendere::dev::constants);
		scendere::stat stats;
		scendere::ledger ledger (store, stats, scendere::dev::constants);
		auto transaction (store.tx_begin_write ());
		store.initialize (transaction, ledger.cache);
		// Lower the database to the max version unsupported for upgrades
		store.version.put (transaction, store.minimum_version - 1);
	}

	// Upgrade should fail
	{
		scendere::mdb_store store (logger, path, scendere::dev::constants);
		ASSERT_TRUE (store.init_error ());
	}

	auto path1 (scendere::unique_path ());
	// Now try with the minimum version
	{
		scendere::mdb_store store (logger, path1, scendere::dev::constants);
		scendere::stat stats;
		scendere::ledger ledger (store, stats, scendere::dev::constants);
		auto transaction (store.tx_begin_write ());
		store.initialize (transaction, ledger.cache);
		// Lower the database version to the minimum version supported for upgrade.
		store.version.put (transaction, store.minimum_version);
		store.confirmation_height.del (transaction, scendere::dev::genesis->account ());
		ASSERT_FALSE (mdb_dbi_open (store.env.tx (transaction), "accounts_v1", MDB_CREATE, &store.accounts_v1_handle));
		ASSERT_FALSE (mdb_dbi_open (store.env.tx (transaction), "open", MDB_CREATE, &store.open_blocks_handle));
		modify_account_info_to_v14 (store, transaction, scendere::dev::genesis->account (), 1, scendere::dev::genesis->hash ());
		write_block_w_sideband_v18 (store, store.open_blocks_handle, transaction, *scendere::dev::genesis);
	}

	// Upgrade should work
	{
		scendere::mdb_store store (logger, path1, scendere::dev::constants);
		ASSERT_FALSE (store.init_error ());
	}
}

TEST (mdb_block_store, bad_path)
{
	if (scendere::rocksdb_config::using_rocksdb_in_tests ())
	{
		// Don't test this in rocksdb mode
		return;
	}
	scendere::logger_mt logger;
	scendere::mdb_store store (logger, boost::filesystem::path ("///"), scendere::dev::constants);
	ASSERT_TRUE (store.init_error ());
}

TEST (block_store, DISABLED_already_open) // File can be shared
{
	auto path (scendere::unique_path ());
	boost::filesystem::create_directories (path.parent_path ());
	scendere::set_secure_perm_directory (path.parent_path ());
	std::ofstream file;
	file.open (path.string ().c_str ());
	ASSERT_TRUE (file.is_open ());
	scendere::logger_mt logger;
	auto store = scendere::make_store (logger, path, scendere::dev::constants);
	ASSERT_TRUE (store->init_error ());
}

TEST (block_store, roots)
{
	scendere::logger_mt logger;
	auto store = scendere::make_store (logger, scendere::unique_path (), scendere::dev::constants);
	ASSERT_TRUE (!store->init_error ());
	scendere::send_block send_block (0, 1, 2, scendere::keypair ().prv, 4, 5);
	ASSERT_EQ (send_block.hashables.previous, send_block.root ());
	scendere::change_block change_block (0, 1, scendere::keypair ().prv, 3, 4);
	ASSERT_EQ (change_block.hashables.previous, change_block.root ());
	scendere::receive_block receive_block (0, 1, scendere::keypair ().prv, 3, 4);
	ASSERT_EQ (receive_block.hashables.previous, receive_block.root ());
	scendere::open_block open_block (0, 1, 2, scendere::keypair ().prv, 4, 5);
	ASSERT_EQ (open_block.hashables.account, open_block.root ());
}

TEST (block_store, pending_exists)
{
	scendere::logger_mt logger;
	auto store = scendere::make_store (logger, scendere::unique_path (), scendere::dev::constants);
	ASSERT_TRUE (!store->init_error ());
	scendere::pending_key two (2, 0);
	scendere::pending_info pending;
	auto transaction (store->tx_begin_write ());
	store->pending.put (transaction, two, pending);
	scendere::pending_key one (1, 0);
	ASSERT_FALSE (store->pending.exists (transaction, one));
}

TEST (block_store, latest_exists)
{
	scendere::logger_mt logger;
	auto store = scendere::make_store (logger, scendere::unique_path (), scendere::dev::constants);
	ASSERT_TRUE (!store->init_error ());
	scendere::account two (2);
	scendere::account_info info;
	auto transaction (store->tx_begin_write ());
	store->confirmation_height.put (transaction, two, { 0, scendere::block_hash (0) });
	store->account.put (transaction, two, info);
	scendere::account one (1);
	ASSERT_FALSE (store->account.exists (transaction, one));
}

TEST (block_store, large_iteration)
{
	scendere::logger_mt logger;
	auto store = scendere::make_store (logger, scendere::unique_path (), scendere::dev::constants);
	ASSERT_TRUE (!store->init_error ());
	std::unordered_set<scendere::account> accounts1;
	for (auto i (0); i < 1000; ++i)
	{
		auto transaction (store->tx_begin_write ());
		scendere::account account;
		scendere::random_pool::generate_block (account.bytes.data (), account.bytes.size ());
		accounts1.insert (account);
		store->confirmation_height.put (transaction, account, { 0, scendere::block_hash (0) });
		store->account.put (transaction, account, scendere::account_info ());
	}
	std::unordered_set<scendere::account> accounts2;
	scendere::account previous{};
	auto transaction (store->tx_begin_read ());
	for (auto i (store->account.begin (transaction, 0)), n (store->account.end ()); i != n; ++i)
	{
		scendere::account current (i->first);
		ASSERT_GT (current.number (), previous.number ());
		accounts2.insert (current);
		previous = current;
	}
	ASSERT_EQ (accounts1, accounts2);
	// Reverse iteration
	std::unordered_set<scendere::account> accounts3;
	previous = std::numeric_limits<scendere::uint256_t>::max ();
	for (auto i (store->account.rbegin (transaction)), n (store->account.end ()); i != n; --i)
	{
		scendere::account current (i->first);
		ASSERT_LT (current.number (), previous.number ());
		accounts3.insert (current);
		previous = current;
	}
	ASSERT_EQ (accounts1, accounts3);
}

TEST (block_store, frontier)
{
	scendere::logger_mt logger;
	auto store = scendere::make_store (logger, scendere::unique_path (), scendere::dev::constants);
	ASSERT_TRUE (!store->init_error ());
	auto transaction (store->tx_begin_write ());
	scendere::block_hash hash (100);
	scendere::account account (200);
	ASSERT_TRUE (store->frontier.get (transaction, hash).is_zero ());
	store->frontier.put (transaction, hash, account);
	ASSERT_EQ (account, store->frontier.get (transaction, hash));
	store->frontier.del (transaction, hash);
	ASSERT_TRUE (store->frontier.get (transaction, hash).is_zero ());
}

TEST (block_store, block_replace)
{
	scendere::logger_mt logger;
	auto store = scendere::make_store (logger, scendere::unique_path (), scendere::dev::constants);
	ASSERT_TRUE (!store->init_error ());
	scendere::send_block send1 (0, 0, 0, scendere::keypair ().prv, 0, 1);
	send1.sideband_set ({});
	scendere::send_block send2 (0, 0, 0, scendere::keypair ().prv, 0, 2);
	send2.sideband_set ({});
	auto transaction (store->tx_begin_write ());
	store->block.put (transaction, 0, send1);
	store->block.put (transaction, 0, send2);
	auto block3 (store->block.get (transaction, 0));
	ASSERT_NE (nullptr, block3);
	ASSERT_EQ (2, block3->block_work ());
}

TEST (block_store, block_count)
{
	scendere::logger_mt logger;
	auto store = scendere::make_store (logger, scendere::unique_path (), scendere::dev::constants);
	ASSERT_TRUE (!store->init_error ());
	{
		auto transaction (store->tx_begin_write ());
		ASSERT_EQ (0, store->block.count (transaction));
		scendere::open_block block (0, 1, 0, scendere::keypair ().prv, 0, 0);
		block.sideband_set ({});
		auto hash1 (block.hash ());
		store->block.put (transaction, hash1, block);
	}
	auto transaction (store->tx_begin_read ());
	ASSERT_EQ (1, store->block.count (transaction));
}

TEST (block_store, account_count)
{
	scendere::logger_mt logger;
	auto store = scendere::make_store (logger, scendere::unique_path (), scendere::dev::constants);
	ASSERT_TRUE (!store->init_error ());
	{
		auto transaction (store->tx_begin_write ());
		ASSERT_EQ (0, store->account.count (transaction));
		scendere::account account (200);
		store->confirmation_height.put (transaction, account, { 0, scendere::block_hash (0) });
		store->account.put (transaction, account, scendere::account_info ());
	}
	auto transaction (store->tx_begin_read ());
	ASSERT_EQ (1, store->account.count (transaction));
}

TEST (block_store, cemented_count_cache)
{
	scendere::logger_mt logger;
	auto store = scendere::make_store (logger, scendere::unique_path (), scendere::dev::constants);
	ASSERT_TRUE (!store->init_error ());
	auto transaction (store->tx_begin_write ());
	scendere::ledger_cache ledger_cache;
	store->initialize (transaction, ledger_cache);
	ASSERT_EQ (1, ledger_cache.cemented_count);
}

TEST (block_store, block_random)
{
	scendere::logger_mt logger;
	auto store = scendere::make_store (logger, scendere::unique_path (), scendere::dev::constants);
	{
		scendere::ledger_cache ledger_cache;
		auto transaction (store->tx_begin_write ());
		store->initialize (transaction, ledger_cache);
	}
	auto transaction (store->tx_begin_read ());
	auto block (store->block.random (transaction));
	ASSERT_NE (nullptr, block);
	ASSERT_EQ (*block, *scendere::dev::genesis);
}

TEST (block_store, pruned_random)
{
	scendere::logger_mt logger;
	auto store = scendere::make_store (logger, scendere::unique_path (), scendere::dev::constants);
	ASSERT_TRUE (!store->init_error ());
	scendere::open_block block (0, 1, 0, scendere::keypair ().prv, 0, 0);
	block.sideband_set ({});
	auto hash1 (block.hash ());
	{
		scendere::ledger_cache ledger_cache;
		auto transaction (store->tx_begin_write ());
		store->initialize (transaction, ledger_cache);
		store->pruned.put (transaction, hash1);
	}
	auto transaction (store->tx_begin_read ());
	auto random_hash (store->pruned.random (transaction));
	ASSERT_EQ (hash1, random_hash);
}

// Databases need to be dropped in order to convert to dupsort compatible
TEST (block_store, DISABLED_change_dupsort) // Unchecked is no longer dupsort table
{
	auto path (scendere::unique_path ());
	scendere::logger_mt logger{};
	scendere::mdb_store store{ logger, path, scendere::dev::constants };
	scendere::unchecked_map unchecked{ store, false };
	auto transaction (store.tx_begin_write ());
	ASSERT_EQ (0, mdb_drop (store.env.tx (transaction), store.unchecked_handle, 1));
	ASSERT_EQ (0, mdb_dbi_open (store.env.tx (transaction), "unchecked", MDB_CREATE, &store.unchecked_handle));
	std::shared_ptr<scendere::block> send1 = std::make_shared<scendere::send_block> (0, 0, 0, scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub, 0);
	std::shared_ptr<scendere::block> send2 = std::make_shared<scendere::send_block> (1, 0, 0, scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub, 0);
	ASSERT_NE (send1->hash (), send2->hash ());
	unchecked.put (send1->hash (), send1);
	unchecked.put (send1->hash (), send2);
	ASSERT_EQ (0, mdb_drop (store.env.tx (transaction), store.unchecked_handle, 0));
	mdb_dbi_close (store.env, store.unchecked_handle);
	ASSERT_EQ (0, mdb_dbi_open (store.env.tx (transaction), "unchecked", MDB_CREATE | MDB_DUPSORT, &store.unchecked_handle));
	unchecked.put (send1->hash (), send1);
	unchecked.put (send1->hash (), send2);
	ASSERT_EQ (0, mdb_drop (store.env.tx (transaction), store.unchecked_handle, 1));
	ASSERT_EQ (0, mdb_dbi_open (store.env.tx (transaction), "unchecked", MDB_CREATE | MDB_DUPSORT, &store.unchecked_handle));
	unchecked.put (send1->hash (), send1);
	unchecked.put (send1->hash (), send2);
}

TEST (block_store, state_block)
{
	scendere::logger_mt logger;
	auto store = scendere::make_store (logger, scendere::unique_path (), scendere::dev::constants);
	ASSERT_FALSE (store->init_error ());
	scendere::keypair key1;
	scendere::state_block block1 (1, scendere::dev::genesis->hash (), 3, 4, 6, key1.prv, key1.pub, 7);
	block1.sideband_set ({});
	{
		scendere::ledger_cache ledger_cache;
		auto transaction (store->tx_begin_write ());
		store->initialize (transaction, ledger_cache);
		ASSERT_EQ (scendere::block_type::state, block1.type ());
		store->block.put (transaction, block1.hash (), block1);
		ASSERT_TRUE (store->block.exists (transaction, block1.hash ()));
		auto block2 (store->block.get (transaction, block1.hash ()));
		ASSERT_NE (nullptr, block2);
		ASSERT_EQ (block1, *block2);
	}
	{
		auto transaction (store->tx_begin_write ());
		auto count (store->block.count (transaction));
		ASSERT_EQ (2, count);
		store->block.del (transaction, block1.hash ());
		ASSERT_FALSE (store->block.exists (transaction, block1.hash ()));
	}
	auto transaction (store->tx_begin_read ());
	auto count2 (store->block.count (transaction));
	ASSERT_EQ (1, count2);
}

TEST (mdb_block_store, sideband_height)
{
	if (scendere::rocksdb_config::using_rocksdb_in_tests ())
	{
		// Don't test this in rocksdb mode
		return;
	}
	scendere::logger_mt logger;
	scendere::keypair key1;
	scendere::keypair key2;
	scendere::keypair key3;
	scendere::mdb_store store (logger, scendere::unique_path (), scendere::dev::constants);
	ASSERT_FALSE (store.init_error ());
	scendere::stat stat;
	scendere::ledger ledger (store, stat, scendere::dev::constants);
	auto transaction (store.tx_begin_write ());
	store.initialize (transaction, ledger.cache);
	scendere::work_pool pool{ scendere::dev::network_params.network, std::numeric_limits<unsigned>::max () };
	scendere::send_block send (scendere::dev::genesis->hash (), scendere::dev::genesis_key.pub, scendere::dev::constants.genesis_amount - scendere::Gxrb_ratio, scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub, *pool.generate (scendere::dev::genesis->hash ()));
	ASSERT_EQ (scendere::process_result::progress, ledger.process (transaction, send).code);
	scendere::receive_block receive (send.hash (), send.hash (), scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub, *pool.generate (send.hash ()));
	ASSERT_EQ (scendere::process_result::progress, ledger.process (transaction, receive).code);
	scendere::change_block change (receive.hash (), 0, scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub, *pool.generate (receive.hash ()));
	ASSERT_EQ (scendere::process_result::progress, ledger.process (transaction, change).code);
	scendere::state_block state_send1 (scendere::dev::genesis_key.pub, change.hash (), 0, scendere::dev::constants.genesis_amount - scendere::Gxrb_ratio, key1.pub, scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub, *pool.generate (change.hash ()));
	ASSERT_EQ (scendere::process_result::progress, ledger.process (transaction, state_send1).code);
	scendere::state_block state_send2 (scendere::dev::genesis_key.pub, state_send1.hash (), 0, scendere::dev::constants.genesis_amount - 2 * scendere::Gxrb_ratio, key2.pub, scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub, *pool.generate (state_send1.hash ()));
	ASSERT_EQ (scendere::process_result::progress, ledger.process (transaction, state_send2).code);
	scendere::state_block state_send3 (scendere::dev::genesis_key.pub, state_send2.hash (), 0, scendere::dev::constants.genesis_amount - 3 * scendere::Gxrb_ratio, key3.pub, scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub, *pool.generate (state_send2.hash ()));
	ASSERT_EQ (scendere::process_result::progress, ledger.process (transaction, state_send3).code);
	scendere::state_block state_open (key1.pub, 0, 0, scendere::Gxrb_ratio, state_send1.hash (), key1.prv, key1.pub, *pool.generate (key1.pub));
	ASSERT_EQ (scendere::process_result::progress, ledger.process (transaction, state_open).code);
	scendere::state_block epoch (key1.pub, state_open.hash (), 0, scendere::Gxrb_ratio, ledger.epoch_link (scendere::epoch::epoch_1), scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub, *pool.generate (state_open.hash ()));
	ASSERT_EQ (scendere::process_result::progress, ledger.process (transaction, epoch).code);
	ASSERT_EQ (scendere::epoch::epoch_1, store.block.version (transaction, epoch.hash ()));
	scendere::state_block epoch_open (key2.pub, 0, 0, 0, ledger.epoch_link (scendere::epoch::epoch_1), scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub, *pool.generate (key2.pub));
	ASSERT_EQ (scendere::process_result::progress, ledger.process (transaction, epoch_open).code);
	ASSERT_EQ (scendere::epoch::epoch_1, store.block.version (transaction, epoch_open.hash ()));
	scendere::state_block state_receive (key2.pub, epoch_open.hash (), 0, scendere::Gxrb_ratio, state_send2.hash (), key2.prv, key2.pub, *pool.generate (epoch_open.hash ()));
	ASSERT_EQ (scendere::process_result::progress, ledger.process (transaction, state_receive).code);
	scendere::open_block open (state_send3.hash (), scendere::dev::genesis_key.pub, key3.pub, key3.prv, key3.pub, *pool.generate (key3.pub));
	ASSERT_EQ (scendere::process_result::progress, ledger.process (transaction, open).code);
	auto block1 (store.block.get (transaction, scendere::dev::genesis->hash ()));
	ASSERT_EQ (block1->sideband ().height, 1);
	auto block2 (store.block.get (transaction, send.hash ()));
	ASSERT_EQ (block2->sideband ().height, 2);
	auto block3 (store.block.get (transaction, receive.hash ()));
	ASSERT_EQ (block3->sideband ().height, 3);
	auto block4 (store.block.get (transaction, change.hash ()));
	ASSERT_EQ (block4->sideband ().height, 4);
	auto block5 (store.block.get (transaction, state_send1.hash ()));
	ASSERT_EQ (block5->sideband ().height, 5);
	auto block6 (store.block.get (transaction, state_send2.hash ()));
	ASSERT_EQ (block6->sideband ().height, 6);
	auto block7 (store.block.get (transaction, state_send3.hash ()));
	ASSERT_EQ (block7->sideband ().height, 7);
	auto block8 (store.block.get (transaction, state_open.hash ()));
	ASSERT_EQ (block8->sideband ().height, 1);
	auto block9 (store.block.get (transaction, epoch.hash ()));
	ASSERT_EQ (block9->sideband ().height, 2);
	auto block10 (store.block.get (transaction, epoch_open.hash ()));
	ASSERT_EQ (block10->sideband ().height, 1);
	auto block11 (store.block.get (transaction, state_receive.hash ()));
	ASSERT_EQ (block11->sideband ().height, 2);
	auto block12 (store.block.get (transaction, open.hash ()));
	ASSERT_EQ (block12->sideband ().height, 1);
}

TEST (block_store, peers)
{
	scendere::logger_mt logger;
	auto store = scendere::make_store (logger, scendere::unique_path (), scendere::dev::constants);
	ASSERT_TRUE (!store->init_error ());

	scendere::endpoint_key endpoint (boost::asio::ip::address_v6::any ().to_bytes (), 100);
	{
		auto transaction (store->tx_begin_write ());

		// Confirm that the store is empty
		ASSERT_FALSE (store->peer.exists (transaction, endpoint));
		ASSERT_EQ (store->peer.count (transaction), 0);

		// Add one
		store->peer.put (transaction, endpoint);
		ASSERT_TRUE (store->peer.exists (transaction, endpoint));
	}

	// Confirm that it can be found
	{
		auto transaction (store->tx_begin_read ());
		ASSERT_EQ (store->peer.count (transaction), 1);
	}

	// Add another one and check that it (and the existing one) can be found
	scendere::endpoint_key endpoint1 (boost::asio::ip::address_v6::any ().to_bytes (), 101);
	{
		auto transaction (store->tx_begin_write ());
		store->peer.put (transaction, endpoint1);
		ASSERT_TRUE (store->peer.exists (transaction, endpoint1)); // Check new peer is here
		ASSERT_TRUE (store->peer.exists (transaction, endpoint)); // Check first peer is still here
	}

	{
		auto transaction (store->tx_begin_read ());
		ASSERT_EQ (store->peer.count (transaction), 2);
	}

	// Delete the first one
	{
		auto transaction (store->tx_begin_write ());
		store->peer.del (transaction, endpoint1);
		ASSERT_FALSE (store->peer.exists (transaction, endpoint1)); // Confirm it no longer exists
		ASSERT_TRUE (store->peer.exists (transaction, endpoint)); // Check first peer is still here
	}

	{
		auto transaction (store->tx_begin_read ());
		ASSERT_EQ (store->peer.count (transaction), 1);
	}

	// Delete original one
	{
		auto transaction (store->tx_begin_write ());
		store->peer.del (transaction, endpoint);
		ASSERT_FALSE (store->peer.exists (transaction, endpoint));
	}

	{
		auto transaction (store->tx_begin_read ());
		ASSERT_EQ (store->peer.count (transaction), 0);
	}
}

TEST (block_store, endpoint_key_byte_order)
{
	boost::asio::ip::address_v6 address (boost::asio::ip::make_address_v6 ("::ffff:127.0.0.1"));
	uint16_t port = 100;
	scendere::endpoint_key endpoint_key (address.to_bytes (), port);

	std::vector<uint8_t> bytes;
	{
		scendere::vectorstream stream (bytes);
		scendere::write (stream, endpoint_key);
	}

	// This checks that the endpoint is serialized as expected, with a size
	// of 18 bytes (16 for ipv6 address and 2 for port), both in network byte order.
	ASSERT_EQ (bytes.size (), 18);
	ASSERT_EQ (bytes[10], 0xff);
	ASSERT_EQ (bytes[11], 0xff);
	ASSERT_EQ (bytes[12], 127);
	ASSERT_EQ (bytes[bytes.size () - 2], 0);
	ASSERT_EQ (bytes.back (), 100);

	// Deserialize the same stream bytes
	scendere::bufferstream stream1 (bytes.data (), bytes.size ());
	scendere::endpoint_key endpoint_key1;
	scendere::read (stream1, endpoint_key1);

	// This should be in network bytes order
	ASSERT_EQ (address.to_bytes (), endpoint_key1.address_bytes ());

	// This should be in host byte order
	ASSERT_EQ (port, endpoint_key1.port ());
}

TEST (block_store, online_weight)
{
	scendere::logger_mt logger;
	auto store = scendere::make_store (logger, scendere::unique_path (), scendere::dev::constants);
	ASSERT_FALSE (store->init_error ());
	{
		auto transaction (store->tx_begin_write ());
		ASSERT_EQ (0, store->online_weight.count (transaction));
		ASSERT_EQ (store->online_weight.end (), store->online_weight.begin (transaction));
		ASSERT_EQ (store->online_weight.end (), store->online_weight.rbegin (transaction));
		store->online_weight.put (transaction, 1, 2);
		store->online_weight.put (transaction, 3, 4);
	}
	{
		auto transaction (store->tx_begin_write ());
		ASSERT_EQ (2, store->online_weight.count (transaction));
		auto item (store->online_weight.begin (transaction));
		ASSERT_NE (store->online_weight.end (), item);
		ASSERT_EQ (1, item->first);
		ASSERT_EQ (2, item->second.number ());
		auto item_last (store->online_weight.rbegin (transaction));
		ASSERT_NE (store->online_weight.end (), item_last);
		ASSERT_EQ (3, item_last->first);
		ASSERT_EQ (4, item_last->second.number ());
		store->online_weight.del (transaction, 1);
		ASSERT_EQ (1, store->online_weight.count (transaction));
		ASSERT_EQ (store->online_weight.begin (transaction), store->online_weight.rbegin (transaction));
		store->online_weight.del (transaction, 3);
	}
	auto transaction (store->tx_begin_read ());
	ASSERT_EQ (0, store->online_weight.count (transaction));
	ASSERT_EQ (store->online_weight.end (), store->online_weight.begin (transaction));
	ASSERT_EQ (store->online_weight.end (), store->online_weight.rbegin (transaction));
}

TEST (block_store, pruned_blocks)
{
	scendere::logger_mt logger;
	auto store = scendere::make_store (logger, scendere::unique_path (), scendere::dev::constants);
	ASSERT_TRUE (!store->init_error ());

	scendere::keypair key1;
	scendere::open_block block1 (0, 1, key1.pub, key1.prv, key1.pub, 0);
	auto hash1 (block1.hash ());
	{
		auto transaction (store->tx_begin_write ());

		// Confirm that the store is empty
		ASSERT_FALSE (store->pruned.exists (transaction, hash1));
		ASSERT_EQ (store->pruned.count (transaction), 0);

		// Add one
		store->pruned.put (transaction, hash1);
		ASSERT_TRUE (store->pruned.exists (transaction, hash1));
	}

	// Confirm that it can be found
	ASSERT_EQ (store->pruned.count (store->tx_begin_read ()), 1);

	// Add another one and check that it (and the existing one) can be found
	scendere::open_block block2 (1, 2, key1.pub, key1.prv, key1.pub, 0);
	block2.sideband_set ({});
	auto hash2 (block2.hash ());
	{
		auto transaction (store->tx_begin_write ());
		store->pruned.put (transaction, hash2);
		ASSERT_TRUE (store->pruned.exists (transaction, hash2)); // Check new pruned hash is here
		ASSERT_FALSE (store->block.exists (transaction, hash2));
		ASSERT_TRUE (store->pruned.exists (transaction, hash1)); // Check first pruned hash is still here
		ASSERT_FALSE (store->block.exists (transaction, hash1));
	}

	ASSERT_EQ (store->pruned.count (store->tx_begin_read ()), 2);

	// Delete the first one
	{
		auto transaction (store->tx_begin_write ());
		store->pruned.del (transaction, hash2);
		ASSERT_FALSE (store->pruned.exists (transaction, hash2)); // Confirm it no longer exists
		ASSERT_FALSE (store->block.exists (transaction, hash2)); // true for block_exists
		store->block.put (transaction, hash2, block2); // Add corresponding block
		ASSERT_TRUE (store->block.exists (transaction, hash2));
		ASSERT_TRUE (store->pruned.exists (transaction, hash1)); // Check first pruned hash is still here
		ASSERT_FALSE (store->block.exists (transaction, hash1));
	}

	ASSERT_EQ (store->pruned.count (store->tx_begin_read ()), 1);

	// Delete original one
	{
		auto transaction (store->tx_begin_write ());
		store->pruned.del (transaction, hash1);
		ASSERT_FALSE (store->pruned.exists (transaction, hash1));
	}

	ASSERT_EQ (store->pruned.count (store->tx_begin_read ()), 0);
}

TEST (mdb_block_store, upgrade_v14_v15)
{
	if (scendere::rocksdb_config::using_rocksdb_in_tests ())
	{
		// Don't test this in rocksdb mode
		return;
	}
	// Extract confirmation height to a separate database
	auto path (scendere::unique_path ());
	scendere::work_pool pool{ scendere::dev::network_params.network, std::numeric_limits<unsigned>::max () };
	scendere::send_block send (scendere::dev::genesis->hash (), scendere::dev::genesis_key.pub, scendere::dev::constants.genesis_amount - scendere::Gxrb_ratio, scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub, *pool.generate (scendere::dev::genesis->hash ()));
	scendere::state_block epoch (scendere::dev::genesis_key.pub, send.hash (), scendere::dev::genesis_key.pub, scendere::dev::constants.genesis_amount - scendere::Gxrb_ratio, scendere::dev::network_params.ledger.epochs.link (scendere::epoch::epoch_1), scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub, *pool.generate (send.hash ()));
	scendere::state_block state_send (scendere::dev::genesis_key.pub, epoch.hash (), scendere::dev::genesis_key.pub, scendere::dev::constants.genesis_amount - scendere::Gxrb_ratio * 2, scendere::dev::genesis_key.pub, scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub, *pool.generate (epoch.hash ()));
	{
		scendere::logger_mt logger;
		scendere::mdb_store store (logger, path, scendere::dev::constants);
		scendere::stat stats;
		scendere::ledger ledger (store, stats, scendere::dev::constants);
		auto transaction (store.tx_begin_write ());
		store.initialize (transaction, ledger.cache);
		scendere::account_info account_info;
		ASSERT_FALSE (store.account.get (transaction, scendere::dev::genesis->account (), account_info));
		scendere::confirmation_height_info confirmation_height_info;
		ASSERT_FALSE (store.confirmation_height.get (transaction, scendere::dev::genesis->account (), confirmation_height_info));
		ASSERT_EQ (confirmation_height_info.height, 1);
		ASSERT_EQ (confirmation_height_info.frontier, scendere::dev::genesis->hash ());
		// These databases get removed after an upgrade, so readd them
		ASSERT_FALSE (mdb_dbi_open (store.env.tx (transaction), "state_v1", MDB_CREATE, &store.state_blocks_v1_handle));
		ASSERT_FALSE (mdb_dbi_open (store.env.tx (transaction), "accounts_v1", MDB_CREATE, &store.accounts_v1_handle));
		ASSERT_FALSE (mdb_dbi_open (store.env.tx (transaction), "pending_v1", MDB_CREATE, &store.pending_v1_handle));
		ASSERT_FALSE (mdb_dbi_open (store.env.tx (transaction), "open", MDB_CREATE, &store.open_blocks_handle));
		ASSERT_FALSE (mdb_dbi_open (store.env.tx (transaction), "send", MDB_CREATE, &store.send_blocks_handle));
		ASSERT_FALSE (mdb_dbi_open (store.env.tx (transaction), "state_blocks", MDB_CREATE, &store.state_blocks_handle));
		ASSERT_EQ (scendere::process_result::progress, ledger.process (transaction, send).code);
		ASSERT_EQ (scendere::process_result::progress, ledger.process (transaction, epoch).code);
		ASSERT_EQ (scendere::process_result::progress, ledger.process (transaction, state_send).code);
		// Lower the database to the previous version
		store.version.put (transaction, 14);
		store.confirmation_height.del (transaction, scendere::dev::genesis->account ());
		modify_account_info_to_v14 (store, transaction, scendere::dev::genesis->account (), confirmation_height_info.height, state_send.hash ());

		store.pending.del (transaction, scendere::pending_key (scendere::dev::genesis->account (), state_send.hash ()));

		write_sideband_v14 (store, transaction, state_send, store.state_blocks_v1_handle);
		write_sideband_v14 (store, transaction, epoch, store.state_blocks_v1_handle);
		write_block_w_sideband_v18 (store, store.open_blocks_handle, transaction, *scendere::dev::genesis);
		write_block_w_sideband_v18 (store, store.send_blocks_handle, transaction, send);

		// Remove from blocks table
		store.block.del (transaction, state_send.hash ());
		store.block.del (transaction, epoch.hash ());

		// Turn pending into v14
		ASSERT_FALSE (mdb_put (store.env.tx (transaction), store.pending_v0_handle, scendere::mdb_val (scendere::pending_key (scendere::dev::genesis_key.pub, send.hash ())), scendere::mdb_val (scendere::pending_info_v14 (scendere::dev::genesis->account (), scendere::Gxrb_ratio, scendere::epoch::epoch_0)), 0));
		ASSERT_FALSE (mdb_put (store.env.tx (transaction), store.pending_v1_handle, scendere::mdb_val (scendere::pending_key (scendere::dev::genesis_key.pub, state_send.hash ())), scendere::mdb_val (scendere::pending_info_v14 (scendere::dev::genesis->account (), scendere::Gxrb_ratio, scendere::epoch::epoch_1)), 0));

		// This should fail as sizes are no longer correct for account_info
		scendere::mdb_val value;
		ASSERT_FALSE (mdb_get (store.env.tx (transaction), store.accounts_v1_handle, scendere::mdb_val (scendere::dev::genesis->account ()), value));
		scendere::account_info info;
		ASSERT_NE (value.size (), info.db_size ());
		store.account.del (transaction, scendere::dev::genesis->account ());

		// Confirmation height for the account should be deleted
		ASSERT_TRUE (mdb_get (store.env.tx (transaction), store.confirmation_height_handle, scendere::mdb_val (scendere::dev::genesis->account ()), value));
	}

	// Now do the upgrade
	scendere::logger_mt logger;
	scendere::mdb_store store (logger, path, scendere::dev::constants);
	ASSERT_FALSE (store.init_error ());
	auto transaction (store.tx_begin_read ());

	// Size of account_info should now equal that set in db
	scendere::mdb_val value;
	ASSERT_FALSE (mdb_get (store.env.tx (transaction), store.accounts_handle, scendere::mdb_val (scendere::dev::genesis->account ()), value));
	scendere::account_info info (value);
	ASSERT_EQ (value.size (), info.db_size ());

	// Confirmation height should exist
	scendere::confirmation_height_info confirmation_height_info;
	ASSERT_FALSE (store.confirmation_height.get (transaction, scendere::dev::genesis->account (), confirmation_height_info));
	ASSERT_EQ (confirmation_height_info.height, 1);
	ASSERT_EQ (confirmation_height_info.frontier, scendere::dev::genesis->hash ());

	// accounts_v1, state_blocks_v1 & pending_v1 tables should be deleted
	auto error_get_accounts_v1 (mdb_get (store.env.tx (transaction), store.accounts_v1_handle, scendere::mdb_val (scendere::dev::genesis->account ()), value));
	ASSERT_NE (error_get_accounts_v1, MDB_SUCCESS);
	auto error_get_pending_v1 (mdb_get (store.env.tx (transaction), store.pending_v1_handle, scendere::mdb_val (scendere::pending_key (scendere::dev::genesis_key.pub, state_send.hash ())), value));
	ASSERT_NE (error_get_pending_v1, MDB_SUCCESS);
	auto error_get_state_v1 (mdb_get (store.env.tx (transaction), store.state_blocks_v1_handle, scendere::mdb_val (state_send.hash ()), value));
	ASSERT_NE (error_get_state_v1, MDB_SUCCESS);

	// Check that the epochs are set correctly for the sideband, accounts and pending entries
	auto block = store.block.get (transaction, state_send.hash ());
	ASSERT_NE (block, nullptr);
	ASSERT_EQ (block->sideband ().details.epoch, scendere::epoch::epoch_1);
	block = store.block.get (transaction, send.hash ());
	ASSERT_NE (block, nullptr);
	ASSERT_EQ (block->sideband ().details.epoch, scendere::epoch::epoch_0);
	ASSERT_EQ (info.epoch (), scendere::epoch::epoch_1);
	scendere::pending_info pending_info;
	store.pending.get (transaction, scendere::pending_key (scendere::dev::genesis_key.pub, send.hash ()), pending_info);
	ASSERT_EQ (pending_info.epoch, scendere::epoch::epoch_0);
	store.pending.get (transaction, scendere::pending_key (scendere::dev::genesis_key.pub, state_send.hash ()), pending_info);
	ASSERT_EQ (pending_info.epoch, scendere::epoch::epoch_1);

	// Version should be correct
	ASSERT_LT (14, store.version.get (transaction));
}

TEST (mdb_block_store, upgrade_v15_v16)
{
	if (scendere::rocksdb_config::using_rocksdb_in_tests ())
	{
		// Don't test this in rocksdb mode
		return;
	}
	auto path (scendere::unique_path ());
	scendere::mdb_val value;
	{
		scendere::logger_mt logger;
		scendere::mdb_store store (logger, path, scendere::dev::constants);
		scendere::stat stats;
		scendere::ledger ledger (store, stats, scendere::dev::constants);
		auto transaction (store.tx_begin_write ());
		store.initialize (transaction, ledger.cache);
		// The representation table should get removed after, so readd it so that we can later confirm this actually happens
		auto txn = store.env.tx (transaction);
		ASSERT_FALSE (mdb_dbi_open (txn, "representation", MDB_CREATE, &store.representation_handle));
		auto weight = ledger.cache.rep_weights.representation_get (scendere::dev::genesis->account ());
		ASSERT_EQ (MDB_SUCCESS, mdb_put (txn, store.representation_handle, scendere::mdb_val (scendere::dev::genesis->account ()), scendere::mdb_val (scendere::uint128_union (weight)), 0));
		ASSERT_FALSE (mdb_dbi_open (store.env.tx (transaction), "open", MDB_CREATE, &store.open_blocks_handle));
		write_block_w_sideband_v18 (store, store.open_blocks_handle, transaction, *scendere::dev::genesis);
		// Lower the database to the previous version
		store.version.put (transaction, 15);
		// Confirm the rep weight exists in the database
		ASSERT_EQ (MDB_SUCCESS, mdb_get (store.env.tx (transaction), store.representation_handle, scendere::mdb_val (scendere::dev::genesis->account ()), value));
		store.confirmation_height.del (transaction, scendere::dev::genesis->account ());
	}

	// Now do the upgrade
	scendere::logger_mt logger;
	scendere::mdb_store store (logger, path, scendere::dev::constants);
	ASSERT_FALSE (store.init_error ());
	auto transaction (store.tx_begin_read ());

	// The representation table should now be deleted
	auto error_get_representation (mdb_get (store.env.tx (transaction), store.representation_handle, scendere::mdb_val (scendere::dev::genesis->account ()), value));
	ASSERT_NE (MDB_SUCCESS, error_get_representation);
	ASSERT_EQ (store.representation_handle, 0);

	// Version should be correct
	ASSERT_LT (15, store.version.get (transaction));
}

TEST (mdb_block_store, upgrade_v16_v17)
{
	if (scendere::rocksdb_config::using_rocksdb_in_tests ())
	{
		// Don't test this in rocksdb mode
		return;
	}
	scendere::work_pool pool{ scendere::dev::network_params.network, std::numeric_limits<unsigned>::max () };
	scendere::state_block block1 (scendere::dev::genesis_key.pub, scendere::dev::genesis->hash (), scendere::dev::genesis_key.pub, scendere::dev::constants.genesis_amount - scendere::Gxrb_ratio, scendere::dev::genesis_key.pub, scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub, *pool.generate (scendere::dev::genesis->hash ()));
	scendere::state_block block2 (scendere::dev::genesis_key.pub, block1.hash (), scendere::dev::genesis_key.pub, scendere::dev::constants.genesis_amount - scendere::Gxrb_ratio - 1, scendere::dev::genesis_key.pub, scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub, *pool.generate (block1.hash ()));
	scendere::state_block block3 (scendere::dev::genesis_key.pub, block2.hash (), scendere::dev::genesis_key.pub, scendere::dev::constants.genesis_amount - scendere::Gxrb_ratio - 2, scendere::dev::genesis_key.pub, scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub, *pool.generate (block2.hash ()));

	auto code = [&block1, &block2, &block3] (auto confirmation_height, scendere::block_hash const & expected_cemented_frontier) {
		auto path (scendere::unique_path ());
		scendere::mdb_val value;
		{
			scendere::logger_mt logger;
			scendere::mdb_store store (logger, path, scendere::dev::constants);
			scendere::stat stats;
			scendere::ledger ledger (store, stats, scendere::dev::constants);
			auto transaction (store.tx_begin_write ());
			store.initialize (transaction, ledger.cache);
			ASSERT_EQ (scendere::process_result::progress, ledger.process (transaction, block1).code);
			ASSERT_EQ (scendere::process_result::progress, ledger.process (transaction, block2).code);
			ASSERT_EQ (scendere::process_result::progress, ledger.process (transaction, block3).code);
			modify_confirmation_height_to_v15 (store, transaction, scendere::dev::genesis->account (), confirmation_height);

			ASSERT_FALSE (mdb_dbi_open (store.env.tx (transaction), "open", MDB_CREATE, &store.open_blocks_handle));
			write_block_w_sideband_v18 (store, store.open_blocks_handle, transaction, *scendere::dev::genesis);
			ASSERT_FALSE (mdb_dbi_open (store.env.tx (transaction), "state_blocks", MDB_CREATE, &store.state_blocks_handle));
			write_block_w_sideband_v18 (store, store.state_blocks_handle, transaction, block1);
			write_block_w_sideband_v18 (store, store.state_blocks_handle, transaction, block2);
			write_block_w_sideband_v18 (store, store.state_blocks_handle, transaction, block3);

			// Lower the database to the previous version
			store.version.put (transaction, 16);
		}

		// Now do the upgrade
		scendere::logger_mt logger;
		scendere::mdb_store store (logger, path, scendere::dev::constants);
		ASSERT_FALSE (store.init_error ());
		auto transaction (store.tx_begin_read ());

		scendere::confirmation_height_info confirmation_height_info;
		ASSERT_FALSE (store.confirmation_height.get (transaction, scendere::dev::genesis->account (), confirmation_height_info));
		ASSERT_EQ (confirmation_height_info.height, confirmation_height);

		// Check confirmation height frontier is correct
		ASSERT_EQ (confirmation_height_info.frontier, expected_cemented_frontier);

		// Version should be correct
		ASSERT_LT (16, store.version.get (transaction));
	};

	code (0, scendere::block_hash (0));
	code (1, scendere::dev::genesis->hash ());
	code (2, block1.hash ());
	code (3, block2.hash ());
	code (4, block3.hash ());
}

TEST (mdb_block_store, upgrade_v17_v18)
{
	if (scendere::rocksdb_config::using_rocksdb_in_tests ())
	{
		// Don't test this in rocksdb mode
		return;
	}
	auto path (scendere::unique_path ());
	scendere::keypair key1;
	scendere::keypair key2;
	scendere::keypair key3;
	scendere::work_pool pool{ scendere::dev::network_params.network, std::numeric_limits<unsigned>::max () };
	scendere::send_block send_zero (scendere::dev::genesis->hash (), scendere::dev::genesis_key.pub, scendere::dev::constants.genesis_amount, scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub, *pool.generate (scendere::dev::genesis->hash ()));
	scendere::state_block state_receive_zero (scendere::dev::genesis_key.pub, send_zero.hash (), scendere::dev::genesis_key.pub, scendere::dev::constants.genesis_amount, send_zero.hash (), scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub, *pool.generate (send_zero.hash ()));
	scendere::state_block epoch (scendere::dev::genesis_key.pub, state_receive_zero.hash (), scendere::dev::genesis_key.pub, scendere::dev::constants.genesis_amount, scendere::dev::network_params.ledger.epochs.link (scendere::epoch::epoch_1), scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub, *pool.generate (state_receive_zero.hash ()));
	scendere::state_block state_send (scendere::dev::genesis_key.pub, epoch.hash (), scendere::dev::genesis_key.pub, scendere::dev::constants.genesis_amount - scendere::Gxrb_ratio, scendere::dev::genesis_key.pub, scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub, *pool.generate (epoch.hash ()));
	scendere::state_block state_receive (scendere::dev::genesis_key.pub, state_send.hash (), scendere::dev::genesis_key.pub, scendere::dev::constants.genesis_amount, state_send.hash (), scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub, *pool.generate (state_send.hash ()));
	scendere::state_block state_change (scendere::dev::genesis_key.pub, state_receive.hash (), scendere::dev::genesis_key.pub, scendere::dev::constants.genesis_amount, 0, scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub, *pool.generate (state_receive.hash ()));
	scendere::state_block state_send_change (scendere::dev::genesis_key.pub, state_change.hash (), key1.pub, scendere::dev::constants.genesis_amount - scendere::Gxrb_ratio, key1.pub, scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub, *pool.generate (state_change.hash ()));
	scendere::state_block epoch_first (key1.pub, 0, 0, 0, scendere::dev::network_params.ledger.epochs.link (scendere::epoch::epoch_2), scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub, *pool.generate (key1.pub));
	scendere::state_block state_receive2 (key1.pub, epoch_first.hash (), key1.pub, scendere::Gxrb_ratio, state_send_change.hash (), key1.prv, key1.pub, *pool.generate (epoch_first.hash ()));
	scendere::state_block state_send2 (scendere::dev::genesis_key.pub, state_send_change.hash (), key1.pub, scendere::dev::constants.genesis_amount - scendere::Gxrb_ratio * 2, key2.pub, scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub, *pool.generate (state_send_change.hash ()));
	scendere::state_block state_open (key2.pub, 0, key2.pub, scendere::Gxrb_ratio, state_send2.hash (), key2.prv, key2.pub, *pool.generate (key2.pub));
	scendere::state_block state_send_epoch_link (key2.pub, state_open.hash (), key2.pub, 0, scendere::dev::network_params.ledger.epochs.link (scendere::epoch::epoch_2), key2.prv, key2.pub, *pool.generate (state_open.hash ()));
	{
		scendere::logger_mt logger;
		scendere::mdb_store store (logger, path, scendere::dev::constants);
		auto transaction (store.tx_begin_write ());
		scendere::stat stats;
		scendere::ledger ledger (store, stats, scendere::dev::constants);
		store.initialize (transaction, ledger.cache);
		ASSERT_EQ (scendere::process_result::progress, ledger.process (transaction, send_zero).code);
		ASSERT_EQ (scendere::process_result::progress, ledger.process (transaction, state_receive_zero).code);
		ASSERT_EQ (scendere::process_result::progress, ledger.process (transaction, epoch).code);
		ASSERT_EQ (scendere::process_result::progress, ledger.process (transaction, state_send).code);
		ASSERT_EQ (scendere::process_result::progress, ledger.process (transaction, state_receive).code);
		ASSERT_EQ (scendere::process_result::progress, ledger.process (transaction, state_change).code);
		ASSERT_EQ (scendere::process_result::progress, ledger.process (transaction, state_send_change).code);
		ASSERT_EQ (scendere::process_result::progress, ledger.process (transaction, epoch_first).code);
		ASSERT_EQ (scendere::process_result::progress, ledger.process (transaction, state_receive2).code);
		ASSERT_EQ (scendere::process_result::progress, ledger.process (transaction, state_send2).code);
		ASSERT_EQ (scendere::process_result::progress, ledger.process (transaction, state_open).code);
		ASSERT_EQ (scendere::process_result::progress, ledger.process (transaction, state_send_epoch_link).code);

		ASSERT_FALSE (mdb_dbi_open (store.env.tx (transaction), "open", MDB_CREATE, &store.open_blocks_handle));
		ASSERT_FALSE (mdb_dbi_open (store.env.tx (transaction), "send", MDB_CREATE, &store.send_blocks_handle));
		ASSERT_FALSE (mdb_dbi_open (store.env.tx (transaction), "state_blocks", MDB_CREATE, &store.state_blocks_handle));

		// Downgrade the store
		store.version.put (transaction, 17);

		write_block_w_sideband_v18 (store, store.state_blocks_handle, transaction, state_receive);
		write_block_w_sideband_v18 (store, store.state_blocks_handle, transaction, epoch_first);
		write_block_w_sideband_v18 (store, store.state_blocks_handle, transaction, state_send2);
		write_block_w_sideband_v18 (store, store.state_blocks_handle, transaction, state_send_epoch_link);
		write_block_w_sideband_v18 (store, store.open_blocks_handle, transaction, *scendere::dev::genesis);
		write_block_w_sideband_v18 (store, store.send_blocks_handle, transaction, send_zero);

		// Replace with the previous sideband version for state blocks
		// The upgrade can resume after upgrading some blocks, test this by only downgrading some of them
		write_sideband_v15 (store, transaction, state_receive_zero);
		write_sideband_v15 (store, transaction, epoch);
		write_sideband_v15 (store, transaction, state_send);
		write_sideband_v15 (store, transaction, state_change);
		write_sideband_v15 (store, transaction, state_send_change);
		write_sideband_v15 (store, transaction, state_receive2);
		write_sideband_v15 (store, transaction, state_open);

		store.block.del (transaction, state_receive_zero.hash ());
		store.block.del (transaction, epoch.hash ());
		store.block.del (transaction, state_send.hash ());
		store.block.del (transaction, state_change.hash ());
		store.block.del (transaction, state_send_change.hash ());
		store.block.del (transaction, state_receive2.hash ());
		store.block.del (transaction, state_open.hash ());
	}

	// Now do the upgrade
	scendere::logger_mt logger;
	scendere::mdb_store store (logger, path, scendere::dev::constants);
	ASSERT_FALSE (store.init_error ());
	auto transaction (store.tx_begin_read ());

	// Size of state block should equal that set in db (no change)
	scendere::mdb_val value;
	ASSERT_FALSE (mdb_get (store.env.tx (transaction), store.blocks_handle, scendere::mdb_val (state_send.hash ()), value));
	ASSERT_EQ (value.size (), sizeof (scendere::block_type) + scendere::state_block::size + scendere::block_sideband::size (scendere::block_type::state));

	// Check that sidebands are correctly populated
	{
		// Non-state unaffected
		auto block = store.block.get (transaction, send_zero.hash ());
		ASSERT_NE (block, nullptr);
		// All defaults
		ASSERT_EQ (block->sideband ().details.epoch, scendere::epoch::epoch_0);
		ASSERT_FALSE (block->sideband ().details.is_epoch);
		ASSERT_FALSE (block->sideband ().details.is_send);
		ASSERT_FALSE (block->sideband ().details.is_receive);
	}
	{
		// State receive from old zero send
		auto block = store.block.get (transaction, state_receive_zero.hash ());
		ASSERT_NE (block, nullptr);
		ASSERT_EQ (block->sideband ().details.epoch, scendere::epoch::epoch_0);
		ASSERT_FALSE (block->sideband ().details.is_epoch);
		ASSERT_FALSE (block->sideband ().details.is_send);
		ASSERT_TRUE (block->sideband ().details.is_receive);
	}
	{
		// Epoch
		auto block = store.block.get (transaction, epoch.hash ());
		ASSERT_NE (block, nullptr);
		ASSERT_EQ (block->sideband ().details.epoch, scendere::epoch::epoch_1);
		ASSERT_TRUE (block->sideband ().details.is_epoch);
		ASSERT_FALSE (block->sideband ().details.is_send);
		ASSERT_FALSE (block->sideband ().details.is_receive);
	}
	{
		// State send
		auto block = store.block.get (transaction, state_send.hash ());
		ASSERT_NE (block, nullptr);
		ASSERT_EQ (block->sideband ().details.epoch, scendere::epoch::epoch_1);
		ASSERT_FALSE (block->sideband ().details.is_epoch);
		ASSERT_TRUE (block->sideband ().details.is_send);
		ASSERT_FALSE (block->sideband ().details.is_receive);
	}
	{
		// State receive
		auto block = store.block.get (transaction, state_receive.hash ());
		ASSERT_NE (block, nullptr);
		ASSERT_EQ (block->sideband ().details.epoch, scendere::epoch::epoch_1);
		ASSERT_FALSE (block->sideband ().details.is_epoch);
		ASSERT_FALSE (block->sideband ().details.is_send);
		ASSERT_TRUE (block->sideband ().details.is_receive);
	}
	{
		// State change
		auto block = store.block.get (transaction, state_change.hash ());
		ASSERT_NE (block, nullptr);
		ASSERT_EQ (block->sideband ().details.epoch, scendere::epoch::epoch_1);
		ASSERT_FALSE (block->sideband ().details.is_epoch);
		ASSERT_FALSE (block->sideband ().details.is_send);
		ASSERT_FALSE (block->sideband ().details.is_receive);
	}
	{
		// State send + change
		auto block = store.block.get (transaction, state_send_change.hash ());
		ASSERT_NE (block, nullptr);
		ASSERT_EQ (block->sideband ().details.epoch, scendere::epoch::epoch_1);
		ASSERT_FALSE (block->sideband ().details.is_epoch);
		ASSERT_TRUE (block->sideband ().details.is_send);
		ASSERT_FALSE (block->sideband ().details.is_receive);
	}
	{
		// Epoch on unopened account
		auto block = store.block.get (transaction, epoch_first.hash ());
		ASSERT_NE (block, nullptr);
		ASSERT_EQ (block->sideband ().details.epoch, scendere::epoch::epoch_2);
		ASSERT_TRUE (block->sideband ().details.is_epoch);
		ASSERT_FALSE (block->sideband ().details.is_send);
		ASSERT_FALSE (block->sideband ().details.is_receive);
	}
	{
		// State open following epoch
		auto block = store.block.get (transaction, state_receive2.hash ());
		ASSERT_NE (block, nullptr);
		ASSERT_EQ (block->sideband ().details.epoch, scendere::epoch::epoch_2);
		ASSERT_FALSE (block->sideband ().details.is_epoch);
		ASSERT_FALSE (block->sideband ().details.is_send);
		ASSERT_TRUE (block->sideband ().details.is_receive);
	}
	{
		// Another state send
		auto block = store.block.get (transaction, state_send2.hash ());
		ASSERT_NE (block, nullptr);
		ASSERT_EQ (block->sideband ().details.epoch, scendere::epoch::epoch_1);
		ASSERT_FALSE (block->sideband ().details.is_epoch);
		ASSERT_TRUE (block->sideband ().details.is_send);
		ASSERT_FALSE (block->sideband ().details.is_receive);
	}
	{
		// State open
		auto block = store.block.get (transaction, state_open.hash ());
		ASSERT_NE (block, nullptr);
		ASSERT_EQ (block->sideband ().details.epoch, scendere::epoch::epoch_1);
		ASSERT_FALSE (block->sideband ().details.is_epoch);
		ASSERT_FALSE (block->sideband ().details.is_send);
		ASSERT_TRUE (block->sideband ().details.is_receive);
	}
	{
		// State send to an epoch link
		auto block = store.block.get (transaction, state_send_epoch_link.hash ());
		ASSERT_NE (block, nullptr);
		ASSERT_EQ (block->sideband ().details.epoch, scendere::epoch::epoch_1);
		ASSERT_FALSE (block->sideband ().details.is_epoch);
		ASSERT_TRUE (block->sideband ().details.is_send);
		ASSERT_FALSE (block->sideband ().details.is_receive);
	}
	// Version should be correct
	ASSERT_LT (17, store.version.get (transaction));
}

TEST (mdb_block_store, upgrade_v18_v19)
{
	if (scendere::rocksdb_config::using_rocksdb_in_tests ())
	{
		// Don't test this in rocksdb mode
		return;
	}
	auto path (scendere::unique_path ());
	scendere::keypair key1;
	scendere::work_pool pool{ scendere::dev::network_params.network, std::numeric_limits<unsigned>::max () };
	scendere::send_block send (scendere::dev::genesis->hash (), scendere::dev::genesis_key.pub, scendere::dev::constants.genesis_amount - scendere::Gxrb_ratio, scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub, *pool.generate (scendere::dev::genesis->hash ()));
	scendere::receive_block receive (send.hash (), send.hash (), scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub, *pool.generate (send.hash ()));
	scendere::change_block change (receive.hash (), 0, scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub, *pool.generate (receive.hash ()));
	scendere::state_block state_epoch (scendere::dev::genesis_key.pub, change.hash (), 0, scendere::dev::constants.genesis_amount, scendere::dev::network_params.ledger.epochs.link (scendere::epoch::epoch_1), scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub, *pool.generate (change.hash ()));
	scendere::state_block state_send (scendere::dev::genesis_key.pub, state_epoch.hash (), 0, scendere::dev::constants.genesis_amount - scendere::Gxrb_ratio, key1.pub, scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub, *pool.generate (state_epoch.hash ()));
	scendere::state_block state_open (key1.pub, 0, 0, scendere::Gxrb_ratio, state_send.hash (), key1.prv, key1.pub, *pool.generate (key1.pub));

	{
		scendere::logger_mt logger;
		scendere::mdb_store store (logger, path, scendere::dev::constants);
		scendere::stat stats;
		scendere::ledger ledger (store, stats, scendere::dev::constants);
		auto transaction (store.tx_begin_write ());
		store.initialize (transaction, ledger.cache);

		ASSERT_EQ (scendere::process_result::progress, ledger.process (transaction, send).code);
		ASSERT_EQ (scendere::process_result::progress, ledger.process (transaction, receive).code);
		ASSERT_EQ (scendere::process_result::progress, ledger.process (transaction, change).code);
		ASSERT_EQ (scendere::process_result::progress, ledger.process (transaction, state_epoch).code);
		ASSERT_EQ (scendere::process_result::progress, ledger.process (transaction, state_send).code);
		ASSERT_EQ (scendere::process_result::progress, ledger.process (transaction, state_open).code);

		// These tables need to be re-opened and populated so that an upgrade can be done
		auto txn = store.env.tx (transaction);
		ASSERT_FALSE (mdb_dbi_open (txn, "open", MDB_CREATE, &store.open_blocks_handle));
		ASSERT_FALSE (mdb_dbi_open (txn, "receive", MDB_CREATE, &store.receive_blocks_handle));
		ASSERT_FALSE (mdb_dbi_open (txn, "send", MDB_CREATE, &store.send_blocks_handle));
		ASSERT_FALSE (mdb_dbi_open (txn, "change", MDB_CREATE, &store.change_blocks_handle));
		ASSERT_FALSE (mdb_dbi_open (txn, "state_blocks", MDB_CREATE, &store.state_blocks_handle));

		// Modify blocks back to the old tables
		write_block_w_sideband_v18 (store, store.open_blocks_handle, transaction, *scendere::dev::genesis);
		write_block_w_sideband_v18 (store, store.send_blocks_handle, transaction, send);
		write_block_w_sideband_v18 (store, store.receive_blocks_handle, transaction, receive);
		write_block_w_sideband_v18 (store, store.change_blocks_handle, transaction, change);
		write_block_w_sideband_v18 (store, store.state_blocks_handle, transaction, state_epoch);
		write_block_w_sideband_v18 (store, store.state_blocks_handle, transaction, state_send);
		write_block_w_sideband_v18 (store, store.state_blocks_handle, transaction, state_open);

		store.version.put (transaction, 18);
	}

	// Now do the upgrade
	scendere::logger_mt logger;
	scendere::mdb_store store (logger, path, scendere::dev::constants);
	ASSERT_FALSE (store.init_error ());
	auto transaction (store.tx_begin_read ());

	// These tables should be deleted
	ASSERT_EQ (store.send_blocks_handle, 0);
	ASSERT_EQ (store.receive_blocks_handle, 0);
	ASSERT_EQ (store.change_blocks_handle, 0);
	ASSERT_EQ (store.open_blocks_handle, 0);
	ASSERT_EQ (store.state_blocks_handle, 0);

	// Confirm these blocks all exist after the upgrade
	ASSERT_TRUE (store.block.get (transaction, send.hash ()));
	ASSERT_TRUE (store.block.get (transaction, receive.hash ()));
	ASSERT_TRUE (store.block.get (transaction, change.hash ()));
	ASSERT_TRUE (store.block.get (transaction, scendere::dev::genesis->hash ()));
	auto state_epoch_disk (store.block.get (transaction, state_epoch.hash ()));
	ASSERT_NE (nullptr, state_epoch_disk);
	ASSERT_EQ (scendere::epoch::epoch_1, state_epoch_disk->sideband ().details.epoch);
	ASSERT_EQ (scendere::epoch::epoch_0, state_epoch_disk->sideband ().source_epoch); // Not used for epoch state blocks
	ASSERT_TRUE (store.block.get (transaction, state_send.hash ()));
	auto state_send_disk (store.block.get (transaction, state_send.hash ()));
	ASSERT_NE (nullptr, state_send_disk);
	ASSERT_EQ (scendere::epoch::epoch_1, state_send_disk->sideband ().details.epoch);
	ASSERT_EQ (scendere::epoch::epoch_0, state_send_disk->sideband ().source_epoch); // Not used for send state blocks
	ASSERT_TRUE (store.block.get (transaction, state_open.hash ()));
	auto state_open_disk (store.block.get (transaction, state_open.hash ()));
	ASSERT_NE (nullptr, state_open_disk);
	ASSERT_EQ (scendere::epoch::epoch_1, state_open_disk->sideband ().details.epoch);
	ASSERT_EQ (scendere::epoch::epoch_1, state_open_disk->sideband ().source_epoch);

	ASSERT_EQ (7, store.count (transaction, store.blocks_handle));

	// Version should be correct
	ASSERT_LT (18, store.version.get (transaction));
}

TEST (mdb_block_store, upgrade_v19_v20)
{
	if (scendere::rocksdb_config::using_rocksdb_in_tests ())
	{
		// Don't test this in rocksdb mode
		return;
	}
	auto path (scendere::unique_path ());
	scendere::logger_mt logger;
	scendere::stat stats;
	{
		scendere::mdb_store store (logger, path, scendere::dev::constants);
		scendere::ledger ledger (store, stats, scendere::dev::constants);
		auto transaction (store.tx_begin_write ());
		store.initialize (transaction, ledger.cache);
		// Delete pruned table
		ASSERT_FALSE (mdb_drop (store.env.tx (transaction), store.pruned_handle, 1));
		store.version.put (transaction, 19);
	}
	// Upgrading should create the table
	scendere::mdb_store store (logger, path, scendere::dev::constants);
	ASSERT_FALSE (store.init_error ());
	ASSERT_NE (store.pruned_handle, 0);

	// Version should be correct
	auto transaction (store.tx_begin_read ());
	ASSERT_LT (19, store.version.get (transaction));
}

TEST (mdb_block_store, upgrade_v20_v21)
{
	if (scendere::rocksdb_config::using_rocksdb_in_tests ())
	{
		// Don't test this in rocksdb mode
		return;
	}
	auto path (scendere::unique_path ());
	scendere::logger_mt logger;
	scendere::stat stats;
	{
		scendere::mdb_store store (logger, path, scendere::dev::constants);
		scendere::ledger ledger (store, stats, scendere::dev::constants);
		auto transaction (store.tx_begin_write ());
		store.initialize (transaction, ledger.cache);
		// Delete pruned table
		ASSERT_FALSE (mdb_drop (store.env.tx (transaction), store.final_votes_handle, 1));
		store.version.put (transaction, 20);
	}
	// Upgrading should create the table
	scendere::mdb_store store (logger, path, scendere::dev::constants);
	ASSERT_FALSE (store.init_error ());
	ASSERT_NE (store.final_votes_handle, 0);

	// Version should be correct
	auto transaction (store.tx_begin_read ());
	ASSERT_LT (19, store.version.get (transaction));
}

TEST (mdb_block_store, upgrade_backup)
{
	if (scendere::rocksdb_config::using_rocksdb_in_tests ())
	{
		// Don't test this in rocksdb mode
		return;
	}
	auto dir (scendere::unique_path ());
	namespace fs = boost::filesystem;
	fs::create_directory (dir);
	auto path = dir / "data.ldb";
	/** Returns 'dir' if backup file cannot be found */
	auto get_backup_path = [&dir] () {
		for (fs::directory_iterator itr (dir); itr != fs::directory_iterator (); ++itr)
		{
			if (itr->path ().filename ().string ().find ("data_backup_") != std::string::npos)
			{
				return itr->path ();
			}
		}
		return dir;
	};

	{
		scendere::logger_mt logger;
		scendere::mdb_store store (logger, path, scendere::dev::constants);
		auto transaction (store.tx_begin_write ());
		store.version.put (transaction, 14);
	}
	ASSERT_EQ (get_backup_path ().string (), dir.string ());

	// Now do the upgrade and confirm that backup is saved
	scendere::logger_mt logger;
	scendere::mdb_store store (logger, path, scendere::dev::constants, scendere::txn_tracking_config{}, std::chrono::seconds (5), scendere::lmdb_config{}, true);
	ASSERT_FALSE (store.init_error ());
	auto transaction (store.tx_begin_read ());
	ASSERT_LT (14, store.version.get (transaction));
	ASSERT_NE (get_backup_path ().string (), dir.string ());
}

// Test various confirmation height values as well as clearing them
TEST (block_store, confirmation_height)
{
	if (scendere::rocksdb_config::using_rocksdb_in_tests ())
	{
		// Don't test this in rocksdb mode
		return;
	}
	auto path (scendere::unique_path ());
	scendere::logger_mt logger;
	auto store = scendere::make_store (logger, path, scendere::dev::constants);

	scendere::account account1{};
	scendere::account account2{ 1 };
	scendere::account account3{ 2 };
	scendere::block_hash cemented_frontier1 (3);
	scendere::block_hash cemented_frontier2 (4);
	scendere::block_hash cemented_frontier3 (5);
	{
		auto transaction (store->tx_begin_write ());
		store->confirmation_height.put (transaction, account1, { 500, cemented_frontier1 });
		store->confirmation_height.put (transaction, account2, { std::numeric_limits<uint64_t>::max (), cemented_frontier2 });
		store->confirmation_height.put (transaction, account3, { 10, cemented_frontier3 });

		scendere::confirmation_height_info confirmation_height_info;
		ASSERT_FALSE (store->confirmation_height.get (transaction, account1, confirmation_height_info));
		ASSERT_EQ (confirmation_height_info.height, 500);
		ASSERT_EQ (confirmation_height_info.frontier, cemented_frontier1);
		ASSERT_FALSE (store->confirmation_height.get (transaction, account2, confirmation_height_info));
		ASSERT_EQ (confirmation_height_info.height, std::numeric_limits<uint64_t>::max ());
		ASSERT_EQ (confirmation_height_info.frontier, cemented_frontier2);
		ASSERT_FALSE (store->confirmation_height.get (transaction, account3, confirmation_height_info));
		ASSERT_EQ (confirmation_height_info.height, 10);
		ASSERT_EQ (confirmation_height_info.frontier, cemented_frontier3);

		// Check clearing of confirmation heights
		store->confirmation_height.clear (transaction);
	}
	auto transaction (store->tx_begin_read ());
	ASSERT_EQ (store->confirmation_height.count (transaction), 0);
	scendere::confirmation_height_info confirmation_height_info;
	ASSERT_TRUE (store->confirmation_height.get (transaction, account1, confirmation_height_info));
	ASSERT_TRUE (store->confirmation_height.get (transaction, account2, confirmation_height_info));
	ASSERT_TRUE (store->confirmation_height.get (transaction, account3, confirmation_height_info));
}

// Test various confirmation height values as well as clearing them
TEST (block_store, final_vote)
{
	if (scendere::rocksdb_config::using_rocksdb_in_tests ())
	{
		// Don't test this in rocksdb mode as deletions cause inaccurate counts
		return;
	}
	auto path (scendere::unique_path ());
	scendere::logger_mt logger;
	auto store = scendere::make_store (logger, path, scendere::dev::constants);

	{
		auto qualified_root = scendere::dev::genesis->qualified_root ();
		auto transaction (store->tx_begin_write ());
		store->final_vote.put (transaction, qualified_root, scendere::block_hash (2));
		ASSERT_EQ (store->final_vote.count (transaction), 1);
		store->final_vote.clear (transaction);
		ASSERT_EQ (store->final_vote.count (transaction), 0);
		store->final_vote.put (transaction, qualified_root, scendere::block_hash (2));
		ASSERT_EQ (store->final_vote.count (transaction), 1);
		// Clearing with incorrect root shouldn't remove
		store->final_vote.clear (transaction, qualified_root.previous ());
		ASSERT_EQ (store->final_vote.count (transaction), 1);
		// Clearing with correct root should remove
		store->final_vote.clear (transaction, qualified_root.root ());
		ASSERT_EQ (store->final_vote.count (transaction), 0);
	}
}

// Ledger versions are not forward compatible
TEST (block_store, incompatible_version)
{
	auto path (scendere::unique_path ());
	scendere::logger_mt logger;
	{
		auto store = scendere::make_store (logger, path, scendere::dev::constants);
		ASSERT_FALSE (store->init_error ());

		// Put version to an unreachable number so that it should always be incompatible
		auto transaction (store->tx_begin_write ());
		store->version.put (transaction, std::numeric_limits<int>::max ());
	}

	// Now try and read it, should give an error
	{
		auto store = scendere::make_store (logger, path, scendere::dev::constants, true);
		ASSERT_TRUE (store->init_error ());

		auto transaction = store->tx_begin_read ();
		auto version_l = store->version.get (transaction);
		ASSERT_EQ (version_l, std::numeric_limits<int>::max ());
	}
}

TEST (block_store, reset_renew_existing_transaction)
{
	scendere::logger_mt logger;
	auto store = scendere::make_store (logger, scendere::unique_path (), scendere::dev::constants);
	ASSERT_TRUE (!store->init_error ());

	scendere::keypair key1;
	scendere::open_block block (0, 1, 1, scendere::keypair ().prv, 0, 0);
	block.sideband_set ({});
	auto hash1 (block.hash ());
	auto read_transaction = store->tx_begin_read ();

	// Block shouldn't exist yet
	auto block_non_existing (store->block.get (read_transaction, hash1));
	ASSERT_EQ (nullptr, block_non_existing);

	// Release resources for the transaction
	read_transaction.reset ();

	// Write the block
	{
		auto write_transaction (store->tx_begin_write ());
		store->block.put (write_transaction, hash1, block);
	}

	read_transaction.renew ();

	// Block should exist now
	auto block_existing (store->block.get (read_transaction, hash1));
	ASSERT_NE (nullptr, block_existing);
}

TEST (block_store, rocksdb_force_test_env_variable)
{
	scendere::logger_mt logger;

	// Set environment variable
	constexpr auto env_var = "TEST_USE_ROCKSDB";
	auto value = std::getenv (env_var);

	auto store = scendere::make_store (logger, scendere::unique_path (), scendere::dev::constants);

	auto mdb_cast = dynamic_cast<scendere::mdb_store *> (store.get ());
	if (value && boost::lexical_cast<int> (value) == 1)
	{
		ASSERT_NE (boost::polymorphic_downcast<scendere::rocksdb_store *> (store.get ()), nullptr);
	}
	else
	{
		ASSERT_NE (mdb_cast, nullptr);
	}
}

namespace scendere
{
// This thest ensures the tombstone_count is increased when there is a delete. The tombstone_count is part of a flush
// logic bound to the way RocksDB is used by the node.
TEST (rocksdb_block_store, tombstone_count)
{
	if (scendere::rocksdb_config::using_rocksdb_in_tests ())
	{
		scendere::system system{};
		scendere::logger_mt logger{};
		auto store = std::make_unique<scendere::rocksdb_store> (logger, scendere::unique_path (), scendere::dev::constants);
		scendere::unchecked_map unchecked{ *store, false };
		ASSERT_TRUE (!store->init_error ());
		std::shared_ptr<scendere::block> block = std::make_shared<scendere::send_block> (0, 1, 2, scendere::keypair ().prv, 4, 5);
		// Enqueues a block to be saved in the database
		unchecked.put (block->previous (), block);
		auto check_block_is_listed = [&] (scendere::transaction const & transaction_a, scendere::block_hash const & block_hash_a) {
			return unchecked.get (transaction_a, block_hash_a).size () > 0;
		};
		// Waits for the block to get saved
		ASSERT_TIMELY (5s, check_block_is_listed (store->tx_begin_read (), block->previous ()));
		ASSERT_EQ (store->tombstone_map.at (scendere::tables::unchecked).num_since_last_flush.load (), 0);
		// Perorms a delete and checks for the tombstone counter
		unchecked.del (store->tx_begin_write (), scendere::unchecked_key (block->previous (), block->hash ()));
		ASSERT_EQ (store->tombstone_map.at (scendere::tables::unchecked).num_since_last_flush.load (), 1);
	}
}
}

namespace
{
void write_sideband_v14 (scendere::mdb_store & store_a, scendere::transaction & transaction_a, scendere::block const & block_a, MDB_dbi db_a)
{
	auto block = store_a.block.get (transaction_a, block_a.hash ());
	ASSERT_NE (block, nullptr);

	scendere::block_sideband_v14 sideband_v14 (block->type (), block->sideband ().account, block->sideband ().successor, block->sideband ().balance, block->sideband ().timestamp, block->sideband ().height);
	std::vector<uint8_t> data;
	{
		scendere::vectorstream stream (data);
		block_a.serialize (stream);
		sideband_v14.serialize (stream);
	}

	MDB_val val{ data.size (), data.data () };
	ASSERT_FALSE (mdb_put (store_a.env.tx (transaction_a), block->sideband ().details.epoch == scendere::epoch::epoch_0 ? store_a.state_blocks_v0_handle : store_a.state_blocks_v1_handle, scendere::mdb_val (block_a.hash ()), &val, 0));
}

void write_sideband_v15 (scendere::mdb_store & store_a, scendere::transaction & transaction_a, scendere::block const & block_a)
{
	auto block = store_a.block.get (transaction_a, block_a.hash ());
	ASSERT_NE (block, nullptr);

	ASSERT_LE (block->sideband ().details.epoch, scendere::epoch::max);
	// Simulated by writing 0 on every of the most significant bits, leaving out epoch only, as if pre-upgrade
	scendere::block_sideband_v18 sideband_v15 (block->sideband ().account, block->sideband ().successor, block->sideband ().balance, block->sideband ().timestamp, block->sideband ().height, block->sideband ().details.epoch, false, false, false);
	std::vector<uint8_t> data;
	{
		scendere::vectorstream stream (data);
		block_a.serialize (stream);
		sideband_v15.serialize (stream, block_a.type ());
	}

	MDB_val val{ data.size (), data.data () };
	ASSERT_FALSE (mdb_put (store_a.env.tx (transaction_a), store_a.state_blocks_handle, scendere::mdb_val (block_a.hash ()), &val, 0));
}

void write_block_w_sideband_v18 (scendere::mdb_store & store_a, MDB_dbi database, scendere::write_transaction & transaction_a, scendere::block const & block_a)
{
	auto block = store_a.block.get (transaction_a, block_a.hash ());
	ASSERT_NE (block, nullptr);
	auto new_sideband (block->sideband ());
	scendere::block_sideband_v18 sideband_v18 (new_sideband.account, new_sideband.successor, new_sideband.balance, new_sideband.height, new_sideband.timestamp, new_sideband.details.epoch, new_sideband.details.is_send, new_sideband.details.is_receive, new_sideband.details.is_epoch);

	std::vector<uint8_t> data;
	{
		scendere::vectorstream stream (data);
		block->serialize (stream);
		sideband_v18.serialize (stream, block->type ());
	}

	MDB_val val{ data.size (), data.data () };
	ASSERT_FALSE (mdb_put (store_a.env.tx (transaction_a), database, scendere::mdb_val (block_a.hash ()), &val, 0));
	store_a.del (transaction_a, scendere::tables::blocks, scendere::mdb_val (block_a.hash ()));
}

void modify_account_info_to_v14 (scendere::mdb_store & store, scendere::transaction const & transaction, scendere::account const & account, uint64_t confirmation_height, scendere::block_hash const & rep_block)
{
	scendere::account_info info;
	ASSERT_FALSE (store.account.get (transaction, account, info));
	scendere::account_info_v14 account_info_v14 (info.head, rep_block, info.open_block, info.balance, info.modified, info.block_count, confirmation_height, info.epoch ());
	auto status (mdb_put (store.env.tx (transaction), info.epoch () == scendere::epoch::epoch_0 ? store.accounts_v0_handle : store.accounts_v1_handle, scendere::mdb_val (account), scendere::mdb_val (account_info_v14), 0));
	ASSERT_EQ (status, 0);
}

void modify_confirmation_height_to_v15 (scendere::mdb_store & store, scendere::transaction const & transaction, scendere::account const & account, uint64_t confirmation_height)
{
	auto status (mdb_put (store.env.tx (transaction), store.confirmation_height_handle, scendere::mdb_val (account), scendere::mdb_val (confirmation_height), 0));
	ASSERT_EQ (status, 0);
}
}
