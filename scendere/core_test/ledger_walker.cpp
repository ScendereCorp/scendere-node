#include <scendere/node/ledger_walker.hpp>
#include <scendere/test_common/system.hpp>
#include <scendere/test_common/testutil.hpp>

#include <gtest/gtest.h>

#include <numeric>

// TODO: keep this until diskhash builds fine on Windows
#ifndef _WIN32

using namespace std::chrono_literals;

TEST (ledger_walker, genesis_block)
{
	scendere::system system{};
	auto const node = system.add_node ();

	scendere::ledger_walker ledger_walker{ node->ledger };

	std::size_t walked_blocks_count = 0;
	ledger_walker.walk_backward (scendere::dev::genesis->hash (),
	[&] (auto const & block) {
		++walked_blocks_count;
		EXPECT_EQ (block->hash (), scendere::dev::genesis->hash ());
	});

	EXPECT_EQ (walked_blocks_count, 1);

	walked_blocks_count = 0;
	ledger_walker.walk (scendere::dev::genesis->hash (),
	[&] (auto const & block) {
		++walked_blocks_count;
		EXPECT_EQ (block->hash (), scendere::dev::genesis->hash ());
	});

	EXPECT_EQ (walked_blocks_count, 1);
}

namespace scendere
{
TEST (ledger_walker, genesis_account_longer)
{
	scendere::system system{};
	scendere::node_config node_config (scendere::get_available_port (), system.logging);
	node_config.enable_voting = true;
	node_config.receive_minimum = 1;

	auto const node = system.add_node (node_config);

	scendere::ledger_walker ledger_walker{ node->ledger };
	EXPECT_TRUE (ledger_walker.walked_blocks.empty ());
	EXPECT_LE (ledger_walker.walked_blocks.bucket_count (), 1);
	EXPECT_TRUE (ledger_walker.blocks_to_walk.empty ());

	auto const get_number_of_walked_blocks = [&ledger_walker] (auto const & start_block_hash) {
		std::size_t walked_blocks_count = 0;
		ledger_walker.walk_backward (start_block_hash,
		[&] (auto const & block) {
			++walked_blocks_count;
		});

		return walked_blocks_count;
	};

	auto const transaction = node->ledger.store.tx_begin_read ();
	scendere::account_info genesis_account_info{};
	ASSERT_FALSE (node->ledger.store.account.get (transaction, scendere::dev::genesis_key.pub, genesis_account_info));
	EXPECT_EQ (get_number_of_walked_blocks (genesis_account_info.open_block), 1);
	EXPECT_EQ (get_number_of_walked_blocks (genesis_account_info.head), 1);

	system.wallet (0)->insert_adhoc (scendere::dev::genesis_key.prv);
	for (auto itr = 1; itr <= 5; ++itr)
	{
		auto const send = system.wallet (0)->send_action (scendere::dev::genesis_key.pub, scendere::dev::genesis_key.pub, 1);
		ASSERT_TRUE (send);
		EXPECT_EQ (get_number_of_walked_blocks (send->hash ()), 1 + itr * 2 - 1);
		ASSERT_TIMELY (3s, 1 + itr * 2 == node->ledger.cache.cemented_count);
		ASSERT_FALSE (node->ledger.store.account.get (transaction, scendere::dev::genesis_key.pub, genesis_account_info));
		// TODO: check issue with account head
		// EXPECT_EQ(get_number_of_walked_blocks (genesis_account_info.head), 1 + itr * 2);
	}

	EXPECT_TRUE (ledger_walker.walked_blocks.empty ());
	EXPECT_LE (ledger_walker.walked_blocks.bucket_count (), 1);
	EXPECT_TRUE (ledger_walker.blocks_to_walk.empty ());
}

}

TEST (ledger_walker, cross_account)
{
	scendere::system system{};
	scendere::node_config node_config (scendere::get_available_port (), system.logging);
	node_config.enable_voting = true;
	node_config.receive_minimum = 1;

	auto const node = system.add_node (node_config);

	system.wallet (0)->insert_adhoc (scendere::dev::genesis_key.prv);
	ASSERT_TRUE (system.wallet (0)->send_action (scendere::dev::genesis_key.pub, scendere::dev::genesis_key.pub, 1));
	ASSERT_TIMELY (3s, 3 == node->ledger.cache.cemented_count);

	scendere::keypair key{};
	system.wallet (0)->insert_adhoc (key.prv);
	ASSERT_TRUE (system.wallet (0)->send_action (scendere::dev::genesis_key.pub, key.pub, 1));
	ASSERT_TIMELY (3s, 5 == node->ledger.cache.cemented_count);

	auto const transaction = node->ledger.store.tx_begin_read ();
	scendere::account_info account_info{};
	ASSERT_FALSE (node->ledger.store.account.get (transaction, key.pub, account_info));

	//    TODO: check issue with account head
	//    auto const first = node->ledger.store.block.get_no_sideband(transaction, account_info.head);
	//    auto const second = node->ledger.store.block.get_no_sideband(transaction, first->previous());
	//    auto const third = node->ledger.store.block.get_no_sideband(transaction, second->previous());
	//    auto const fourth = node->ledger.store.block.get_no_sideband(transaction, third->previous());
	//    auto const fifth = node->ledger.store.block.get_no_sideband(transaction, fourth->previous());
	//
	//    auto const expected_blocks_to_walk = { first, second, third, fourth, fifth };
	//    auto expected_blocks_to_walk_itr = expected_blocks_to_walk.begin();
	//
	//    scendere::ledger_walker ledger_walker{ node->ledger };
	//    ledger_walker.walk_backward (account_info.block_count, [&] (auto const & block) {
	//        if (expected_blocks_to_walk_itr == expected_blocks_to_walk.end())
	//        {
	//            EXPECT_TRUE(false);
	//            return false;
	//        }
	//
	//        EXPECT_EQ((*expected_blocks_to_walk_itr++)->hash(), block->hash());
	//        return true;
	//    });
	//
	//    EXPECT_EQ(expected_blocks_to_walk_itr, expected_blocks_to_walk.end());
}

// Test disabled because it's failing intermittently.
// PR in which it got disabled: https://github.com/scenderecurrency/scendere-node/pull/3602
// Issue for investigating it: https://github.com/scenderecurrency/scendere-node/issues/3603
TEST (ledger_walker, DISABLED_ladder_geometry)
{
	scendere::system system{};

	scendere::node_config node_config (scendere::get_available_port (), system.logging);
	node_config.enable_voting = true;
	node_config.receive_minimum = 1;

	auto const node = system.add_node (node_config);
	std::array<scendere::keypair, 3> keys{};

	system.wallet (0)->insert_adhoc (scendere::dev::genesis_key.prv);
	for (auto itr = 0; itr != keys.size (); ++itr)
	{
		system.wallet (0)->insert_adhoc (keys[itr].prv);
		auto const block = system.wallet (0)->send_action (scendere::dev::genesis_key.pub, keys[itr].pub, 1000);
		ASSERT_TRUE (block);
		ASSERT_TIMELY (3s, 1 + (itr + 1) * 2 == node->ledger.cache.cemented_count);
	}

	std::vector<scendere::uint128_t> amounts_to_send (10);
	std::iota (amounts_to_send.begin (), amounts_to_send.end (), 1);

	scendere::account const * last_destination{};
	for (auto itr = 0; itr != amounts_to_send.size (); ++itr)
	{
		auto const source_index = itr % keys.size ();
		auto const destination_index = (source_index + 1) % keys.size ();
		last_destination = &keys[destination_index].pub;

		auto const send = system.wallet (0)->send_action (keys[source_index].pub, keys[destination_index].pub, amounts_to_send[itr]);
		ASSERT_TRUE (send);
		ASSERT_TIMELY (3s, 1 + keys.size () * 2 + (itr + 1) * 2 == node->ledger.cache.cemented_count);
	}

	ASSERT_TRUE (last_destination);
	scendere::account_info last_destination_info{};
	auto const last_destination_read_error = node->ledger.store.account.get (node->ledger.store.tx_begin_read (), *last_destination, last_destination_info);
	ASSERT_FALSE (last_destination_read_error);

	// This is how we expect chains to look like (for 3 accounts and 10 amounts to be sent)
	// k1: 1000     SEND     3     SEND     6     SEND     9     SEND
	// k2: 1000     1       SEND   4     SEND     7     SEND     10
	// k3: 1000     2       SEND   5     SEND     8     SEND

	std::vector<scendere::uint128_t> amounts_expected_backwards{ 10, 9, 8, 5, 4, 3, 1000, 1, 1000, 2, 1000, 6, 7 };
	auto amounts_expected_backwards_itr = amounts_expected_backwards.cbegin ();

	scendere::ledger_walker ledger_walker{ node->ledger };
	ledger_walker.walk_backward (last_destination_info.head,
	[&] (auto const & block) {
		if (block->sideband ().details.is_receive)
		{
			scendere::amount previous_balance{};
			if (!block->previous ().is_zero ())
			{
				auto const previous_block = node->ledger.store.block.get_no_sideband (node->ledger.store.tx_begin_read (), block->previous ());
				previous_balance = previous_block->balance ();
			}

			EXPECT_EQ (*amounts_expected_backwards_itr++, block->balance ().number () - previous_balance.number ());
		}
	});

	EXPECT_EQ (amounts_expected_backwards_itr, amounts_expected_backwards.cend ());

	auto amounts_expected_itr = amounts_expected_backwards.crbegin ();

	ledger_walker.walk (last_destination_info.head,
	[&] (auto const & block) {
		if (block->sideband ().details.is_receive)
		{
			scendere::amount previous_balance{};
			if (!block->previous ().is_zero ())
			{
				auto const previous_block = node->ledger.store.block.get_no_sideband (node->ledger.store.tx_begin_read (), block->previous ());
				previous_balance = previous_block->balance ();
			}

			EXPECT_EQ (*amounts_expected_itr++, block->balance ().number () - previous_balance.number ());
		}
	});

	EXPECT_EQ (amounts_expected_itr, amounts_expected_backwards.crend ());
}

#endif // _WIN32 -- TODO: keep this until diskhash builds fine on Windows
