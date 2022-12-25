#include <scendere/node/active_transactions.hpp>
#include <scendere/test_common/system.hpp>
#include <scendere/test_common/testutil.hpp>

#include <gtest/gtest.h>

using namespace std::chrono_literals;

namespace scendere
{
TEST (frontiers_confirmation, prioritize_frontiers)
{
	scendere::system system;
	// Prevent frontiers being confirmed as it will affect the priorization checking
	scendere::node_config node_config (scendere::get_available_port (), system.logging);
	node_config.frontiers_confirmation = scendere::frontiers_confirmation_mode::disabled;
	auto node = system.add_node (node_config);

	scendere::keypair key1;
	scendere::keypair key2;
	scendere::keypair key3;
	scendere::keypair key4;
	scendere::block_hash latest1 (node->latest (scendere::dev::genesis_key.pub));

	// Send different numbers of blocks all accounts
	scendere::send_block send1 (latest1, key1.pub, node->config.online_weight_minimum.number () + 10000, scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub, *system.work.generate (latest1));
	scendere::send_block send2 (send1.hash (), key1.pub, node->config.online_weight_minimum.number () + 8500, scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub, *system.work.generate (send1.hash ()));
	scendere::send_block send3 (send2.hash (), key1.pub, node->config.online_weight_minimum.number () + 8000, scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub, *system.work.generate (send2.hash ()));
	scendere::send_block send4 (send3.hash (), key2.pub, node->config.online_weight_minimum.number () + 7500, scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub, *system.work.generate (send3.hash ()));
	scendere::send_block send5 (send4.hash (), key3.pub, node->config.online_weight_minimum.number () + 6500, scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub, *system.work.generate (send4.hash ()));
	scendere::send_block send6 (send5.hash (), key4.pub, node->config.online_weight_minimum.number () + 6000, scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub, *system.work.generate (send5.hash ()));

	// Open all accounts and add other sends to get different uncemented counts (as well as some which are the same)
	scendere::open_block open1 (send1.hash (), scendere::dev::genesis->account (), key1.pub, key1.prv, key1.pub, *system.work.generate (key1.pub));
	scendere::send_block send7 (open1.hash (), scendere::dev::genesis_key.pub, 500, key1.prv, key1.pub, *system.work.generate (open1.hash ()));

	scendere::open_block open2 (send4.hash (), scendere::dev::genesis->account (), key2.pub, key2.prv, key2.pub, *system.work.generate (key2.pub));

	scendere::open_block open3 (send5.hash (), scendere::dev::genesis->account (), key3.pub, key3.prv, key3.pub, *system.work.generate (key3.pub));
	scendere::send_block send8 (open3.hash (), scendere::dev::genesis_key.pub, 500, key3.prv, key3.pub, *system.work.generate (open3.hash ()));
	scendere::send_block send9 (send8.hash (), scendere::dev::genesis_key.pub, 200, key3.prv, key3.pub, *system.work.generate (send8.hash ()));

	scendere::open_block open4 (send6.hash (), scendere::dev::genesis->account (), key4.pub, key4.prv, key4.pub, *system.work.generate (key4.pub));
	scendere::send_block send10 (open4.hash (), scendere::dev::genesis_key.pub, 500, key4.prv, key4.pub, *system.work.generate (open4.hash ()));
	scendere::send_block send11 (send10.hash (), scendere::dev::genesis_key.pub, 200, key4.prv, key4.pub, *system.work.generate (send10.hash ()));

	{
		auto transaction = node->store.tx_begin_write ();
		ASSERT_EQ (scendere::process_result::progress, node->ledger.process (transaction, send1).code);
		ASSERT_EQ (scendere::process_result::progress, node->ledger.process (transaction, send2).code);
		ASSERT_EQ (scendere::process_result::progress, node->ledger.process (transaction, send3).code);
		ASSERT_EQ (scendere::process_result::progress, node->ledger.process (transaction, send4).code);
		ASSERT_EQ (scendere::process_result::progress, node->ledger.process (transaction, send5).code);
		ASSERT_EQ (scendere::process_result::progress, node->ledger.process (transaction, send6).code);

		ASSERT_EQ (scendere::process_result::progress, node->ledger.process (transaction, open1).code);
		ASSERT_EQ (scendere::process_result::progress, node->ledger.process (transaction, send7).code);

		ASSERT_EQ (scendere::process_result::progress, node->ledger.process (transaction, open2).code);

		ASSERT_EQ (scendere::process_result::progress, node->ledger.process (transaction, open3).code);
		ASSERT_EQ (scendere::process_result::progress, node->ledger.process (transaction, send8).code);
		ASSERT_EQ (scendere::process_result::progress, node->ledger.process (transaction, send9).code);

		ASSERT_EQ (scendere::process_result::progress, node->ledger.process (transaction, open4).code);
		ASSERT_EQ (scendere::process_result::progress, node->ledger.process (transaction, send10).code);
		ASSERT_EQ (scendere::process_result::progress, node->ledger.process (transaction, send11).code);
	}

	auto transaction = node->store.tx_begin_read ();
	constexpr auto num_accounts = 5;
	auto priority_orders_match = [] (auto const & cementable_frontiers, auto const & desired_order) {
		return std::equal (desired_order.begin (), desired_order.end (), cementable_frontiers.template get<1> ().begin (), cementable_frontiers.template get<1> ().end (), [] (scendere::account const & account, scendere::cementable_account const & cementable_account) {
			return (account == cementable_account.account);
		});
	};
	{
		node->active.prioritize_frontiers_for_confirmation (transaction, std::chrono::seconds (1), std::chrono::seconds (1));
		ASSERT_EQ (node->active.priority_cementable_frontiers_size (), num_accounts);
		// Check the order of accounts is as expected (greatest number of uncemented blocks at the front). key3 and key4 have the same value, the order is unspecified so check both
		std::array<scendere::account, num_accounts> desired_order_1{ scendere::dev::genesis->account (), key3.pub, key4.pub, key1.pub, key2.pub };
		std::array<scendere::account, num_accounts> desired_order_2{ scendere::dev::genesis->account (), key4.pub, key3.pub, key1.pub, key2.pub };
		ASSERT_TRUE (priority_orders_match (node->active.priority_cementable_frontiers, desired_order_1) || priority_orders_match (node->active.priority_cementable_frontiers, desired_order_2));
	}

	{
		// Add some to the local node wallets and check ordering of both containers
		system.wallet (0)->insert_adhoc (scendere::dev::genesis_key.prv);
		system.wallet (0)->insert_adhoc (key1.prv);
		system.wallet (0)->insert_adhoc (key2.prv);
		node->active.prioritize_frontiers_for_confirmation (transaction, std::chrono::seconds (1), std::chrono::seconds (1));
		ASSERT_EQ (node->active.priority_cementable_frontiers_size (), num_accounts - 3);
		ASSERT_EQ (node->active.priority_wallet_cementable_frontiers_size (), num_accounts - 2);
		std::array<scendere::account, 3> local_desired_order{ scendere::dev::genesis->account (), key1.pub, key2.pub };
		ASSERT_TRUE (priority_orders_match (node->active.priority_wallet_cementable_frontiers, local_desired_order));
		std::array<scendere::account, 2> desired_order_1{ key3.pub, key4.pub };
		std::array<scendere::account, 2> desired_order_2{ key4.pub, key3.pub };
		ASSERT_TRUE (priority_orders_match (node->active.priority_cementable_frontiers, desired_order_1) || priority_orders_match (node->active.priority_cementable_frontiers, desired_order_2));
	}

	{
		// Add the remainder of accounts to node wallets and check size/ordering is correct
		system.wallet (0)->insert_adhoc (key3.prv);
		system.wallet (0)->insert_adhoc (key4.prv);
		node->active.prioritize_frontiers_for_confirmation (transaction, std::chrono::seconds (1), std::chrono::seconds (1));
		ASSERT_EQ (node->active.priority_cementable_frontiers_size (), 0);
		ASSERT_EQ (node->active.priority_wallet_cementable_frontiers_size (), num_accounts);
		std::array<scendere::account, num_accounts> desired_order_1{ scendere::dev::genesis->account (), key3.pub, key4.pub, key1.pub, key2.pub };
		std::array<scendere::account, num_accounts> desired_order_2{ scendere::dev::genesis->account (), key4.pub, key3.pub, key1.pub, key2.pub };
		ASSERT_TRUE (priority_orders_match (node->active.priority_wallet_cementable_frontiers, desired_order_1) || priority_orders_match (node->active.priority_wallet_cementable_frontiers, desired_order_2));
	}

	// Check that accounts which already exist have their order modified when the uncemented count changes.
	scendere::send_block send12 (send9.hash (), scendere::dev::genesis_key.pub, 100, key3.prv, key3.pub, *system.work.generate (send9.hash ()));
	scendere::send_block send13 (send12.hash (), scendere::dev::genesis_key.pub, 90, key3.prv, key3.pub, *system.work.generate (send12.hash ()));
	scendere::send_block send14 (send13.hash (), scendere::dev::genesis_key.pub, 80, key3.prv, key3.pub, *system.work.generate (send13.hash ()));
	scendere::send_block send15 (send14.hash (), scendere::dev::genesis_key.pub, 70, key3.prv, key3.pub, *system.work.generate (send14.hash ()));
	scendere::send_block send16 (send15.hash (), scendere::dev::genesis_key.pub, 60, key3.prv, key3.pub, *system.work.generate (send15.hash ()));
	scendere::send_block send17 (send16.hash (), scendere::dev::genesis_key.pub, 50, key3.prv, key3.pub, *system.work.generate (send16.hash ()));
	{
		auto transaction = node->store.tx_begin_write ();
		ASSERT_EQ (scendere::process_result::progress, node->ledger.process (transaction, send12).code);
		ASSERT_EQ (scendere::process_result::progress, node->ledger.process (transaction, send13).code);
		ASSERT_EQ (scendere::process_result::progress, node->ledger.process (transaction, send14).code);
		ASSERT_EQ (scendere::process_result::progress, node->ledger.process (transaction, send15).code);
		ASSERT_EQ (scendere::process_result::progress, node->ledger.process (transaction, send16).code);
		ASSERT_EQ (scendere::process_result::progress, node->ledger.process (transaction, send17).code);
	}
	transaction.refresh ();
	node->active.prioritize_frontiers_for_confirmation (transaction, std::chrono::seconds (1), std::chrono::seconds (1));
	ASSERT_TRUE (priority_orders_match (node->active.priority_wallet_cementable_frontiers, std::array<scendere::account, num_accounts>{ key3.pub, scendere::dev::genesis->account (), key4.pub, key1.pub, key2.pub }));
	uint64_t election_count = 0;
	node->active.confirm_prioritized_frontiers (transaction, 100, election_count);

	// Check that the active transactions roots contains the frontiers
	ASSERT_TIMELY (10s, node->active.size () == num_accounts);

	std::array<scendere::qualified_root, num_accounts> frontiers{ send17.qualified_root (), send6.qualified_root (), send7.qualified_root (), open2.qualified_root (), send11.qualified_root () };
	for (auto & frontier : frontiers)
	{
		ASSERT_TRUE (node->active.active (frontier));
	}
}

TEST (frontiers_confirmation, prioritize_frontiers_max_optimistic_elections)
{
	scendere::system system;
	// Prevent frontiers being confirmed as it will affect the priorization checking
	scendere::node_config node_config (scendere::get_available_port (), system.logging);
	node_config.frontiers_confirmation = scendere::frontiers_confirmation_mode::disabled;
	auto node = system.add_node (node_config);

	node->ledger.cache.cemented_count = node->ledger.bootstrap_weight_max_blocks - 1;
	auto max_optimistic_election_count_under_hardcoded_weight = node->active.max_optimistic ();
	node->ledger.cache.cemented_count = node->ledger.bootstrap_weight_max_blocks;
	auto max_optimistic_election_count = node->active.max_optimistic ();
	ASSERT_GT (max_optimistic_election_count_under_hardcoded_weight, max_optimistic_election_count);

	for (auto i = 0; i < max_optimistic_election_count * 2; ++i)
	{
		auto transaction = node->store.tx_begin_write ();
		auto latest = node->latest (scendere::dev::genesis->account ());
		scendere::keypair key;
		scendere::send_block send (latest, key.pub, node->config.online_weight_minimum.number () + 10000, scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub, *system.work.generate (latest));
		ASSERT_EQ (scendere::process_result::progress, node->ledger.process (transaction, send).code);
		scendere::open_block open (send.hash (), scendere::dev::genesis->account (), key.pub, key.prv, key.pub, *system.work.generate (key.pub));
		ASSERT_EQ (scendere::process_result::progress, node->ledger.process (transaction, open).code);
	}

	{
		scendere::unique_lock<scendere::mutex> lk (node->active.mutex);
		node->active.frontiers_confirmation (lk);
	}

	ASSERT_EQ (max_optimistic_election_count, node->active.roots.size ());

	scendere::account next_frontier_account{ 2 };
	node->active.next_frontier_account = next_frontier_account;

	// Call frontiers confirmation again and confirm that next_frontier_account hasn't changed
	{
		scendere::unique_lock<scendere::mutex> lk (node->active.mutex);
		node->active.frontiers_confirmation (lk);
	}

	ASSERT_EQ (max_optimistic_election_count, node->active.roots.size ());
	ASSERT_EQ (next_frontier_account, node->active.next_frontier_account);
}

TEST (frontiers_confirmation, expired_optimistic_elections_removal)
{
	scendere::system system;
	scendere::node_config node_config (scendere::get_available_port (), system.logging);
	node_config.frontiers_confirmation = scendere::frontiers_confirmation_mode::disabled;
	auto node = system.add_node (node_config);

	// This should be removed on the next prioritization call
	node->active.expired_optimistic_election_infos.emplace (std::chrono::steady_clock::now () - (node->active.expired_optimistic_election_info_cutoff + 1min), scendere::account (1));
	ASSERT_EQ (1, node->active.expired_optimistic_election_infos.size ());
	node->active.prioritize_frontiers_for_confirmation (node->store.tx_begin_read (), 0s, 0s);
	ASSERT_EQ (0, node->active.expired_optimistic_election_infos.size ());

	// This should not be removed on the next prioritization call
	node->active.expired_optimistic_election_infos.emplace (std::chrono::steady_clock::now () - (node->active.expired_optimistic_election_info_cutoff - 1min), scendere::account (1));
	ASSERT_EQ (1, node->active.expired_optimistic_election_infos.size ());
	node->active.prioritize_frontiers_for_confirmation (node->store.tx_begin_read (), 0s, 0s);
	ASSERT_EQ (1, node->active.expired_optimistic_election_infos.size ());
}
}

TEST (frontiers_confirmation, mode)
{
	scendere::keypair key;
	scendere::node_flags node_flags;
	// Always mode
	{
		scendere::system system;
		scendere::node_config node_config (scendere::get_available_port (), system.logging);
		node_config.frontiers_confirmation = scendere::frontiers_confirmation_mode::always;
		auto node = system.add_node (node_config, node_flags);
		scendere::state_block send (scendere::dev::genesis_key.pub, scendere::dev::genesis->hash (), scendere::dev::genesis_key.pub, scendere::dev::constants.genesis_amount - scendere::Gxrb_ratio, key.pub, scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub, *node->work_generate_blocking (scendere::dev::genesis->hash ()));
		{
			auto transaction = node->store.tx_begin_write ();
			ASSERT_EQ (scendere::process_result::progress, node->ledger.process (transaction, send).code);
		}
		ASSERT_TIMELY (5s, node->active.size () == 1);
	}
	// Auto mode
	{
		scendere::system system;
		scendere::node_config node_config (scendere::get_available_port (), system.logging);
		node_config.frontiers_confirmation = scendere::frontiers_confirmation_mode::automatic;
		auto node = system.add_node (node_config, node_flags);
		scendere::state_block send (scendere::dev::genesis_key.pub, scendere::dev::genesis->hash (), scendere::dev::genesis_key.pub, scendere::dev::constants.genesis_amount - scendere::Gxrb_ratio, key.pub, scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub, *node->work_generate_blocking (scendere::dev::genesis->hash ()));
		{
			auto transaction = node->store.tx_begin_write ();
			ASSERT_EQ (scendere::process_result::progress, node->ledger.process (transaction, send).code);
		}
		ASSERT_TIMELY (5s, node->active.size () == 1);
	}
	// Disabled mode
	{
		scendere::system system;
		scendere::node_config node_config (scendere::get_available_port (), system.logging);
		node_config.frontiers_confirmation = scendere::frontiers_confirmation_mode::disabled;
		auto node = system.add_node (node_config, node_flags);
		scendere::state_block send (scendere::dev::genesis_key.pub, scendere::dev::genesis->hash (), scendere::dev::genesis_key.pub, scendere::dev::constants.genesis_amount - scendere::Gxrb_ratio, key.pub, scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub, *node->work_generate_blocking (scendere::dev::genesis->hash ()));
		{
			auto transaction = node->store.tx_begin_write ();
			ASSERT_EQ (scendere::process_result::progress, node->ledger.process (transaction, send).code);
		}
		system.wallet (0)->insert_adhoc (scendere::dev::genesis_key.prv);
		std::this_thread::sleep_for (std::chrono::seconds (1));
		ASSERT_EQ (0, node->active.size ());
	}
}
