#include <scendere/test_common/system.hpp>
#include <scendere/test_common/testutil.hpp>

#include <gtest/gtest.h>

using namespace std::chrono_literals;

TEST (gap_cache, add_new)
{
	scendere::system system (1);
	scendere::gap_cache cache (*system.nodes[0]);
	auto block1 (std::make_shared<scendere::send_block> (0, 1, 2, scendere::keypair ().prv, 4, 5));
	cache.add (block1->hash ());
}

TEST (gap_cache, add_existing)
{
	scendere::system system (1);
	scendere::gap_cache cache (*system.nodes[0]);
	auto block1 (std::make_shared<scendere::send_block> (0, 1, 2, scendere::keypair ().prv, 4, 5));
	cache.add (block1->hash ());
	scendere::unique_lock<scendere::mutex> lock (cache.mutex);
	auto existing1 (cache.blocks.get<1> ().find (block1->hash ()));
	ASSERT_NE (cache.blocks.get<1> ().end (), existing1);
	auto arrival (existing1->arrival);
	lock.unlock ();
	ASSERT_TIMELY (20s, arrival != std::chrono::steady_clock::now ());
	cache.add (block1->hash ());
	ASSERT_EQ (1, cache.size ());
	lock.lock ();
	auto existing2 (cache.blocks.get<1> ().find (block1->hash ()));
	ASSERT_NE (cache.blocks.get<1> ().end (), existing2);
	ASSERT_GT (existing2->arrival, arrival);
}

TEST (gap_cache, comparison)
{
	scendere::system system (1);
	scendere::gap_cache cache (*system.nodes[0]);
	auto block1 (std::make_shared<scendere::send_block> (1, 0, 2, scendere::keypair ().prv, 4, 5));
	cache.add (block1->hash ());
	scendere::unique_lock<scendere::mutex> lock (cache.mutex);
	auto existing1 (cache.blocks.get<1> ().find (block1->hash ()));
	ASSERT_NE (cache.blocks.get<1> ().end (), existing1);
	auto arrival (existing1->arrival);
	lock.unlock ();
	ASSERT_TIMELY (20s, std::chrono::steady_clock::now () != arrival);
	auto block3 (std::make_shared<scendere::send_block> (0, 42, 1, scendere::keypair ().prv, 3, 4));
	cache.add (block3->hash ());
	ASSERT_EQ (2, cache.size ());
	lock.lock ();
	auto existing2 (cache.blocks.get<1> ().find (block3->hash ()));
	ASSERT_NE (cache.blocks.get<1> ().end (), existing2);
	ASSERT_GT (existing2->arrival, arrival);
	ASSERT_EQ (arrival, cache.blocks.get<1> ().begin ()->arrival);
}

// Upon receiving enough votes for a gapped block, a lazy bootstrap should be initiated
TEST (gap_cache, gap_bootstrap)
{
	scendere::node_flags node_flags;
	node_flags.disable_legacy_bootstrap = true;
	node_flags.disable_request_loop = true; // to avoid fallback behavior of broadcasting blocks
	scendere::system system (2, scendere::transport::transport_type::tcp, node_flags);

	auto & node1 (*system.nodes[0]);
	auto & node2 (*system.nodes[1]);
	scendere::block_hash latest (node1.latest (scendere::dev::genesis_key.pub));
	scendere::keypair key;
	auto send (std::make_shared<scendere::send_block> (latest, key.pub, scendere::dev::constants.genesis_amount - 100, scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub, *system.work.generate (latest)));
	ASSERT_EQ (scendere::process_result::progress, node1.process (*send).code);
	ASSERT_EQ (scendere::dev::constants.genesis_amount - 100, node1.balance (scendere::dev::genesis->account ()));
	ASSERT_EQ (scendere::dev::constants.genesis_amount, node2.balance (scendere::dev::genesis->account ()));
	// Confirm send block, allowing voting on the upcoming block
	node1.block_confirm (send);
	auto election = node1.active.election (send->qualified_root ());
	ASSERT_NE (nullptr, election);
	election->force_confirm ();
	ASSERT_TIMELY (2s, node1.block_confirmed (send->hash ()));
	node1.active.erase (*send);
	system.wallet (0)->insert_adhoc (scendere::dev::genesis_key.prv);
	auto latest_block (system.wallet (0)->send_action (scendere::dev::genesis_key.pub, key.pub, 100));
	ASSERT_NE (nullptr, latest_block);
	ASSERT_EQ (scendere::dev::constants.genesis_amount - 200, node1.balance (scendere::dev::genesis->account ()));
	ASSERT_EQ (scendere::dev::constants.genesis_amount, node2.balance (scendere::dev::genesis->account ()));
	ASSERT_TIMELY (10s, node2.balance (scendere::dev::genesis->account ()) == scendere::dev::constants.genesis_amount - 200);
}

TEST (gap_cache, two_dependencies)
{
	scendere::system system (1);
	auto & node1 (*system.nodes[0]);
	scendere::keypair key;
	auto send1 (std::make_shared<scendere::send_block> (scendere::dev::genesis->hash (), key.pub, 1, scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub, *system.work.generate (scendere::dev::genesis->hash ())));
	auto send2 (std::make_shared<scendere::send_block> (send1->hash (), key.pub, 0, scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub, *system.work.generate (send1->hash ())));
	auto open (std::make_shared<scendere::open_block> (send1->hash (), key.pub, key.pub, key.prv, key.pub, *system.work.generate (key.pub)));
	ASSERT_EQ (0, node1.gap_cache.size ());
	node1.block_processor.add (send2);
	node1.block_processor.flush ();
	ASSERT_EQ (1, node1.gap_cache.size ());
	node1.block_processor.add (open);
	node1.block_processor.flush ();
	ASSERT_EQ (2, node1.gap_cache.size ());
	node1.block_processor.add (send1);
	node1.block_processor.flush ();
	ASSERT_TIMELY (5s, node1.gap_cache.size () == 0);
	auto transaction (node1.store.tx_begin_read ());
	ASSERT_TRUE (node1.store.block.exists (transaction, send1->hash ()));
	ASSERT_TRUE (node1.store.block.exists (transaction, send2->hash ()));
	ASSERT_TRUE (node1.store.block.exists (transaction, open->hash ()));
}
