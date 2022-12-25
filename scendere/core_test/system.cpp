#include <scendere/test_common/network.hpp>
#include <scendere/test_common/system.hpp>
#include <scendere/test_common/testutil.hpp>

#include <gtest/gtest.h>

using namespace std::chrono_literals;

TEST (system, work_generate_limited)
{
	scendere::system system;
	scendere::block_hash key (1);
	auto min = scendere::dev::network_params.work.entry;
	auto max = scendere::dev::network_params.work.base;
	for (int i = 0; i < 5; ++i)
	{
		auto work = system.work_generate_limited (key, min, max);
		auto difficulty = scendere::dev::network_params.work.difficulty (scendere::work_version::work_1, key, work);
		ASSERT_GE (difficulty, min);
		ASSERT_LT (difficulty, max);
	}
}

// All nodes in the system should agree on the genesis balance
TEST (system, system_genesis)
{
	scendere::system system (2);
	for (auto & i : system.nodes)
	{
		auto transaction (i->store.tx_begin_read ());
		ASSERT_EQ (scendere::dev::constants.genesis_amount, i->ledger.account_balance (transaction, scendere::dev::genesis->account ()));
	}
}

TEST (system, DISABLED_generate_send_existing)
{
	scendere::system system (1);
	auto & node1 (*system.nodes[0]);
	scendere::thread_runner runner (system.io_ctx, node1.config.io_threads);
	system.wallet (0)->insert_adhoc (scendere::dev::genesis_key.prv);
	scendere::keypair stake_preserver;
	auto send_block (system.wallet (0)->send_action (scendere::dev::genesis->account (), stake_preserver.pub, scendere::dev::constants.genesis_amount / 3 * 2, true));
	scendere::account_info info1;
	{
		auto transaction (node1.store.tx_begin_read ());
		ASSERT_FALSE (node1.store.account.get (transaction, scendere::dev::genesis_key.pub, info1));
	}
	std::vector<scendere::account> accounts;
	accounts.push_back (scendere::dev::genesis_key.pub);
	system.generate_send_existing (node1, accounts);
	// Have stake_preserver receive funds after generate_send_existing so it isn't chosen as the destination
	{
		auto transaction (node1.store.tx_begin_write ());
		auto open_block (std::make_shared<scendere::open_block> (send_block->hash (), scendere::dev::genesis->account (), stake_preserver.pub, stake_preserver.prv, stake_preserver.pub, 0));
		node1.work_generate_blocking (*open_block);
		ASSERT_EQ (scendere::process_result::progress, node1.ledger.process (transaction, *open_block).code);
	}
	ASSERT_GT (node1.balance (stake_preserver.pub), node1.balance (scendere::dev::genesis->account ()));
	scendere::account_info info2;
	{
		auto transaction (node1.store.tx_begin_read ());
		ASSERT_FALSE (node1.store.account.get (transaction, scendere::dev::genesis_key.pub, info2));
	}
	ASSERT_NE (info1.head, info2.head);
	system.deadline_set (15s);
	while (info2.block_count < info1.block_count + 2)
	{
		ASSERT_NO_ERROR (system.poll ());
		auto transaction (node1.store.tx_begin_read ());
		ASSERT_FALSE (node1.store.account.get (transaction, scendere::dev::genesis_key.pub, info2));
	}
	ASSERT_EQ (info1.block_count + 2, info2.block_count);
	ASSERT_EQ (info2.balance, scendere::dev::constants.genesis_amount / 3);
	{
		auto transaction (node1.store.tx_begin_read ());
		ASSERT_NE (node1.ledger.amount (transaction, info2.head), 0);
	}
	system.stop ();
	runner.join ();
}

TEST (system, DISABLED_generate_send_new)
{
	scendere::system system (1);
	auto & node1 (*system.nodes[0]);
	scendere::thread_runner runner (system.io_ctx, node1.config.io_threads);
	system.wallet (0)->insert_adhoc (scendere::dev::genesis_key.prv);
	{
		auto transaction (node1.store.tx_begin_read ());
		auto iterator1 (node1.store.account.begin (transaction));
		ASSERT_NE (node1.store.account.end (), iterator1);
		++iterator1;
		ASSERT_EQ (node1.store.account.end (), iterator1);
	}
	scendere::keypair stake_preserver;
	auto send_block (system.wallet (0)->send_action (scendere::dev::genesis->account (), stake_preserver.pub, scendere::dev::constants.genesis_amount / 3 * 2, true));
	{
		auto transaction (node1.store.tx_begin_write ());
		auto open_block (std::make_shared<scendere::open_block> (send_block->hash (), scendere::dev::genesis->account (), stake_preserver.pub, stake_preserver.prv, stake_preserver.pub, 0));
		node1.work_generate_blocking (*open_block);
		ASSERT_EQ (scendere::process_result::progress, node1.ledger.process (transaction, *open_block).code);
	}
	ASSERT_GT (node1.balance (stake_preserver.pub), node1.balance (scendere::dev::genesis->account ()));
	std::vector<scendere::account> accounts;
	accounts.push_back (scendere::dev::genesis_key.pub);
	// This indirectly waits for online weight to stabilize, required to prevent intermittent failures
	ASSERT_TIMELY (5s, node1.wallets.reps ().voting > 0);
	system.generate_send_new (node1, accounts);
	scendere::account new_account{};
	{
		auto transaction (node1.wallets.tx_begin_read ());
		auto iterator2 (system.wallet (0)->store.begin (transaction));
		if (iterator2->first != scendere::dev::genesis_key.pub)
		{
			new_account = iterator2->first;
		}
		++iterator2;
		ASSERT_NE (system.wallet (0)->store.end (), iterator2);
		if (iterator2->first != scendere::dev::genesis_key.pub)
		{
			new_account = iterator2->first;
		}
		++iterator2;
		ASSERT_EQ (system.wallet (0)->store.end (), iterator2);
		ASSERT_FALSE (new_account.is_zero ());
	}
	ASSERT_TIMELY (10s, node1.balance (new_account) != 0);
	system.stop ();
	runner.join ();
}

TEST (system, rep_initialize_one)
{
	scendere::system system;
	scendere::keypair key;
	system.ledger_initialization_set ({ key });
	auto node = system.add_node ();
	ASSERT_EQ (scendere::dev::constants.genesis_amount, node->balance (key.pub));
}

TEST (system, rep_initialize_two)
{
	scendere::system system;
	scendere::keypair key0;
	scendere::keypair key1;
	system.ledger_initialization_set ({ key0, key1 });
	auto node = system.add_node ();
	ASSERT_EQ (scendere::dev::constants.genesis_amount / 2, node->balance (key0.pub));
	ASSERT_EQ (scendere::dev::constants.genesis_amount / 2, node->balance (key1.pub));
}

TEST (system, rep_initialize_one_reserve)
{
	scendere::system system;
	scendere::keypair key;
	system.ledger_initialization_set ({ key }, scendere::Gxrb_ratio);
	auto node = system.add_node ();
	ASSERT_EQ (scendere::dev::constants.genesis_amount - scendere::Gxrb_ratio, node->balance (key.pub));
	ASSERT_EQ (scendere::Gxrb_ratio, node->balance (scendere::dev::genesis_key.pub));
}

TEST (system, rep_initialize_two_reserve)
{
	scendere::system system;
	scendere::keypair key0;
	scendere::keypair key1;
	system.ledger_initialization_set ({ key0, key1 }, scendere::Gxrb_ratio);
	auto node = system.add_node ();
	ASSERT_EQ ((scendere::dev::constants.genesis_amount - scendere::Gxrb_ratio) / 2, node->balance (key0.pub));
	ASSERT_EQ ((scendere::dev::constants.genesis_amount - scendere::Gxrb_ratio) / 2, node->balance (key1.pub));
}

TEST (system, rep_initialize_many)
{
	scendere::system system;
	scendere::keypair key0;
	scendere::keypair key1;
	system.ledger_initialization_set ({ key0, key1 }, scendere::Gxrb_ratio);
	auto node0 = system.add_node ();
	ASSERT_EQ ((scendere::dev::constants.genesis_amount - scendere::Gxrb_ratio) / 2, node0->balance (key0.pub));
	ASSERT_EQ ((scendere::dev::constants.genesis_amount - scendere::Gxrb_ratio) / 2, node0->balance (key1.pub));
	auto node1 = system.add_node ();
	ASSERT_EQ ((scendere::dev::constants.genesis_amount - scendere::Gxrb_ratio) / 2, node1->balance (key0.pub));
	ASSERT_EQ ((scendere::dev::constants.genesis_amount - scendere::Gxrb_ratio) / 2, node1->balance (key1.pub));
}
