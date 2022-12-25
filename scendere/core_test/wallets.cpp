#include <scendere/secure/versioning.hpp>
#include <scendere/test_common/system.hpp>
#include <scendere/test_common/testutil.hpp>

#include <gtest/gtest.h>

using namespace std::chrono_literals;

TEST (wallets, open_create)
{
	scendere::system system (1);
	bool error (false);
	scendere::wallets wallets (error, *system.nodes[0]);
	ASSERT_FALSE (error);
	ASSERT_EQ (1, wallets.items.size ()); // it starts out with a default wallet
	auto id = scendere::random_wallet_id ();
	ASSERT_EQ (nullptr, wallets.open (id));
	auto wallet (wallets.create (id));
	ASSERT_NE (nullptr, wallet);
	ASSERT_EQ (wallet, wallets.open (id));
}

TEST (wallets, open_existing)
{
	scendere::system system (1);
	auto id (scendere::random_wallet_id ());
	{
		bool error (false);
		scendere::wallets wallets (error, *system.nodes[0]);
		ASSERT_FALSE (error);
		ASSERT_EQ (1, wallets.items.size ());
		auto wallet (wallets.create (id));
		ASSERT_NE (nullptr, wallet);
		ASSERT_EQ (wallet, wallets.open (id));
		scendere::raw_key password;
		password.clear ();
		system.deadline_set (10s);
		while (password == 0)
		{
			ASSERT_NO_ERROR (system.poll ());
			wallet->store.password.value (password);
		}
	}
	{
		bool error (false);
		scendere::wallets wallets (error, *system.nodes[0]);
		ASSERT_FALSE (error);
		ASSERT_EQ (2, wallets.items.size ());
		ASSERT_NE (nullptr, wallets.open (id));
	}
}

TEST (wallets, remove)
{
	scendere::system system (1);
	scendere::wallet_id one (1);
	{
		bool error (false);
		scendere::wallets wallets (error, *system.nodes[0]);
		ASSERT_FALSE (error);
		ASSERT_EQ (1, wallets.items.size ());
		auto wallet (wallets.create (one));
		ASSERT_NE (nullptr, wallet);
		ASSERT_EQ (2, wallets.items.size ());
		wallets.destroy (one);
		ASSERT_EQ (1, wallets.items.size ());
	}
	{
		bool error (false);
		scendere::wallets wallets (error, *system.nodes[0]);
		ASSERT_FALSE (error);
		ASSERT_EQ (1, wallets.items.size ());
	}
}

TEST (wallets, reload)
{
	scendere::system system (1);
	auto & node1 (*system.nodes[0]);
	scendere::wallet_id one (1);
	bool error (false);
	ASSERT_FALSE (error);
	ASSERT_EQ (1, node1.wallets.items.size ());
	{
		scendere::lock_guard<scendere::mutex> lock_wallet (node1.wallets.mutex);
		scendere::inactive_node node (node1.application_path, scendere::inactive_node_flag_defaults ());
		auto wallet (node.node->wallets.create (one));
		ASSERT_NE (wallet, nullptr);
	}
	ASSERT_TIMELY (5s, node1.wallets.open (one) != nullptr);
	ASSERT_EQ (2, node1.wallets.items.size ());
}

TEST (wallets, vote_minimum)
{
	scendere::system system (1);
	auto & node1 (*system.nodes[0]);
	scendere::keypair key1;
	scendere::keypair key2;
	scendere::state_block send1 (scendere::dev::genesis_key.pub, scendere::dev::genesis->hash (), scendere::dev::genesis_key.pub, std::numeric_limits<scendere::uint128_t>::max () - node1.config.vote_minimum.number (), key1.pub, scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub, *system.work.generate (scendere::dev::genesis->hash ()));
	ASSERT_EQ (scendere::process_result::progress, node1.process (send1).code);
	scendere::state_block open1 (key1.pub, 0, key1.pub, node1.config.vote_minimum.number (), send1.hash (), key1.prv, key1.pub, *system.work.generate (key1.pub));
	ASSERT_EQ (scendere::process_result::progress, node1.process (open1).code);
	// send2 with amount vote_minimum - 1 (not voting representative)
	scendere::state_block send2 (scendere::dev::genesis_key.pub, send1.hash (), scendere::dev::genesis_key.pub, std::numeric_limits<scendere::uint128_t>::max () - 2 * node1.config.vote_minimum.number () + 1, key2.pub, scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub, *system.work.generate (send1.hash ()));
	ASSERT_EQ (scendere::process_result::progress, node1.process (send2).code);
	scendere::state_block open2 (key2.pub, 0, key2.pub, node1.config.vote_minimum.number () - 1, send2.hash (), key2.prv, key2.pub, *system.work.generate (key2.pub));
	ASSERT_EQ (scendere::process_result::progress, node1.process (open2).code);
	auto wallet (node1.wallets.items.begin ()->second);
	scendere::unique_lock<scendere::mutex> representatives_lk (wallet->representatives_mutex);
	ASSERT_EQ (0, wallet->representatives.size ());
	representatives_lk.unlock ();
	wallet->insert_adhoc (scendere::dev::genesis_key.prv);
	wallet->insert_adhoc (key1.prv);
	wallet->insert_adhoc (key2.prv);
	node1.wallets.compute_reps ();
	representatives_lk.lock ();
	ASSERT_EQ (2, wallet->representatives.size ());
}

TEST (wallets, exists)
{
	scendere::system system (1);
	auto & node (*system.nodes[0]);
	scendere::keypair key1;
	scendere::keypair key2;
	{
		auto transaction (node.wallets.tx_begin_read ());
		ASSERT_FALSE (node.wallets.exists (transaction, key1.pub));
		ASSERT_FALSE (node.wallets.exists (transaction, key2.pub));
	}
	system.wallet (0)->insert_adhoc (key1.prv);
	{
		auto transaction (node.wallets.tx_begin_read ());
		ASSERT_TRUE (node.wallets.exists (transaction, key1.pub));
		ASSERT_FALSE (node.wallets.exists (transaction, key2.pub));
	}
	system.wallet (0)->insert_adhoc (key2.prv);
	{
		auto transaction (node.wallets.tx_begin_read ());
		ASSERT_TRUE (node.wallets.exists (transaction, key1.pub));
		ASSERT_TRUE (node.wallets.exists (transaction, key2.pub));
	}
}

TEST (wallets, search_receivable)
{
	for (auto search_all : { false, true })
	{
		scendere::system system;
		scendere::node_config config (scendere::get_available_port (), system.logging);
		config.enable_voting = false;
		config.frontiers_confirmation = scendere::frontiers_confirmation_mode::disabled;
		scendere::node_flags flags;
		flags.disable_search_pending = true;
		auto & node (*system.add_node (config, flags));

		scendere::unique_lock<scendere::mutex> lk (node.wallets.mutex);
		auto wallets = node.wallets.get_wallets ();
		lk.unlock ();
		ASSERT_EQ (1, wallets.size ());
		auto wallet_id = wallets.begin ()->first;
		auto wallet = wallets.begin ()->second;

		wallet->insert_adhoc (scendere::dev::genesis_key.prv);
		scendere::block_builder builder;
		auto send = builder.state ()
					.account (scendere::dev::genesis->account ())
					.previous (scendere::dev::genesis->hash ())
					.representative (scendere::dev::genesis->account ())
					.balance (scendere::dev::constants.genesis_amount - node.config.receive_minimum.number ())
					.link (scendere::dev::genesis->account ())
					.sign (scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub)
					.work (*system.work.generate (scendere::dev::genesis->hash ()))
					.build ();
		ASSERT_EQ (scendere::process_result::progress, node.process (*send).code);

		// Pending search should start an election
		ASSERT_TRUE (node.active.empty ());
		if (search_all)
		{
			node.wallets.search_receivable_all ();
		}
		else
		{
			node.wallets.search_receivable (wallet_id);
		}
		auto election = node.active.election (send->qualified_root ());
		ASSERT_NE (nullptr, election);

		// Erase the key so the confirmation does not trigger an automatic receive
		wallet->store.erase (node.wallets.tx_begin_write (), scendere::dev::genesis->account ());

		// Now confirm the election
		election->force_confirm ();

		ASSERT_TIMELY (5s, node.block_confirmed (send->hash ()) && node.active.empty ());

		// Re-insert the key
		wallet->insert_adhoc (scendere::dev::genesis_key.prv);

		// Pending search should create the receive block
		ASSERT_EQ (2, node.ledger.cache.block_count);
		if (search_all)
		{
			node.wallets.search_receivable_all ();
		}
		else
		{
			node.wallets.search_receivable (wallet_id);
		}
		ASSERT_TIMELY (3s, node.balance (scendere::dev::genesis->account ()) == scendere::dev::constants.genesis_amount);
		auto receive_hash = node.ledger.latest (node.store.tx_begin_read (), scendere::dev::genesis->account ());
		auto receive = node.block (receive_hash);
		ASSERT_NE (nullptr, receive);
		ASSERT_EQ (receive->sideband ().height, 3);
		ASSERT_EQ (send->hash (), receive->link ().as_block_hash ());
	}
}
