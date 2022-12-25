#include <scendere/node/election.hpp>
#include <scendere/node/transport/udp.hpp>
#include <scendere/test_common/network.hpp>
#include <scendere/test_common/system.hpp>
#include <scendere/test_common/testutil.hpp>

#include <gtest/gtest.h>

#include <boost/filesystem.hpp>
#include <boost/make_shared.hpp>
#include <boost/variant.hpp>

#include <numeric>

using namespace std::chrono_literals;

TEST (node, null_account)
{
	auto const & null_account = scendere::account::null ();
	ASSERT_TRUE (null_account == nullptr);
	ASSERT_FALSE (null_account != nullptr);

	scendere::account default_account{};
	ASSERT_FALSE (default_account == nullptr);
	ASSERT_TRUE (default_account != nullptr);
}

TEST (node, stop)
{
	scendere::system system (1);
	ASSERT_NE (system.nodes[0]->wallets.items.end (), system.nodes[0]->wallets.items.begin ());
	system.nodes[0]->stop ();
	system.io_ctx.run ();
	ASSERT_TRUE (true);
}

TEST (node, work_generate)
{
	scendere::system system (1);
	auto & node (*system.nodes[0]);
	scendere::block_hash root{ 1 };
	scendere::work_version version{ scendere::work_version::work_1 };
	{
		auto difficulty = scendere::difficulty::from_multiplier (1.5, node.network_params.work.base);
		auto work = node.work_generate_blocking (version, root, difficulty);
		ASSERT_TRUE (work.is_initialized ());
		ASSERT_TRUE (scendere::dev::network_params.work.difficulty (version, root, *work) >= difficulty);
	}
	{
		auto difficulty = scendere::difficulty::from_multiplier (0.5, node.network_params.work.base);
		boost::optional<uint64_t> work;
		do
		{
			work = node.work_generate_blocking (version, root, difficulty);
		} while (scendere::dev::network_params.work.difficulty (version, root, *work) >= node.network_params.work.base);
		ASSERT_TRUE (work.is_initialized ());
		ASSERT_TRUE (scendere::dev::network_params.work.difficulty (version, root, *work) >= difficulty);
		ASSERT_FALSE (scendere::dev::network_params.work.difficulty (version, root, *work) >= node.network_params.work.base);
	}
}

TEST (node, block_store_path_failure)
{
	auto service (boost::make_shared<boost::asio::io_context> ());
	auto path (scendere::unique_path ());
	scendere::logging logging;
	logging.init (path);
	scendere::work_pool pool{ scendere::dev::network_params.network, std::numeric_limits<unsigned>::max () };
	auto node (std::make_shared<scendere::node> (*service, scendere::get_available_port (), path, logging, pool));
	ASSERT_TRUE (node->wallets.items.empty ());
	node->stop ();
}
#if defined(__clang__) && defined(__linux__) && CI
// Disable test due to instability with clang and actions
TEST (node_DeathTest, DISABLED_readonly_block_store_not_exist)
#else
TEST (node_DeathTest, readonly_block_store_not_exist)
#endif
{
	// This is a read-only node with no ledger file
	if (scendere::rocksdb_config::using_rocksdb_in_tests ())
	{
		scendere::inactive_node node (scendere::unique_path (), scendere::inactive_node_flag_defaults ());
		ASSERT_TRUE (node.node->init_error ());
	}
	else
	{
		ASSERT_EXIT (scendere::inactive_node node (scendere::unique_path (), scendere::inactive_node_flag_defaults ()), ::testing::ExitedWithCode (1), "");
	}
}

TEST (node, password_fanout)
{
	boost::asio::io_context io_ctx;
	auto path (scendere::unique_path ());
	scendere::node_config config;
	config.peering_port = scendere::get_available_port ();
	config.logging.init (path);
	scendere::work_pool pool{ scendere::dev::network_params.network, std::numeric_limits<unsigned>::max () };
	config.password_fanout = 10;
	scendere::node node (io_ctx, path, config, pool);
	auto wallet (node.wallets.create (100));
	ASSERT_EQ (10, wallet->store.password.values.size ());
	node.stop ();
}

TEST (node, balance)
{
	scendere::system system (1);
	system.wallet (0)->insert_adhoc (scendere::dev::genesis_key.prv);
	auto transaction (system.nodes[0]->store.tx_begin_write ());
	ASSERT_EQ (std::numeric_limits<scendere::uint128_t>::max (), system.nodes[0]->ledger.account_balance (transaction, scendere::dev::genesis_key.pub));
}

TEST (node, representative)
{
	scendere::system system (1);
	auto block1 (system.nodes[0]->rep_block (scendere::dev::genesis_key.pub));
	{
		auto transaction (system.nodes[0]->store.tx_begin_read ());
		ASSERT_TRUE (system.nodes[0]->ledger.store.block.exists (transaction, block1));
	}
	scendere::keypair key;
	ASSERT_TRUE (system.nodes[0]->rep_block (key.pub).is_zero ());
}

TEST (node, send_unkeyed)
{
	scendere::system system (1);
	scendere::keypair key2;
	system.wallet (0)->insert_adhoc (scendere::dev::genesis_key.prv);
	system.wallet (0)->store.password.value_set (scendere::keypair ().prv);
	ASSERT_EQ (nullptr, system.wallet (0)->send_action (scendere::dev::genesis_key.pub, key2.pub, system.nodes[0]->config.receive_minimum.number ()));
}

TEST (node, send_self)
{
	scendere::system system (1);
	scendere::keypair key2;
	system.wallet (0)->insert_adhoc (scendere::dev::genesis_key.prv);
	system.wallet (0)->insert_adhoc (key2.prv);
	ASSERT_NE (nullptr, system.wallet (0)->send_action (scendere::dev::genesis_key.pub, key2.pub, system.nodes[0]->config.receive_minimum.number ()));
	ASSERT_TIMELY (10s, !system.nodes[0]->balance (key2.pub).is_zero ());
	ASSERT_EQ (std::numeric_limits<scendere::uint128_t>::max () - system.nodes[0]->config.receive_minimum.number (), system.nodes[0]->balance (scendere::dev::genesis_key.pub));
}

TEST (node, send_single)
{
	scendere::system system (2);
	scendere::keypair key2;
	system.wallet (0)->insert_adhoc (scendere::dev::genesis_key.prv);
	system.wallet (1)->insert_adhoc (key2.prv);
	ASSERT_NE (nullptr, system.wallet (0)->send_action (scendere::dev::genesis_key.pub, key2.pub, system.nodes[0]->config.receive_minimum.number ()));
	ASSERT_EQ (std::numeric_limits<scendere::uint128_t>::max () - system.nodes[0]->config.receive_minimum.number (), system.nodes[0]->balance (scendere::dev::genesis_key.pub));
	ASSERT_TRUE (system.nodes[0]->balance (key2.pub).is_zero ());
	ASSERT_TIMELY (10s, !system.nodes[0]->balance (key2.pub).is_zero ());
}

TEST (node, send_single_observing_peer)
{
	scendere::system system (3);
	scendere::keypair key2;
	system.wallet (0)->insert_adhoc (scendere::dev::genesis_key.prv);
	system.wallet (1)->insert_adhoc (key2.prv);
	ASSERT_NE (nullptr, system.wallet (0)->send_action (scendere::dev::genesis_key.pub, key2.pub, system.nodes[0]->config.receive_minimum.number ()));
	ASSERT_EQ (std::numeric_limits<scendere::uint128_t>::max () - system.nodes[0]->config.receive_minimum.number (), system.nodes[0]->balance (scendere::dev::genesis_key.pub));
	ASSERT_TRUE (system.nodes[0]->balance (key2.pub).is_zero ());
	ASSERT_TIMELY (10s, std::all_of (system.nodes.begin (), system.nodes.end (), [&] (std::shared_ptr<scendere::node> const & node_a) { return !node_a->balance (key2.pub).is_zero (); }));
}

TEST (node, send_single_many_peers)
{
	scendere::system system (10);
	scendere::keypair key2;
	system.wallet (0)->insert_adhoc (scendere::dev::genesis_key.prv);
	system.wallet (1)->insert_adhoc (key2.prv);
	ASSERT_NE (nullptr, system.wallet (0)->send_action (scendere::dev::genesis_key.pub, key2.pub, system.nodes[0]->config.receive_minimum.number ()));
	ASSERT_EQ (std::numeric_limits<scendere::uint128_t>::max () - system.nodes[0]->config.receive_minimum.number (), system.nodes[0]->balance (scendere::dev::genesis_key.pub));
	ASSERT_TRUE (system.nodes[0]->balance (key2.pub).is_zero ());
	ASSERT_TIMELY (3.5min, std::all_of (system.nodes.begin (), system.nodes.end (), [&] (std::shared_ptr<scendere::node> const & node_a) { return !node_a->balance (key2.pub).is_zero (); }));
	system.stop ();
	for (auto node : system.nodes)
	{
		ASSERT_TRUE (node->stopped);
	}
}

TEST (node, send_out_of_order)
{
	scendere::system system (2);
	auto & node1 (*system.nodes[0]);
	scendere::keypair key2;
	scendere::send_block_builder builder;
	auto send1 = builder.make_block ()
				 .previous (scendere::dev::genesis->hash ())
				 .destination (key2.pub)
				 .balance (std::numeric_limits<scendere::uint128_t>::max () - node1.config.receive_minimum.number ())
				 .sign (scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub)
				 .work (*system.work.generate (scendere::dev::genesis->hash ()))
				 .build_shared ();
	auto send2 = builder.make_block ()
				 .previous (send1->hash ())
				 .destination (key2.pub)
				 .balance (std::numeric_limits<scendere::uint128_t>::max () - 2 * node1.config.receive_minimum.number ())
				 .sign (scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub)
				 .work (*system.work.generate (send1->hash ()))
				 .build_shared ();
	auto send3 = builder.make_block ()
				 .previous (send2->hash ())
				 .destination (key2.pub)
				 .balance (std::numeric_limits<scendere::uint128_t>::max () - 3 * node1.config.receive_minimum.number ())
				 .sign (scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub)
				 .work (*system.work.generate (send2->hash ()))
				 .build_shared ();
	node1.process_active (send3);
	node1.process_active (send2);
	node1.process_active (send1);
	ASSERT_TIMELY (10s, std::all_of (system.nodes.begin (), system.nodes.end (), [&] (std::shared_ptr<scendere::node> const & node_a) { return node_a->balance (scendere::dev::genesis_key.pub) == scendere::dev::constants.genesis_amount - node1.config.receive_minimum.number () * 3; }));
}

TEST (node, quick_confirm)
{
	scendere::system system (1);
	auto & node1 (*system.nodes[0]);
	scendere::keypair key;
	scendere::block_hash previous (node1.latest (scendere::dev::genesis_key.pub));
	auto genesis_start_balance (node1.balance (scendere::dev::genesis_key.pub));
	system.wallet (0)->insert_adhoc (key.prv);
	system.wallet (0)->insert_adhoc (scendere::dev::genesis_key.prv);
	auto send = scendere::send_block_builder ()
				.previous (previous)
				.destination (key.pub)
				.balance (node1.online_reps.delta () + 1)
				.sign (scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub)
				.work (*system.work.generate (previous))
				.build_shared ();
	node1.process_active (send);
	ASSERT_TIMELY (10s, !node1.balance (key.pub).is_zero ());
	ASSERT_EQ (node1.balance (scendere::dev::genesis_key.pub), node1.online_reps.delta () + 1);
	ASSERT_EQ (node1.balance (key.pub), genesis_start_balance - (node1.online_reps.delta () + 1));
}

TEST (node, node_receive_quorum)
{
	scendere::system system (1);
	auto & node1 = *system.nodes[0];
	scendere::keypair key;
	scendere::block_hash previous (node1.latest (scendere::dev::genesis_key.pub));
	system.wallet (0)->insert_adhoc (key.prv);
	auto send = scendere::send_block_builder ()
				.previous (previous)
				.destination (key.pub)
				.balance (scendere::dev::constants.genesis_amount - scendere::Gxrb_ratio)
				.sign (scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub)
				.work (*system.work.generate (previous))
				.build_shared ();
	node1.process_active (send);
	ASSERT_TIMELY (10s, node1.ledger.block_or_pruned_exists (send->hash ()));
	ASSERT_TIMELY (10s, node1.active.election (scendere::qualified_root (previous, previous)) != nullptr);
	auto election (node1.active.election (scendere::qualified_root (previous, previous)));
	ASSERT_NE (nullptr, election);
	ASSERT_FALSE (election->confirmed ());
	ASSERT_EQ (1, election->votes ().size ());

	scendere::system system2;
	system2.add_node ();

	system2.wallet (0)->insert_adhoc (scendere::dev::genesis_key.prv);
	ASSERT_TRUE (node1.balance (key.pub).is_zero ());
	node1.network.tcp_channels.start_tcp (system2.nodes[0]->network.endpoint ());
	while (node1.balance (key.pub).is_zero ())
	{
		ASSERT_NO_ERROR (system.poll ());
		ASSERT_NO_ERROR (system2.poll ());
	}
}

TEST (node, auto_bootstrap)
{
	scendere::system system;
	scendere::node_config config (scendere::get_available_port (), system.logging);
	config.frontiers_confirmation = scendere::frontiers_confirmation_mode::disabled;
	scendere::node_flags node_flags;
	node_flags.disable_bootstrap_bulk_push_client = true;
	node_flags.disable_lazy_bootstrap = true;
	auto node0 = system.add_node (config, node_flags);
	scendere::keypair key2;
	system.wallet (0)->insert_adhoc (scendere::dev::genesis_key.prv);
	system.wallet (0)->insert_adhoc (key2.prv);
	auto send1 (system.wallet (0)->send_action (scendere::dev::genesis_key.pub, key2.pub, node0->config.receive_minimum.number ()));
	ASSERT_NE (nullptr, send1);
	ASSERT_TIMELY (10s, node0->balance (key2.pub) == node0->config.receive_minimum.number ());
	auto node1 (std::make_shared<scendere::node> (system.io_ctx, scendere::get_available_port (), scendere::unique_path (), system.logging, system.work, node_flags));
	ASSERT_FALSE (node1->init_error ());
	node1->start ();
	system.nodes.push_back (node1);
	ASSERT_NE (nullptr, scendere::establish_tcp (system, *node1, node0->network.endpoint ()));
	ASSERT_TIMELY (10s, node1->bootstrap_initiator.in_progress ());
	ASSERT_TIMELY (10s, node1->balance (key2.pub) == node0->config.receive_minimum.number ());
	ASSERT_TIMELY (10s, !node1->bootstrap_initiator.in_progress ());
	ASSERT_TRUE (node1->ledger.block_or_pruned_exists (send1->hash ()));
	// Wait block receive
	ASSERT_TIMELY (5s, node1->ledger.cache.block_count == 3);
	// Confirmation for all blocks
	ASSERT_TIMELY (5s, node1->ledger.cache.cemented_count == 3);

	node1->stop ();
}

TEST (node, auto_bootstrap_reverse)
{
	scendere::system system;
	scendere::node_config config (scendere::get_available_port (), system.logging);
	config.frontiers_confirmation = scendere::frontiers_confirmation_mode::disabled;
	scendere::node_flags node_flags;
	node_flags.disable_bootstrap_bulk_push_client = true;
	node_flags.disable_lazy_bootstrap = true;
	auto node0 = system.add_node (config, node_flags);
	scendere::keypair key2;
	system.wallet (0)->insert_adhoc (scendere::dev::genesis_key.prv);
	system.wallet (0)->insert_adhoc (key2.prv);
	auto node1 (std::make_shared<scendere::node> (system.io_ctx, scendere::get_available_port (), scendere::unique_path (), system.logging, system.work, node_flags));
	ASSERT_FALSE (node1->init_error ());
	ASSERT_NE (nullptr, system.wallet (0)->send_action (scendere::dev::genesis_key.pub, key2.pub, node0->config.receive_minimum.number ()));
	node1->start ();
	system.nodes.push_back (node1);
	ASSERT_NE (nullptr, scendere::establish_tcp (system, *node0, node1->network.endpoint ()));
	ASSERT_TIMELY (10s, node1->balance (key2.pub) == node0->config.receive_minimum.number ());
}

TEST (node, auto_bootstrap_age)
{
	scendere::system system;
	scendere::node_config config (scendere::get_available_port (), system.logging);
	config.frontiers_confirmation = scendere::frontiers_confirmation_mode::disabled;
	scendere::node_flags node_flags;
	node_flags.disable_bootstrap_bulk_push_client = true;
	node_flags.disable_lazy_bootstrap = true;
	node_flags.bootstrap_interval = 1;
	auto node0 = system.add_node (config, node_flags);
	auto node1 (std::make_shared<scendere::node> (system.io_ctx, scendere::get_available_port (), scendere::unique_path (), system.logging, system.work, node_flags));
	ASSERT_FALSE (node1->init_error ());
	node1->start ();
	system.nodes.push_back (node1);
	ASSERT_NE (nullptr, scendere::establish_tcp (system, *node1, node0->network.endpoint ()));
	ASSERT_TIMELY (10s, node1->bootstrap_initiator.in_progress ());
	// 4 bootstraps with frontiers age
	ASSERT_TIMELY (10s, node0->stats.count (scendere::stat::type::bootstrap, scendere::stat::detail::initiate_legacy_age, scendere::stat::dir::out) >= 3);
	// More attempts with frontiers age
	ASSERT_GE (node0->stats.count (scendere::stat::type::bootstrap, scendere::stat::detail::initiate_legacy_age, scendere::stat::dir::out), node0->stats.count (scendere::stat::type::bootstrap, scendere::stat::detail::initiate, scendere::stat::dir::out));

	node1->stop ();
}

TEST (node, receive_gap)
{
	scendere::system system (1);
	auto & node1 (*system.nodes[0]);
	ASSERT_EQ (0, node1.gap_cache.size ());
	auto block = scendere::send_block_builder ()
				 .previous (5)
				 .destination (1)
				 .balance (2)
				 .sign (scendere::keypair ().prv, 4)
				 .work (0)
				 .build_shared ();
	node1.work_generate_blocking (*block);
	scendere::publish message{ scendere::dev::network_params.network, block };
	node1.network.inbound (message, node1.network.udp_channels.create (node1.network.endpoint ()));
	node1.block_processor.flush ();
	ASSERT_EQ (1, node1.gap_cache.size ());
}

TEST (node, merge_peers)
{
	scendere::system system (1);
	std::array<scendere::endpoint, 8> endpoints;
	endpoints.fill (scendere::endpoint (boost::asio::ip::address_v6::loopback (), scendere::get_available_port ()));
	endpoints[0] = scendere::endpoint (boost::asio::ip::address_v6::loopback (), scendere::get_available_port ());
	system.nodes[0]->network.merge_peers (endpoints);
	ASSERT_EQ (0, system.nodes[0]->network.size ());
}

TEST (node, search_receivable)
{
	scendere::system system (1);
	auto node (system.nodes[0]);
	scendere::keypair key2;
	system.wallet (0)->insert_adhoc (scendere::dev::genesis_key.prv);
	ASSERT_NE (nullptr, system.wallet (0)->send_action (scendere::dev::genesis_key.pub, key2.pub, node->config.receive_minimum.number ()));
	system.wallet (0)->insert_adhoc (key2.prv);
	ASSERT_FALSE (system.wallet (0)->search_receivable (system.wallet (0)->wallets.tx_begin_read ()));
	ASSERT_TIMELY (10s, !node->balance (key2.pub).is_zero ());
}

TEST (node, search_receivable_same)
{
	scendere::system system (1);
	auto node (system.nodes[0]);
	scendere::keypair key2;
	system.wallet (0)->insert_adhoc (scendere::dev::genesis_key.prv);
	ASSERT_NE (nullptr, system.wallet (0)->send_action (scendere::dev::genesis_key.pub, key2.pub, node->config.receive_minimum.number ()));
	ASSERT_NE (nullptr, system.wallet (0)->send_action (scendere::dev::genesis_key.pub, key2.pub, node->config.receive_minimum.number ()));
	system.wallet (0)->insert_adhoc (key2.prv);
	ASSERT_FALSE (system.wallet (0)->search_receivable (system.wallet (0)->wallets.tx_begin_read ()));
	ASSERT_TIMELY (10s, node->balance (key2.pub) == 2 * node->config.receive_minimum.number ());
}

TEST (node, search_receivable_multiple)
{
	scendere::system system (1);
	auto node (system.nodes[0]);
	scendere::keypair key2;
	scendere::keypair key3;
	system.wallet (0)->insert_adhoc (scendere::dev::genesis_key.prv);
	system.wallet (0)->insert_adhoc (key3.prv);
	ASSERT_NE (nullptr, system.wallet (0)->send_action (scendere::dev::genesis_key.pub, key3.pub, node->config.receive_minimum.number ()));
	ASSERT_TIMELY (10s, !node->balance (key3.pub).is_zero ());
	ASSERT_NE (nullptr, system.wallet (0)->send_action (scendere::dev::genesis_key.pub, key2.pub, node->config.receive_minimum.number ()));
	ASSERT_NE (nullptr, system.wallet (0)->send_action (key3.pub, key2.pub, node->config.receive_minimum.number ()));
	system.wallet (0)->insert_adhoc (key2.prv);
	ASSERT_FALSE (system.wallet (0)->search_receivable (system.wallet (0)->wallets.tx_begin_read ()));
	ASSERT_TIMELY (10s, node->balance (key2.pub) == 2 * node->config.receive_minimum.number ());
}

TEST (node, search_receivable_confirmed)
{
	scendere::system system;
	scendere::node_config node_config (scendere::get_available_port (), system.logging);
	node_config.frontiers_confirmation = scendere::frontiers_confirmation_mode::disabled;
	auto node = system.add_node (node_config);
	scendere::keypair key2;
	system.wallet (0)->insert_adhoc (scendere::dev::genesis_key.prv);
	auto send1 (system.wallet (0)->send_action (scendere::dev::genesis_key.pub, key2.pub, node->config.receive_minimum.number ()));
	ASSERT_NE (nullptr, send1);
	auto send2 (system.wallet (0)->send_action (scendere::dev::genesis_key.pub, key2.pub, node->config.receive_minimum.number ()));
	ASSERT_NE (nullptr, send2);
	ASSERT_TIMELY (10s, node->active.empty ());
	bool confirmed (false);
	system.deadline_set (5s);
	while (!confirmed)
	{
		auto transaction (node->store.tx_begin_read ());
		confirmed = node->ledger.block_confirmed (transaction, send2->hash ());
		ASSERT_NO_ERROR (system.poll ());
	}
	{
		auto transaction (node->wallets.tx_begin_write ());
		system.wallet (0)->store.erase (transaction, scendere::dev::genesis_key.pub);
	}
	system.wallet (0)->insert_adhoc (key2.prv);
	ASSERT_FALSE (system.wallet (0)->search_receivable (system.wallet (0)->wallets.tx_begin_read ()));
	{
		scendere::lock_guard<scendere::mutex> guard (node->active.mutex);
		auto existing1 (node->active.blocks.find (send1->hash ()));
		ASSERT_EQ (node->active.blocks.end (), existing1);
		auto existing2 (node->active.blocks.find (send2->hash ()));
		ASSERT_EQ (node->active.blocks.end (), existing2);
	}
	ASSERT_TIMELY (10s, node->balance (key2.pub) == 2 * node->config.receive_minimum.number ());
}

TEST (node, search_receivable_pruned)
{
	scendere::system system;
	scendere::node_config node_config (scendere::get_available_port (), system.logging);
	node_config.frontiers_confirmation = scendere::frontiers_confirmation_mode::disabled;
	auto node1 = system.add_node (node_config);
	scendere::node_flags node_flags;
	node_flags.enable_pruning = true;
	scendere::node_config config (scendere::get_available_port (), system.logging);
	config.enable_voting = false; // Remove after allowing pruned voting
	auto node2 = system.add_node (config, node_flags);
	scendere::keypair key2;
	system.wallet (0)->insert_adhoc (scendere::dev::genesis_key.prv);
	auto send1 (system.wallet (0)->send_action (scendere::dev::genesis_key.pub, key2.pub, node2->config.receive_minimum.number ()));
	ASSERT_NE (nullptr, send1);
	auto send2 (system.wallet (0)->send_action (scendere::dev::genesis_key.pub, key2.pub, node2->config.receive_minimum.number ()));
	ASSERT_NE (nullptr, send2);

	// Confirmation
	ASSERT_TIMELY (10s, node1->active.empty () && node2->active.empty ());
	ASSERT_TIMELY (5s, node1->ledger.block_confirmed (node1->store.tx_begin_read (), send2->hash ()));
	ASSERT_TIMELY (5s, node2->ledger.cache.cemented_count == 3);
	system.wallet (0)->store.erase (node1->wallets.tx_begin_write (), scendere::dev::genesis_key.pub);

	// Pruning
	{
		auto transaction (node2->store.tx_begin_write ());
		ASSERT_EQ (1, node2->ledger.pruning_action (transaction, send1->hash (), 1));
	}
	ASSERT_EQ (1, node2->ledger.cache.pruned_count);
	ASSERT_TRUE (node2->ledger.block_or_pruned_exists (send1->hash ())); // true for pruned

	// Receive pruned block
	system.wallet (1)->insert_adhoc (key2.prv);
	ASSERT_FALSE (system.wallet (1)->search_receivable (system.wallet (1)->wallets.tx_begin_read ()));
	ASSERT_TIMELY (10s, node2->balance (key2.pub) == 2 * node2->config.receive_minimum.number ());
}

TEST (node, unlock_search)
{
	scendere::system system (1);
	auto node (system.nodes[0]);
	scendere::keypair key2;
	scendere::uint128_t balance (node->balance (scendere::dev::genesis_key.pub));
	{
		auto transaction (system.wallet (0)->wallets.tx_begin_write ());
		system.wallet (0)->store.rekey (transaction, "");
	}
	system.wallet (0)->insert_adhoc (scendere::dev::genesis_key.prv);
	ASSERT_NE (nullptr, system.wallet (0)->send_action (scendere::dev::genesis_key.pub, key2.pub, node->config.receive_minimum.number ()));
	ASSERT_TIMELY (10s, node->balance (scendere::dev::genesis_key.pub) != balance);
	ASSERT_TIMELY (10s, node->active.empty ());
	system.wallet (0)->insert_adhoc (key2.prv);
	{
		scendere::lock_guard<std::recursive_mutex> lock (system.wallet (0)->store.mutex);
		system.wallet (0)->store.password.value_set (scendere::keypair ().prv);
	}
	{
		auto transaction (system.wallet (0)->wallets.tx_begin_write ());
		ASSERT_FALSE (system.wallet (0)->enter_password (transaction, ""));
	}
	ASSERT_TIMELY (10s, !node->balance (key2.pub).is_zero ());
}

TEST (node, connect_after_junk)
{
	scendere::system system;
	scendere::node_flags node_flags;
	node_flags.disable_udp = false;
	auto node0 = system.add_node (node_flags);
	auto node1 (std::make_shared<scendere::node> (system.io_ctx, scendere::get_available_port (), scendere::unique_path (), system.logging, system.work, node_flags));
	std::vector<uint8_t> junk_buffer;
	junk_buffer.push_back (0);
	auto channel1 (std::make_shared<scendere::transport::channel_udp> (node1->network.udp_channels, node0->network.endpoint (), node1->network_params.network.protocol_version));
	channel1->send_buffer (scendere::shared_const_buffer (std::move (junk_buffer)), [] (boost::system::error_code const &, size_t) {});
	ASSERT_TIMELY (10s, node0->stats.count (scendere::stat::type::error) != 0);
	node1->start ();
	system.nodes.push_back (node1);
	auto channel2 (std::make_shared<scendere::transport::channel_udp> (node1->network.udp_channels, node0->network.endpoint (), node1->network_params.network.protocol_version));
	node1->network.send_keepalive (channel2);
	ASSERT_TIMELY (10s, !node1->network.empty ());
	node1->stop ();
}

TEST (node, working)
{
	auto path (scendere::working_path ());
	ASSERT_FALSE (path.empty ());
}

TEST (node, price)
{
	scendere::system system (1);
	auto price1 (system.nodes[0]->price (scendere::Gxrb_ratio, 1));
	ASSERT_EQ (scendere::node::price_max * 100.0, price1);
	auto price2 (system.nodes[0]->price (scendere::Gxrb_ratio * int (scendere::node::free_cutoff + 1), 1));
	ASSERT_EQ (0, price2);
	auto price3 (system.nodes[0]->price (scendere::Gxrb_ratio * int (scendere::node::free_cutoff + 2) / 2, 1));
	ASSERT_EQ (scendere::node::price_max * 100.0 / 2, price3);
	auto price4 (system.nodes[0]->price (scendere::Gxrb_ratio * int (scendere::node::free_cutoff) * 2, 1));
	ASSERT_EQ (0, price4);
}

TEST (node, confirm_locked)
{
	scendere::system system (1);
	system.wallet (0)->insert_adhoc (scendere::dev::genesis_key.prv);
	auto transaction (system.wallet (0)->wallets.tx_begin_read ());
	system.wallet (0)->enter_password (transaction, "1");
	auto block = scendere::send_block_builder ()
				 .previous (0)
				 .destination (0)
				 .balance (0)
				 .sign (scendere::keypair ().prv, 0)
				 .work (0)
				 .build_shared ();
	system.nodes[0]->network.flood_block (block);
}

TEST (node_config, random_rep)
{
	auto path (scendere::unique_path ());
	scendere::logging logging1;
	logging1.init (path);
	scendere::node_config config1 (100, logging1);
	auto rep (config1.random_representative ());
	ASSERT_NE (config1.preconfigured_representatives.end (), std::find (config1.preconfigured_representatives.begin (), config1.preconfigured_representatives.end (), rep));
}

TEST (node_flags, disable_tcp_realtime)
{
	scendere::system system;
	scendere::node_flags node_flags;
	node_flags.disable_udp = false;
	auto node1 = system.add_node (node_flags);
	node_flags.disable_tcp_realtime = true;
	auto node2 = system.add_node (node_flags);
	ASSERT_EQ (1, node1->network.size ());
	auto list1 (node1->network.list (2));
	ASSERT_EQ (node2->network.endpoint (), list1[0]->get_endpoint ());
	ASSERT_EQ (scendere::transport::transport_type::udp, list1[0]->get_type ());
	ASSERT_EQ (1, node2->network.size ());
	auto list2 (node2->network.list (2));
	ASSERT_EQ (node1->network.endpoint (), list2[0]->get_endpoint ());
	ASSERT_EQ (scendere::transport::transport_type::udp, list2[0]->get_type ());
}

TEST (node_flags, disable_tcp_realtime_and_bootstrap_listener)
{
	scendere::system system;
	scendere::node_flags node_flags;
	node_flags.disable_udp = false;
	auto node1 = system.add_node (node_flags);
	node_flags.disable_tcp_realtime = true;
	node_flags.disable_bootstrap_listener = true;
	auto node2 = system.add_node (node_flags);
	ASSERT_EQ (scendere::tcp_endpoint (boost::asio::ip::address_v6::loopback (), 0), node2->bootstrap.endpoint ());
	ASSERT_NE (scendere::endpoint (boost::asio::ip::address_v6::loopback (), 0), node2->network.endpoint ());
	ASSERT_EQ (1, node1->network.size ());
	auto list1 (node1->network.list (2));
	ASSERT_EQ (node2->network.endpoint (), list1[0]->get_endpoint ());
	ASSERT_EQ (scendere::transport::transport_type::udp, list1[0]->get_type ());
	ASSERT_EQ (1, node2->network.size ());
	auto list2 (node2->network.list (2));
	ASSERT_EQ (node1->network.endpoint (), list2[0]->get_endpoint ());
	ASSERT_EQ (scendere::transport::transport_type::udp, list2[0]->get_type ());
}

// UDP is disabled by default
TEST (node_flags, disable_udp)
{
	scendere::system system;
	scendere::node_flags node_flags;
	node_flags.disable_udp = false;
	auto node1 = system.add_node (node_flags);
	auto node2 (std::make_shared<scendere::node> (system.io_ctx, scendere::unique_path (), scendere::node_config (scendere::get_available_port (), system.logging), system.work));
	system.nodes.push_back (node2);
	node2->start ();
	ASSERT_EQ (scendere::endpoint (boost::asio::ip::address_v6::loopback (), 0), node2->network.udp_channels.get_local_endpoint ());
	ASSERT_NE (scendere::endpoint (boost::asio::ip::address_v6::loopback (), 0), node2->network.endpoint ());
	// Send UDP message
	auto channel (std::make_shared<scendere::transport::channel_udp> (node1->network.udp_channels, node2->network.endpoint (), node2->network_params.network.protocol_version));
	node1->network.send_keepalive (channel);
	std::this_thread::sleep_for (std::chrono::milliseconds (500));
	// Check empty network
	ASSERT_EQ (0, node1->network.size ());
	ASSERT_EQ (0, node2->network.size ());
	// Send TCP handshake
	node1->network.merge_peer (node2->network.endpoint ());
	ASSERT_TIMELY (5s, node1->bootstrap.realtime_count == 1 && node2->bootstrap.realtime_count == 1);
	ASSERT_EQ (1, node1->network.size ());
	auto list1 (node1->network.list (2));
	ASSERT_EQ (node2->network.endpoint (), list1[0]->get_endpoint ());
	ASSERT_EQ (scendere::transport::transport_type::tcp, list1[0]->get_type ());
	ASSERT_EQ (1, node2->network.size ());
	auto list2 (node2->network.list (2));
	ASSERT_EQ (node1->network.endpoint (), list2[0]->get_endpoint ());
	ASSERT_EQ (scendere::transport::transport_type::tcp, list2[0]->get_type ());
	node2->stop ();
}

TEST (node, fork_publish)
{
	std::weak_ptr<scendere::node> node0;
	{
		scendere::system system (1);
		node0 = system.nodes[0];
		auto & node1 (*system.nodes[0]);
		system.wallet (0)->insert_adhoc (scendere::dev::genesis_key.prv);
		scendere::keypair key1;
		scendere::send_block_builder builder;
		auto send1 = builder.make_block ()
					 .previous (scendere::dev::genesis->hash ())
					 .destination (key1.pub)
					 .balance (scendere::dev::constants.genesis_amount - 100)
					 .sign (scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub)
					 .work (0)
					 .build_shared ();
		node1.work_generate_blocking (*send1);
		scendere::keypair key2;
		auto send2 = builder.make_block ()
					 .previous (scendere::dev::genesis->hash ())
					 .destination (key2.pub)
					 .balance (scendere::dev::constants.genesis_amount - 100)
					 .sign (scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub)
					 .work (0)
					 .build_shared ();
		node1.work_generate_blocking (*send2);
		node1.process_active (send1);
		node1.block_processor.flush ();
		node1.scheduler.flush ();
		ASSERT_EQ (1, node1.active.size ());
		auto election (node1.active.election (send1->qualified_root ()));
		ASSERT_NE (nullptr, election);
		// Wait until the genesis rep activated & makes vote
		ASSERT_TIMELY (1s, election->votes ().size () == 2);
		node1.process_active (send2);
		node1.block_processor.flush ();
		auto votes1 (election->votes ());
		auto existing1 (votes1.find (scendere::dev::genesis_key.pub));
		ASSERT_NE (votes1.end (), existing1);
		ASSERT_EQ (send1->hash (), existing1->second.hash);
		auto winner (*election->tally ().begin ());
		ASSERT_EQ (*send1, *winner.second);
		ASSERT_EQ (scendere::dev::constants.genesis_amount - 100, winner.first);
	}
	ASSERT_TRUE (node0.expired ());
}

// Test disabled because it's failing intermittently.
// PR in which it got disabled: https://github.com/scenderecurrency/scendere-node/pull/3611
// Issue for investigating it: https://github.com/scenderecurrency/scendere-node/issues/3614
TEST (node, DISABLED_fork_publish_inactive)
{
	scendere::system system (1);
	scendere::keypair key1;
	scendere::keypair key2;
	scendere::send_block_builder builder;
	auto send1 = builder.make_block ()
				 .previous (scendere::dev::genesis->hash ())
				 .destination (key1.pub)
				 .balance (scendere::dev::constants.genesis_amount - 100)
				 .sign (scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub)
				 .work (*system.work.generate (scendere::dev::genesis->hash ()))
				 .build_shared ();
	auto send2 = builder.make_block ()
				 .previous (scendere::dev::genesis->hash ())
				 .destination (key2.pub)
				 .balance (scendere::dev::constants.genesis_amount - 100)
				 .sign (scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub)
				 .work (send1->block_work ())
				 .build_shared ();
	auto & node = *system.nodes[0];
	node.process_active (send1);
	ASSERT_TIMELY (3s, nullptr != node.block (send1->hash ()));
	ASSERT_EQ (scendere::process_result::fork, node.process_local (send2).code);
	auto election = node.active.election (send1->qualified_root ());
	ASSERT_NE (election, nullptr);
	auto blocks = election->blocks ();
	ASSERT_NE (blocks.end (), blocks.find (send1->hash ()));
	ASSERT_NE (blocks.end (), blocks.find (send2->hash ()));
	ASSERT_EQ (election->winner ()->hash (), send1->hash ());
	ASSERT_NE (election->winner ()->hash (), send2->hash ());
}

TEST (node, fork_keep)
{
	scendere::system system (2);
	auto & node1 (*system.nodes[0]);
	auto & node2 (*system.nodes[1]);
	ASSERT_EQ (1, node1.network.size ());
	scendere::keypair key1;
	scendere::keypair key2;
	scendere::send_block_builder builder;
	// send1 and send2 fork to different accounts
	auto send1 = builder.make_block ()
				 .previous (scendere::dev::genesis->hash ())
				 .destination (key1.pub)
				 .balance (scendere::dev::constants.genesis_amount - 100)
				 .sign (scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub)
				 .work (*system.work.generate (scendere::dev::genesis->hash ()))
				 .build_shared ();
	auto send2 = builder.make_block ()
				 .previous (scendere::dev::genesis->hash ())
				 .destination (key2.pub)
				 .balance (scendere::dev::constants.genesis_amount - 100)
				 .sign (scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub)
				 .work (*system.work.generate (scendere::dev::genesis->hash ()))
				 .build_shared ();
	node1.process_active (send1);
	node1.block_processor.flush ();
	node1.scheduler.flush ();
	node2.process_active (send1);
	node2.block_processor.flush ();
	node2.scheduler.flush ();
	ASSERT_EQ (1, node1.active.size ());
	ASSERT_EQ (1, node2.active.size ());
	system.wallet (0)->insert_adhoc (scendere::dev::genesis_key.prv);
	node1.process_active (send2);
	node1.block_processor.flush ();
	node2.process_active (send2);
	node2.block_processor.flush ();
	auto election1 (node2.active.election (scendere::qualified_root (scendere::dev::genesis->hash (), scendere::dev::genesis->hash ())));
	ASSERT_NE (nullptr, election1);
	ASSERT_EQ (1, election1->votes ().size ());
	ASSERT_TRUE (node1.ledger.block_or_pruned_exists (send1->hash ()));
	ASSERT_TRUE (node2.ledger.block_or_pruned_exists (send1->hash ()));
	// Wait until the genesis rep makes a vote
	ASSERT_TIMELY (1.5min, election1->votes ().size () != 1);
	auto transaction0 (node1.store.tx_begin_read ());
	auto transaction1 (node2.store.tx_begin_read ());
	// The vote should be in agreement with what we already have.
	auto winner (*election1->tally ().begin ());
	ASSERT_EQ (*send1, *winner.second);
	ASSERT_EQ (scendere::dev::constants.genesis_amount - 100, winner.first);
	ASSERT_TRUE (node1.store.block.exists (transaction0, send1->hash ()));
	ASSERT_TRUE (node2.store.block.exists (transaction1, send1->hash ()));
}

TEST (node, fork_flip)
{
	scendere::system system (2);
	auto & node1 (*system.nodes[0]);
	auto & node2 (*system.nodes[1]);
	ASSERT_EQ (1, node1.network.size ());
	scendere::keypair key1;
	scendere::send_block_builder builder;
	auto send1 = builder.make_block ()
				 .previous (scendere::dev::genesis->hash ())
				 .destination (key1.pub)
				 .balance (scendere::dev::constants.genesis_amount - 100)
				 .sign (scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub)
				 .work (*system.work.generate (scendere::dev::genesis->hash ()))
				 .build_shared ();
	scendere::publish publish1{ scendere::dev::network_params.network, send1 };
	scendere::keypair key2;
	auto send2 = builder.make_block ()
				 .previous (scendere::dev::genesis->hash ())
				 .destination (key2.pub)
				 .balance (scendere::dev::constants.genesis_amount - 100)
				 .sign (scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub)
				 .work (*system.work.generate (scendere::dev::genesis->hash ()))
				 .build_shared ();
	scendere::publish publish2{ scendere::dev::network_params.network, send2 };
	auto channel1 (node1.network.udp_channels.create (node1.network.endpoint ()));
	node1.network.inbound (publish1, channel1);
	node1.block_processor.flush ();
	node1.scheduler.flush ();
	auto channel2 (node2.network.udp_channels.create (node1.network.endpoint ()));
	node2.network.inbound (publish2, channel2);
	node2.block_processor.flush ();
	node2.scheduler.flush ();
	ASSERT_EQ (1, node1.active.size ());
	ASSERT_EQ (1, node2.active.size ());
	system.wallet (0)->insert_adhoc (scendere::dev::genesis_key.prv);
	node1.network.inbound (publish2, channel1);
	node1.block_processor.flush ();
	node2.network.inbound (publish1, channel2);
	node2.block_processor.flush ();
	auto election1 (node2.active.election (scendere::qualified_root (scendere::dev::genesis->hash (), scendere::dev::genesis->hash ())));
	ASSERT_NE (nullptr, election1);
	ASSERT_EQ (1, election1->votes ().size ());
	ASSERT_NE (nullptr, node1.block (publish1.block->hash ()));
	ASSERT_NE (nullptr, node2.block (publish2.block->hash ()));
	ASSERT_TIMELY (10s, node2.ledger.block_or_pruned_exists (publish1.block->hash ()));
	auto winner (*election1->tally ().begin ());
	ASSERT_EQ (*publish1.block, *winner.second);
	ASSERT_EQ (scendere::dev::constants.genesis_amount - 100, winner.first);
	ASSERT_TRUE (node1.ledger.block_or_pruned_exists (publish1.block->hash ()));
	ASSERT_TRUE (node2.ledger.block_or_pruned_exists (publish1.block->hash ()));
	ASSERT_FALSE (node2.ledger.block_or_pruned_exists (publish2.block->hash ()));
}

TEST (node, fork_multi_flip)
{
	std::vector<scendere::transport::transport_type> types{ scendere::transport::transport_type::tcp, scendere::transport::transport_type::udp };
	for (auto & type : types)
	{
		scendere::system system;
		scendere::node_flags node_flags;
		if (type == scendere::transport::transport_type::udp)
		{
			node_flags.disable_tcp_realtime = true;
			node_flags.disable_bootstrap_listener = true;
			node_flags.disable_udp = false;
		}
		scendere::node_config node_config (scendere::get_available_port (), system.logging);
		node_config.frontiers_confirmation = scendere::frontiers_confirmation_mode::disabled;
		auto & node1 (*system.add_node (node_config, node_flags, type));
		node_config.peering_port = scendere::get_available_port ();
		auto & node2 (*system.add_node (node_config, node_flags, type));
		ASSERT_EQ (1, node1.network.size ());
		scendere::keypair key1;
		scendere::send_block_builder builder;
		auto send1 = builder.make_block ()
					 .previous (scendere::dev::genesis->hash ())
					 .destination (key1.pub)
					 .balance (scendere::dev::constants.genesis_amount - 100)
					 .sign (scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub)
					 .work (*system.work.generate (scendere::dev::genesis->hash ()))
					 .build_shared ();
		scendere::publish publish1{ scendere::dev::network_params.network, send1 };
		scendere::keypair key2;
		auto send2 = builder.make_block ()
					 .previous (scendere::dev::genesis->hash ())
					 .destination (key2.pub)
					 .balance (scendere::dev::constants.genesis_amount - 100)
					 .sign (scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub)
					 .work (*system.work.generate (scendere::dev::genesis->hash ()))
					 .build_shared ();
		scendere::publish publish2{ scendere::dev::network_params.network, send2 };
		auto send3 = builder.make_block ()
					 .previous (publish2.block->hash ())
					 .destination (key2.pub)
					 .balance (scendere::dev::constants.genesis_amount - 100)
					 .sign (scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub)
					 .work (*system.work.generate (publish2.block->hash ()))
					 .build_shared ();
		scendere::publish publish3{ scendere::dev::network_params.network, send3 };
		node1.network.inbound (publish1, node1.network.udp_channels.create (node1.network.endpoint ()));
		node2.network.inbound (publish2, node2.network.udp_channels.create (node2.network.endpoint ()));
		node2.network.inbound (publish3, node2.network.udp_channels.create (node2.network.endpoint ()));
		node1.block_processor.flush ();
		node1.scheduler.flush ();
		node2.block_processor.flush ();
		node2.scheduler.flush ();
		ASSERT_EQ (1, node1.active.size ());
		ASSERT_EQ (1, node2.active.size ());
		system.wallet (0)->insert_adhoc (scendere::dev::genesis_key.prv);
		node1.network.inbound (publish2, node1.network.udp_channels.create (node1.network.endpoint ()));
		node1.network.inbound (publish3, node1.network.udp_channels.create (node1.network.endpoint ()));
		node1.block_processor.flush ();
		node2.network.inbound (publish1, node2.network.udp_channels.create (node2.network.endpoint ()));
		node2.block_processor.flush ();
		auto election1 (node2.active.election (scendere::qualified_root (scendere::dev::genesis->hash (), scendere::dev::genesis->hash ())));
		ASSERT_NE (nullptr, election1);
		ASSERT_EQ (1, election1->votes ().size ());
		ASSERT_TRUE (node1.ledger.block_or_pruned_exists (publish1.block->hash ()));
		ASSERT_TRUE (node2.ledger.block_or_pruned_exists (publish2.block->hash ()));
		ASSERT_TRUE (node2.ledger.block_or_pruned_exists (publish3.block->hash ()));
		ASSERT_TIMELY (10s, node2.ledger.block_or_pruned_exists (publish1.block->hash ()));
		auto winner (*election1->tally ().begin ());
		ASSERT_EQ (*publish1.block, *winner.second);
		ASSERT_EQ (scendere::dev::constants.genesis_amount - 100, winner.first);
		ASSERT_TRUE (node1.ledger.block_or_pruned_exists (publish1.block->hash ()));
		ASSERT_TRUE (node2.ledger.block_or_pruned_exists (publish1.block->hash ()));
		ASSERT_FALSE (node2.ledger.block_or_pruned_exists (publish2.block->hash ()));
		ASSERT_FALSE (node2.ledger.block_or_pruned_exists (publish3.block->hash ()));
	}
}

// Blocks that are no longer actively being voted on should be able to be evicted through bootstrapping.
// This could happen if a fork wasn't resolved before the process previously shut down
TEST (node, fork_bootstrap_flip)
{
	scendere::system system0;
	scendere::system system1;
	scendere::node_config config0{ scendere::get_available_port (), system0.logging };
	config0.frontiers_confirmation = scendere::frontiers_confirmation_mode::disabled;
	scendere::node_flags node_flags;
	node_flags.disable_bootstrap_bulk_push_client = true;
	node_flags.disable_lazy_bootstrap = true;
	auto & node1 = *system0.add_node (config0, node_flags);
	scendere::node_config config1 (scendere::get_available_port (), system1.logging);
	auto & node2 = *system1.add_node (config1, node_flags);
	system0.wallet (0)->insert_adhoc (scendere::dev::genesis_key.prv);
	scendere::block_hash latest = node1.latest (scendere::dev::genesis_key.pub);
	scendere::keypair key1;
	scendere::send_block_builder builder;
	auto send1 = builder.make_block ()
				 .previous (latest)
				 .destination (key1.pub)
				 .balance (scendere::dev::constants.genesis_amount - scendere::Gxrb_ratio)
				 .sign (scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub)
				 .work (*system0.work.generate (latest))
				 .build_shared ();
	scendere::keypair key2;
	auto send2 = builder.make_block ()
				 .previous (latest)
				 .destination (key2.pub)
				 .balance (scendere::dev::constants.genesis_amount - scendere::Gxrb_ratio)
				 .sign (scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub)
				 .work (*system0.work.generate (latest))
				 .build_shared ();
	// Insert but don't rebroadcast, simulating settled blocks
	ASSERT_EQ (scendere::process_result::progress, node1.ledger.process (node1.store.tx_begin_write (), *send1).code);
	ASSERT_EQ (scendere::process_result::progress, node2.ledger.process (node2.store.tx_begin_write (), *send2).code);
	ASSERT_TRUE (node2.store.block.exists (node2.store.tx_begin_read (), send2->hash ()));
	node2.bootstrap_initiator.bootstrap (node1.network.endpoint ()); // Additionally add new peer to confirm & replace bootstrap block
	auto again (true);
	system1.deadline_set (50s);
	while (again)
	{
		ASSERT_NO_ERROR (system0.poll ());
		ASSERT_NO_ERROR (system1.poll ());
		again = !node2.store.block.exists (node2.store.tx_begin_read (), send1->hash ());
	}
}

TEST (node, fork_open)
{
	scendere::system system (1);
	auto & node1 (*system.nodes[0]);
	scendere::keypair key1;
	auto send1 = scendere::send_block_builder ()
				 .previous (scendere::dev::genesis->hash ())
				 .destination (key1.pub)
				 .balance (0)
				 .sign (scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub)
				 .work (*system.work.generate (scendere::dev::genesis->hash ()))
				 .build_shared ();
	scendere::publish publish1{ scendere::dev::network_params.network, send1 };
	auto channel1 (node1.network.udp_channels.create (node1.network.endpoint ()));
	node1.network.inbound (publish1, channel1);
	node1.block_processor.flush ();
	node1.scheduler.flush ();
	auto election = node1.active.election (publish1.block->qualified_root ());
	ASSERT_NE (nullptr, election);
	election->force_confirm ();
	ASSERT_TIMELY (3s, node1.active.empty () && node1.block_confirmed (publish1.block->hash ()));
	scendere::open_block_builder builder;
	auto open1 = builder.make_block ()
				 .source (publish1.block->hash ())
				 .representative (1)
				 .account (key1.pub)
				 .sign (key1.prv, key1.pub)
				 .work (*system.work.generate (key1.pub))
				 .build_shared ();
	scendere::publish publish2{ scendere::dev::network_params.network, open1 };
	node1.network.inbound (publish2, channel1);
	node1.block_processor.flush ();
	node1.scheduler.flush ();
	ASSERT_EQ (1, node1.active.size ());
	auto open2 = builder.make_block ()
				 .source (publish1.block->hash ())
				 .representative (2)
				 .account (key1.pub)
				 .sign (key1.prv, key1.pub)
				 .work (*system.work.generate (key1.pub))
				 .build_shared ();
	scendere::publish publish3{ scendere::dev::network_params.network, open2 };
	system.wallet (0)->insert_adhoc (scendere::dev::genesis_key.prv);
	node1.network.inbound (publish3, channel1);
	node1.block_processor.flush ();
	node1.scheduler.flush ();
	election = node1.active.election (publish3.block->qualified_root ());
	ASSERT_EQ (2, election->blocks ().size ());
	ASSERT_EQ (publish2.block->hash (), election->winner ()->hash ());
	ASSERT_FALSE (election->confirmed ());
	ASSERT_TRUE (node1.block (publish2.block->hash ()));
	ASSERT_FALSE (node1.block (publish3.block->hash ()));
}

TEST (node, fork_open_flip)
{
	scendere::system system (2);
	auto & node1 (*system.nodes[0]);
	auto & node2 (*system.nodes[1]);
	ASSERT_EQ (1, node1.network.size ());
	scendere::keypair key1;
	scendere::keypair rep1;
	scendere::keypair rep2;
	auto send1 = scendere::send_block_builder ()
				 .previous (scendere::dev::genesis->hash ())
				 .destination (key1.pub)
				 .balance (scendere::dev::constants.genesis_amount - 1)
				 .sign (scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub)
				 .work (*system.work.generate (scendere::dev::genesis->hash ()))
				 .build_shared ();
	// A copy is necessary to avoid data races during ledger processing, which sets the sideband
	auto send1_copy (std::make_shared<scendere::send_block> (*send1));
	node1.process_active (send1);
	node2.process_active (send1_copy);
	// We should be keeping this block
	scendere::open_block_builder builder;
	auto open1 = builder.make_block ()
				 .source (send1->hash ())
				 .representative (rep1.pub)
				 .account (key1.pub)
				 .sign (key1.prv, key1.pub)
				 .work (*system.work.generate (key1.pub))
				 .build_shared ();
	// This block should be evicted
	auto open2 = builder.make_block ()
				 .source (send1->hash ())
				 .representative (rep2.pub)
				 .account (key1.pub)
				 .sign (key1.prv, key1.pub)
				 .work (*system.work.generate (key1.pub))
				 .build_shared ();
	ASSERT_FALSE (*open1 == *open2);
	// node1 gets copy that will remain
	node1.process_active (open1);
	node1.block_processor.flush ();
	node1.block_confirm (open1);
	// node2 gets copy that will be evicted
	node2.process_active (open2);
	node2.block_processor.flush ();
	node2.block_confirm (open2);
	ASSERT_EQ (2, node1.active.size ());
	ASSERT_EQ (2, node2.active.size ());
	system.wallet (0)->insert_adhoc (scendere::dev::genesis_key.prv);
	// Notify both nodes that a fork exists
	node1.process_active (open2);
	node1.block_processor.flush ();
	node2.process_active (open1);
	node2.block_processor.flush ();
	auto election1 (node2.active.election (open1->qualified_root ()));
	ASSERT_NE (nullptr, election1);
	ASSERT_EQ (1, election1->votes ().size ());
	ASSERT_TRUE (node1.block (open1->hash ()) != nullptr);
	ASSERT_TRUE (node2.block (open2->hash ()) != nullptr);
	// Node2 should eventually settle on open1
	ASSERT_TIMELY (10s, node2.block (open1->hash ()));
	node2.block_processor.flush ();
	auto transaction1 (node1.store.tx_begin_read ());
	auto transaction2 (node2.store.tx_begin_read ());
	auto winner (*election1->tally ().begin ());
	ASSERT_EQ (*open1, *winner.second);
	ASSERT_EQ (scendere::dev::constants.genesis_amount - 1, winner.first);
	ASSERT_TRUE (node1.store.block.exists (transaction1, open1->hash ()));
	ASSERT_TRUE (node2.store.block.exists (transaction2, open1->hash ()));
	ASSERT_FALSE (node2.store.block.exists (transaction2, open2->hash ()));
}

TEST (node, coherent_observer)
{
	scendere::system system (1);
	auto & node1 (*system.nodes[0]);
	node1.observers.blocks.add ([&node1] (scendere::election_status const & status_a, std::vector<scendere::vote_with_weight_info> const &, scendere::account const &, scendere::uint128_t const &, bool, bool) {
		auto transaction (node1.store.tx_begin_read ());
		ASSERT_TRUE (node1.store.block.exists (transaction, status_a.winner->hash ()));
	});
	system.wallet (0)->insert_adhoc (scendere::dev::genesis_key.prv);
	scendere::keypair key;
	system.wallet (0)->send_action (scendere::dev::genesis_key.pub, key.pub, 1);
}

TEST (node, fork_no_vote_quorum)
{
	scendere::system system (3);
	auto & node1 (*system.nodes[0]);
	auto & node2 (*system.nodes[1]);
	auto & node3 (*system.nodes[2]);
	system.wallet (0)->insert_adhoc (scendere::dev::genesis_key.prv);
	auto key4 (system.wallet (0)->deterministic_insert ());
	system.wallet (0)->send_action (scendere::dev::genesis_key.pub, key4, scendere::dev::constants.genesis_amount / 4);
	auto key1 (system.wallet (1)->deterministic_insert ());
	{
		auto transaction (system.wallet (1)->wallets.tx_begin_write ());
		system.wallet (1)->store.representative_set (transaction, key1);
	}
	auto block (system.wallet (0)->send_action (scendere::dev::genesis_key.pub, key1, node1.config.receive_minimum.number ()));
	ASSERT_NE (nullptr, block);
	ASSERT_TIMELY (30s, node3.balance (key1) == node1.config.receive_minimum.number () && node2.balance (key1) == node1.config.receive_minimum.number () && node1.balance (key1) == node1.config.receive_minimum.number ());
	ASSERT_EQ (node1.config.receive_minimum.number (), node1.weight (key1));
	ASSERT_EQ (node1.config.receive_minimum.number (), node2.weight (key1));
	ASSERT_EQ (node1.config.receive_minimum.number (), node3.weight (key1));
	scendere::state_block send1 (scendere::dev::genesis_key.pub, block->hash (), scendere::dev::genesis_key.pub, (scendere::dev::constants.genesis_amount / 4) - (node1.config.receive_minimum.number () * 2), key1, scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub, *system.work.generate (block->hash ()));
	ASSERT_EQ (scendere::process_result::progress, node1.process (send1).code);
	ASSERT_EQ (scendere::process_result::progress, node2.process (send1).code);
	ASSERT_EQ (scendere::process_result::progress, node3.process (send1).code);
	auto key2 (system.wallet (2)->deterministic_insert ());
	auto send2 = scendere::send_block_builder ()
				 .previous (block->hash ())
				 .destination (key2)
				 .balance ((scendere::dev::constants.genesis_amount / 4) - (node1.config.receive_minimum.number () * 2))
				 .sign (scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub)
				 .work (*system.work.generate (block->hash ()))
				 .build_shared ();
	scendere::raw_key key3;
	auto transaction (system.wallet (1)->wallets.tx_begin_read ());
	ASSERT_FALSE (system.wallet (1)->store.fetch (transaction, key1, key3));
	auto vote (std::make_shared<scendere::vote> (key1, key3, 0, 0, send2));
	scendere::confirm_ack confirm{ scendere::dev::network_params.network, vote };
	std::vector<uint8_t> buffer;
	{
		scendere::vectorstream stream (buffer);
		confirm.serialize (stream);
	}
	auto channel = node2.network.find_node_id (node3.node_id.pub);
	ASSERT_NE (nullptr, channel);
	channel->send_buffer (scendere::shared_const_buffer (std::move (buffer)));
	ASSERT_TIMELY (10s, node3.stats.count (scendere::stat::type::message, scendere::stat::detail::confirm_ack, scendere::stat::dir::in) >= 3);
	ASSERT_TRUE (node1.latest (scendere::dev::genesis_key.pub) == send1.hash ());
	ASSERT_TRUE (node2.latest (scendere::dev::genesis_key.pub) == send1.hash ());
	ASSERT_TRUE (node3.latest (scendere::dev::genesis_key.pub) == send1.hash ());
}

// Disabled because it sometimes takes way too long (but still eventually finishes)
TEST (node, DISABLED_fork_pre_confirm)
{
	scendere::system system (3);
	auto & node0 (*system.nodes[0]);
	auto & node1 (*system.nodes[1]);
	auto & node2 (*system.nodes[2]);
	system.wallet (0)->insert_adhoc (scendere::dev::genesis_key.prv);
	scendere::keypair key1;
	system.wallet (1)->insert_adhoc (key1.prv);
	{
		auto transaction (system.wallet (1)->wallets.tx_begin_write ());
		system.wallet (1)->store.representative_set (transaction, key1.pub);
	}
	scendere::keypair key2;
	system.wallet (2)->insert_adhoc (key2.prv);
	{
		auto transaction (system.wallet (2)->wallets.tx_begin_write ());
		system.wallet (2)->store.representative_set (transaction, key2.pub);
	}
	auto block0 (system.wallet (0)->send_action (scendere::dev::genesis_key.pub, key1.pub, scendere::dev::constants.genesis_amount / 3));
	ASSERT_NE (nullptr, block0);
	ASSERT_TIMELY (30s, node0.balance (key1.pub) != 0);
	auto block1 (system.wallet (0)->send_action (scendere::dev::genesis_key.pub, key2.pub, scendere::dev::constants.genesis_amount / 3));
	ASSERT_NE (nullptr, block1);
	ASSERT_TIMELY (30s, node0.balance (key2.pub) != 0);
	scendere::keypair key3;
	scendere::keypair key4;
	scendere::state_block_builder builder;
	auto block2 = builder.make_block ()
				  .account (scendere::dev::genesis_key.pub)
				  .previous (node0.latest (scendere::dev::genesis_key.pub))
				  .representative (key3.pub)
				  .balance (node0.balance (scendere::dev::genesis_key.pub))
				  .link (0)
				  .sign (scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub)
				  .work (0)
				  .build_shared ();
	auto block3 = builder.make_block ()
				  .account (scendere::dev::genesis_key.pub)
				  .previous (node0.latest (scendere::dev::genesis_key.pub))
				  .representative (key4.pub)
				  .balance (node0.balance (scendere::dev::genesis_key.pub))
				  .link (0)
				  .sign (scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub)
				  .work (0)
				  .build_shared ();
	node0.work_generate_blocking (*block2);
	node0.work_generate_blocking (*block3);
	node0.process_active (block2);
	node1.process_active (block2);
	node2.process_active (block3);
	auto done (false);
	// Extend deadline; we must finish within a total of 100 seconds
	system.deadline_set (70s);
	while (!done)
	{
		done |= node0.latest (scendere::dev::genesis_key.pub) == block2->hash () && node1.latest (scendere::dev::genesis_key.pub) == block2->hash () && node2.latest (scendere::dev::genesis_key.pub) == block2->hash ();
		done |= node0.latest (scendere::dev::genesis_key.pub) == block3->hash () && node1.latest (scendere::dev::genesis_key.pub) == block3->hash () && node2.latest (scendere::dev::genesis_key.pub) == block3->hash ();
		ASSERT_NO_ERROR (system.poll ());
	}
}

// Sometimes hangs on the bootstrap_initiator.bootstrap call
TEST (node, DISABLED_fork_stale)
{
	scendere::system system1 (1);
	system1.wallet (0)->insert_adhoc (scendere::dev::genesis_key.prv);
	scendere::system system2 (1);
	auto & node1 (*system1.nodes[0]);
	auto & node2 (*system2.nodes[0]);
	node2.bootstrap_initiator.bootstrap (node1.network.endpoint (), false);
	std::shared_ptr<scendere::transport::channel> channel (std::make_shared<scendere::transport::channel_udp> (node2.network.udp_channels, node1.network.endpoint (), node2.network_params.network.protocol_version));
	auto vote = std::make_shared<scendere::vote> (scendere::dev::genesis_key.pub, scendere::dev::genesis_key.prv, 0, 0, std::vector<scendere::block_hash> ());
	node2.rep_crawler.response (channel, vote);
	scendere::keypair key1;
	scendere::keypair key2;
	scendere::state_block_builder builder;
	auto send3 = builder.make_block ()
				 .account (scendere::dev::genesis_key.pub)
				 .previous (scendere::dev::genesis->hash ())
				 .representative (scendere::dev::genesis_key.pub)
				 .balance (scendere::dev::constants.genesis_amount - scendere::Mxrb_ratio)
				 .link (key1.pub)
				 .sign (scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub)
				 .work (0)
				 .build_shared ();
	node1.work_generate_blocking (*send3);
	node1.process_active (send3);
	system2.deadline_set (10s);
	while (node2.block (send3->hash ()) == nullptr)
	{
		system1.poll ();
		ASSERT_NO_ERROR (system2.poll ());
	}
	auto send1 = builder.make_block ()
				 .account (scendere::dev::genesis_key.pub)
				 .previous (send3->hash ())
				 .representative (scendere::dev::genesis_key.pub)
				 .balance (scendere::dev::constants.genesis_amount - 2 * scendere::Mxrb_ratio)
				 .link (key1.pub)
				 .sign (scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub)
				 .work (0)
				 .build_shared ();
	node1.work_generate_blocking (*send1);
	auto send2 = builder.make_block ()
				 .account (scendere::dev::genesis_key.pub)
				 .previous (send3->hash ())
				 .representative (scendere::dev::genesis_key.pub)
				 .balance (scendere::dev::constants.genesis_amount - 2 * scendere::Mxrb_ratio)
				 .link (key2.pub)
				 .sign (scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub)
				 .work (0)
				 .build_shared ();
	node1.work_generate_blocking (*send2);
	{
		auto transaction1 (node1.store.tx_begin_write ());
		ASSERT_EQ (scendere::process_result::progress, node1.ledger.process (transaction1, *send1).code);
		auto transaction2 (node2.store.tx_begin_write ());
		ASSERT_EQ (scendere::process_result::progress, node2.ledger.process (transaction2, *send2).code);
	}
	node1.process_active (send1);
	node1.process_active (send2);
	node2.process_active (send1);
	node2.process_active (send2);
	node2.bootstrap_initiator.bootstrap (node1.network.endpoint (), false);
	while (node2.block (send1->hash ()) == nullptr)
	{
		system1.poll ();
		ASSERT_NO_ERROR (system2.poll ());
	}
}

// Test disabled because it's failing intermittently.
// PR in which it got disabled: https://github.com/scenderecurrency/scendere-node/pull/3512
// Issue for investigating it: https://github.com/scenderecurrency/scendere-node/issues/3516
TEST (node, DISABLED_broadcast_elected)
{
	std::vector<scendere::transport::transport_type> types{ scendere::transport::transport_type::tcp, scendere::transport::transport_type::udp };
	for (auto & type : types)
	{
		scendere::node_flags node_flags;
		if (type == scendere::transport::transport_type::udp)
		{
			node_flags.disable_tcp_realtime = true;
			node_flags.disable_bootstrap_listener = true;
			node_flags.disable_udp = false;
		}
		scendere::system system;
		scendere::node_config node_config (scendere::get_available_port (), system.logging);
		node_config.frontiers_confirmation = scendere::frontiers_confirmation_mode::disabled;
		auto node0 = system.add_node (node_config, node_flags, type);
		node_config.peering_port = scendere::get_available_port ();
		auto node1 = system.add_node (node_config, node_flags, type);
		node_config.peering_port = scendere::get_available_port ();
		auto node2 = system.add_node (node_config, node_flags, type);
		scendere::keypair rep_big;
		scendere::keypair rep_small;
		scendere::keypair rep_other;
		scendere::block_builder builder;
		{
			auto transaction0 (node0->store.tx_begin_write ());
			auto transaction1 (node1->store.tx_begin_write ());
			auto transaction2 (node2->store.tx_begin_write ());
			auto fund_big = *builder.send ()
							 .previous (scendere::dev::genesis->hash ())
							 .destination (rep_big.pub)
							 .balance (scendere::Gxrb_ratio * 5)
							 .sign (scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub)
							 .work (*system.work.generate (scendere::dev::genesis->hash ()))
							 .build ();
			auto open_big = *builder.open ()
							 .source (fund_big.hash ())
							 .representative (rep_big.pub)
							 .account (rep_big.pub)
							 .sign (rep_big.prv, rep_big.pub)
							 .work (*system.work.generate (rep_big.pub))
							 .build ();
			auto fund_small = *builder.send ()
							   .previous (fund_big.hash ())
							   .destination (rep_small.pub)
							   .balance (scendere::Gxrb_ratio * 2)
							   .sign (scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub)
							   .work (*system.work.generate (fund_big.hash ()))
							   .build ();
			auto open_small = *builder.open ()
							   .source (fund_small.hash ())
							   .representative (rep_small.pub)
							   .account (rep_small.pub)
							   .sign (rep_small.prv, rep_small.pub)
							   .work (*system.work.generate (rep_small.pub))
							   .build ();
			auto fund_other = *builder.send ()
							   .previous (fund_small.hash ())
							   .destination (rep_other.pub)
							   .balance (scendere::Gxrb_ratio)
							   .sign (scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub)
							   .work (*system.work.generate (fund_small.hash ()))
							   .build ();
			auto open_other = *builder.open ()
							   .source (fund_other.hash ())
							   .representative (rep_other.pub)
							   .account (rep_other.pub)
							   .sign (rep_other.prv, rep_other.pub)
							   .work (*system.work.generate (rep_other.pub))
							   .build ();
			ASSERT_EQ (scendere::process_result::progress, node0->ledger.process (transaction0, fund_big).code);
			ASSERT_EQ (scendere::process_result::progress, node1->ledger.process (transaction1, fund_big).code);
			ASSERT_EQ (scendere::process_result::progress, node2->ledger.process (transaction2, fund_big).code);
			ASSERT_EQ (scendere::process_result::progress, node0->ledger.process (transaction0, open_big).code);
			ASSERT_EQ (scendere::process_result::progress, node1->ledger.process (transaction1, open_big).code);
			ASSERT_EQ (scendere::process_result::progress, node2->ledger.process (transaction2, open_big).code);
			ASSERT_EQ (scendere::process_result::progress, node0->ledger.process (transaction0, fund_small).code);
			ASSERT_EQ (scendere::process_result::progress, node1->ledger.process (transaction1, fund_small).code);
			ASSERT_EQ (scendere::process_result::progress, node2->ledger.process (transaction2, fund_small).code);
			ASSERT_EQ (scendere::process_result::progress, node0->ledger.process (transaction0, open_small).code);
			ASSERT_EQ (scendere::process_result::progress, node1->ledger.process (transaction1, open_small).code);
			ASSERT_EQ (scendere::process_result::progress, node2->ledger.process (transaction2, open_small).code);
			ASSERT_EQ (scendere::process_result::progress, node0->ledger.process (transaction0, fund_other).code);
			ASSERT_EQ (scendere::process_result::progress, node1->ledger.process (transaction1, fund_other).code);
			ASSERT_EQ (scendere::process_result::progress, node2->ledger.process (transaction2, fund_other).code);
			ASSERT_EQ (scendere::process_result::progress, node0->ledger.process (transaction0, open_other).code);
			ASSERT_EQ (scendere::process_result::progress, node1->ledger.process (transaction1, open_other).code);
			ASSERT_EQ (scendere::process_result::progress, node2->ledger.process (transaction2, open_other).code);
		}
		// Confirm blocks to allow voting
		for (auto & node : system.nodes)
		{
			auto block (node->block (node->latest (scendere::dev::genesis_key.pub)));
			ASSERT_NE (nullptr, block);
			node->block_confirm (block);
			auto election (node->active.election (block->qualified_root ()));
			ASSERT_NE (nullptr, election);
			election->force_confirm ();
			ASSERT_TIMELY (5s, 4 == node->ledger.cache.cemented_count)
		}

		system.wallet (0)->insert_adhoc (rep_big.prv);
		system.wallet (1)->insert_adhoc (rep_small.prv);
		system.wallet (2)->insert_adhoc (rep_other.prv);
		auto fork0 = builder.send ()
					 .previous (node2->latest (scendere::dev::genesis_key.pub))
					 .destination (rep_small.pub)
					 .balance (0)
					 .sign (scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub)
					 .work (*node0->work_generate_blocking (node2->latest (scendere::dev::genesis_key.pub)))
					 .build_shared ();
		// A copy is necessary to avoid data races during ledger processing, which sets the sideband
		auto fork0_copy (std::make_shared<scendere::send_block> (*fork0));
		node0->process_active (fork0);
		node1->process_active (fork0_copy);
		auto fork1 = builder.send ()
					 .previous (node2->latest (scendere::dev::genesis_key.pub))
					 .destination (rep_big.pub)
					 .balance (0)
					 .sign (scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub)
					 .work (*node0->work_generate_blocking (node2->latest (scendere::dev::genesis_key.pub)))
					 .build_shared ();
		system.wallet (2)->insert_adhoc (rep_small.prv);
		node2->process_active (fork1);
		ASSERT_TIMELY (10s, node0->ledger.block_or_pruned_exists (fork0->hash ()) && node1->ledger.block_or_pruned_exists (fork0->hash ()));
		system.deadline_set (50s);
		while (!node2->ledger.block_or_pruned_exists (fork0->hash ()))
		{
			auto ec = system.poll ();
			ASSERT_TRUE (node0->ledger.block_or_pruned_exists (fork0->hash ()));
			ASSERT_TRUE (node1->ledger.block_or_pruned_exists (fork0->hash ()));
			ASSERT_NO_ERROR (ec);
		}
		ASSERT_TIMELY (5s, node1->stats.count (scendere::stat::type::confirmation_observer, scendere::stat::detail::inactive_conf_height, scendere::stat::dir::out) != 0);
	}
}

TEST (node, rep_self_vote)
{
	scendere::system system;
	scendere::node_config node_config (scendere::get_available_port (), system.logging);
	node_config.online_weight_minimum = std::numeric_limits<scendere::uint128_t>::max ();
	node_config.frontiers_confirmation = scendere::frontiers_confirmation_mode::disabled;
	auto node0 = system.add_node (node_config);
	scendere::keypair rep_big;
	scendere::block_builder builder;
	auto fund_big = *builder.send ()
					 .previous (scendere::dev::genesis->hash ())
					 .destination (rep_big.pub)
					 .balance (scendere::uint128_t{ "0xb0000000000000000000000000000000" })
					 .sign (scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub)
					 .work (*system.work.generate (scendere::dev::genesis->hash ()))
					 .build ();
	auto open_big = *builder.open ()
					 .source (fund_big.hash ())
					 .representative (rep_big.pub)
					 .account (rep_big.pub)
					 .sign (rep_big.prv, rep_big.pub)
					 .work (*system.work.generate (rep_big.pub))
					 .build ();
	ASSERT_EQ (scendere::process_result::progress, node0->process (fund_big).code);
	ASSERT_EQ (scendere::process_result::progress, node0->process (open_big).code);
	// Confirm both blocks, allowing voting on the upcoming block
	node0->block_confirm (node0->block (open_big.hash ()));
	auto election = node0->active.election (open_big.qualified_root ());
	ASSERT_NE (nullptr, election);
	election->force_confirm ();

	system.wallet (0)->insert_adhoc (rep_big.prv);
	system.wallet (0)->insert_adhoc (scendere::dev::genesis_key.prv);
	ASSERT_EQ (system.wallet (0)->wallets.reps ().voting, 2);
	auto block0 = builder.send ()
				  .previous (fund_big.hash ())
				  .destination (rep_big.pub)
				  .balance (scendere::uint128_t ("0x60000000000000000000000000000000"))
				  .sign (scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub)
				  .work (*system.work.generate (fund_big.hash ()))
				  .build_shared ();
	ASSERT_EQ (scendere::process_result::progress, node0->process (*block0).code);
	auto & active = node0->active;
	auto & scheduler = node0->scheduler;
	scheduler.activate (scendere::dev::genesis_key.pub, node0->store.tx_begin_read ());
	scheduler.flush ();
	auto election1 = active.election (block0->qualified_root ());
	ASSERT_NE (nullptr, election1);
	// Wait until representatives are activated & make vote
	ASSERT_TIMELY (1s, election1->votes ().size () == 3);
	auto rep_votes (election1->votes ());
	ASSERT_NE (rep_votes.end (), rep_votes.find (scendere::dev::genesis_key.pub));
	ASSERT_NE (rep_votes.end (), rep_votes.find (rep_big.pub));
}

// Bootstrapping shouldn't republish the blocks to the network.
TEST (node, DISABLED_bootstrap_no_publish)
{
	scendere::system system0 (1);
	scendere::system system1 (1);
	auto node0 (system0.nodes[0]);
	auto node1 (system1.nodes[0]);
	scendere::keypair key0;
	// node0 knows about send0 but node1 doesn't.
	scendere::send_block send0 (node0->latest (scendere::dev::genesis_key.pub), key0.pub, 500, scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub, 0);
	{
		auto transaction (node0->store.tx_begin_write ());
		ASSERT_EQ (scendere::process_result::progress, node0->ledger.process (transaction, send0).code);
	}
	ASSERT_FALSE (node1->bootstrap_initiator.in_progress ());
	node1->bootstrap_initiator.bootstrap (node0->network.endpoint (), false);
	ASSERT_TRUE (node1->active.empty ());
	system1.deadline_set (10s);
	while (node1->block (send0.hash ()) == nullptr)
	{
		// Poll until the TCP connection is torn down and in_progress goes false
		system0.poll ();
		auto ec = system1.poll ();
		// There should never be an active transaction because the only activity is bootstrapping 1 block which shouldn't be publishing.
		ASSERT_TRUE (node1->active.empty ());
		ASSERT_NO_ERROR (ec);
	}
}

// Check that an outgoing bootstrap request can push blocks
// Test disabled because it's failing intermittently.
// PR in which it got disabled: https://github.com/scenderecurrency/scendere-node/pull/3512
// Issue for investigating it: https://github.com/scenderecurrency/scendere-node/issues/3515
TEST (node, DISABLED_bootstrap_bulk_push)
{
	scendere::system system0;
	scendere::system system1;
	scendere::node_config config0 (scendere::get_available_port (), system0.logging);
	config0.frontiers_confirmation = scendere::frontiers_confirmation_mode::disabled;
	auto node0 (system0.add_node (config0));
	scendere::node_config config1 (scendere::get_available_port (), system1.logging);
	config1.frontiers_confirmation = scendere::frontiers_confirmation_mode::disabled;
	auto node1 (system1.add_node (config1));
	scendere::keypair key0;
	// node0 knows about send0 but node1 doesn't.
	auto send0 = scendere::send_block_builder ()
				 .previous (scendere::dev::genesis->hash ())
				 .destination (key0.pub)
				 .balance (500)
				 .sign (scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub)
				 .work (*node0->work_generate_blocking (scendere::dev::genesis->hash ()))
				 .build_shared ();
	ASSERT_EQ (scendere::process_result::progress, node0->process (*send0).code);

	ASSERT_FALSE (node0->bootstrap_initiator.in_progress ());
	ASSERT_FALSE (node1->bootstrap_initiator.in_progress ());
	ASSERT_TRUE (node1->active.empty ());
	node0->bootstrap_initiator.bootstrap (node1->network.endpoint (), false);
	system1.deadline_set (10s);
	while (node1->block (send0->hash ()) == nullptr)
	{
		ASSERT_NO_ERROR (system0.poll ());
		ASSERT_NO_ERROR (system1.poll ());
	}
	// since this uses bulk_push, the new block should be republished
	system1.deadline_set (10s);
	while (node1->active.empty ())
	{
		ASSERT_NO_ERROR (system0.poll ());
		ASSERT_NO_ERROR (system1.poll ());
	}
}

// Bootstrapping a forked open block should succeed.
TEST (node, bootstrap_fork_open)
{
	scendere::system system;
	scendere::node_config node_config (scendere::get_available_port (), system.logging);
	auto node0 = system.add_node (node_config);
	node_config.peering_port = scendere::get_available_port ();
	auto node1 = system.add_node (node_config);
	scendere::keypair key0;
	scendere::block_builder builder;
	auto send0 = *builder.send ()
				  .previous (scendere::dev::genesis->hash ())
				  .destination (key0.pub)
				  .balance (scendere::dev::constants.genesis_amount - 500)
				  .sign (scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub)
				  .work (*system.work.generate (scendere::dev::genesis->hash ()))
				  .build ();
	auto open0 = *builder.open ()
				  .source (send0.hash ())
				  .representative (1)
				  .account (key0.pub)
				  .sign (key0.prv, key0.pub)
				  .work (*system.work.generate (key0.pub))
				  .build ();
	auto open1 = *builder.open ()
				  .source (send0.hash ())
				  .representative (2)
				  .account (key0.pub)
				  .sign (key0.prv, key0.pub)
				  .work (*system.work.generate (key0.pub))
				  .build ();
	// Both know about send0
	ASSERT_EQ (scendere::process_result::progress, node0->process (send0).code);
	ASSERT_EQ (scendere::process_result::progress, node1->process (send0).code);
	// Confirm send0 to allow starting and voting on the following blocks
	for (auto node : system.nodes)
	{
		node->block_confirm (node->block (node->latest (scendere::dev::genesis_key.pub)));
		ASSERT_TIMELY (1s, node->active.election (send0.qualified_root ()));
		auto election = node->active.election (send0.qualified_root ());
		ASSERT_NE (nullptr, election);
		election->force_confirm ();
		ASSERT_TIMELY (2s, node->active.empty ());
	}
	ASSERT_TIMELY (3s, node0->block_confirmed (send0.hash ()));
	// They disagree about open0/open1
	ASSERT_EQ (scendere::process_result::progress, node0->process (open0).code);
	ASSERT_EQ (scendere::process_result::progress, node1->process (open1).code);
	system.wallet (0)->insert_adhoc (scendere::dev::genesis_key.prv);
	ASSERT_FALSE (node1->ledger.block_or_pruned_exists (open0.hash ()));
	ASSERT_FALSE (node1->bootstrap_initiator.in_progress ());
	node1->bootstrap_initiator.bootstrap (node0->network.endpoint (), false);
	ASSERT_TIMELY (1s, node1->active.empty ());
	ASSERT_TIMELY (10s, !node1->ledger.block_or_pruned_exists (open1.hash ()) && node1->ledger.block_or_pruned_exists (open0.hash ()));
}

// Unconfirmed blocks from bootstrap should be confirmed
TEST (node, bootstrap_confirm_frontiers)
{
	scendere::system system0 (1);
	scendere::system system1 (1);
	auto node0 (system0.nodes[0]);
	auto node1 (system1.nodes[0]);
	system0.wallet (0)->insert_adhoc (scendere::dev::genesis_key.prv);
	scendere::keypair key0;
	// node0 knows about send0 but node1 doesn't.
	auto send0 = scendere::send_block_builder ()
				 .previous (scendere::dev::genesis->hash ())
				 .destination (key0.pub)
				 .balance (scendere::dev::constants.genesis_amount - 500)
				 .sign (scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub)
				 .work (*node0->work_generate_blocking (scendere::dev::genesis->hash ()))
				 .build_shared ();
	ASSERT_EQ (scendere::process_result::progress, node0->process (*send0).code);

	ASSERT_FALSE (node0->bootstrap_initiator.in_progress ());
	ASSERT_FALSE (node1->bootstrap_initiator.in_progress ());
	ASSERT_TRUE (node1->active.empty ());
	node1->bootstrap_initiator.bootstrap (node0->network.endpoint ()); // Additionally add new peer to confirm bootstrap frontier
	system1.deadline_set (10s);
	while (node1->block (send0->hash ()) == nullptr)
	{
		ASSERT_NO_ERROR (system0.poll ());
		ASSERT_NO_ERROR (system1.poll ());
	}
	// Wait for election start
	system1.deadline_set (10s);
	while (node1->active.empty ())
	{
		ASSERT_NO_ERROR (system0.poll ());
		ASSERT_NO_ERROR (system1.poll ());
	}
	{
		scendere::lock_guard<scendere::mutex> guard (node1->active.mutex);
		auto existing1 (node1->active.blocks.find (send0->hash ()));
		ASSERT_NE (node1->active.blocks.end (), existing1);
	}
	// Wait for confirmation height update
	system1.deadline_set (10s);
	bool done (false);
	while (!done)
	{
		{
			auto transaction (node1->store.tx_begin_read ());
			done = node1->ledger.block_confirmed (transaction, send0->hash ());
		}
		ASSERT_NO_ERROR (system0.poll ());
		ASSERT_NO_ERROR (system1.poll ());
	}
}

// Test that if we create a block that isn't confirmed, the bootstrapping processes sync the missing block.
TEST (node, unconfirmed_send)
{
	scendere::system system{};

	auto & node1 = *system.add_node ();
	auto wallet1 = system.wallet (0);
	wallet1->insert_adhoc (scendere::dev::genesis_key.prv);

	scendere::keypair key2{};
	auto & node2 = *system.add_node ();
	auto wallet2 = system.wallet (1);
	wallet2->insert_adhoc (key2.prv);

	// firstly, send two units from node1 to node2 and expect that both nodes see the block as confirmed
	// (node1 will start an election for it, vote on it and node2 gets synced up)
	auto send1 = wallet1->send_action (scendere::dev::genesis->account (), key2.pub, 2 * scendere::Mxrb_ratio);
	ASSERT_TIMELY (5s, node1.block_confirmed (send1->hash ()));
	ASSERT_TIMELY (5s, node2.block_confirmed (send1->hash ()));

	// wait until receive1 (auto-receive created by wallet) is cemented
	ASSERT_TIMELY (5s, node2.get_confirmation_height (node2.store.tx_begin_read (), key2.pub) == 1);
	ASSERT_EQ (node2.balance (key2.pub), 2 * scendere::Mxrb_ratio);
	auto recv1 = node2.ledger.find_receive_block_by_send_hash (node2.store.tx_begin_read (), key2.pub, send1->hash ());

	// create send2 to send from node2 to node1 and save it to node2's ledger without triggering an election (node1 does not hear about it)
	auto send2 = scendere::state_block_builder{}
				 .make_block ()
				 .account (key2.pub)
				 .previous (recv1->hash ())
				 .representative (scendere::dev::genesis_key.pub)
				 .balance (scendere::Mxrb_ratio)
				 .link (scendere::dev::genesis->account ())
				 .sign (key2.prv, key2.pub)
				 .work (*system.work.generate (recv1->hash ()))
				 .build_shared ();
	ASSERT_EQ (scendere::process_result::progress, node2.process (*send2).code);

	auto send3 = wallet2->send_action (key2.pub, scendere::dev::genesis->account (), scendere::Mxrb_ratio);
	ASSERT_TIMELY (5s, node2.block_confirmed (send2->hash ()));
	ASSERT_TIMELY (5s, node1.block_confirmed (send2->hash ()));
	ASSERT_TIMELY (5s, node2.block_confirmed (send3->hash ()));
	ASSERT_TIMELY (5s, node1.block_confirmed (send3->hash ()));
	ASSERT_TIMELY (5s, node2.ledger.cache.cemented_count == 7);
	ASSERT_TIMELY (5s, node1.balance (scendere::dev::genesis->account ()) == scendere::dev::constants.genesis_amount);
}

// Test that nodes can track nodes that have rep weight for priority broadcasting
TEST (node, rep_list)
{
	scendere::system system (2);
	auto & node1 (*system.nodes[1]);
	auto wallet0 (system.wallet (0));
	auto wallet1 (system.wallet (1));
	// Node0 has a rep
	wallet0->insert_adhoc (scendere::dev::genesis_key.prv);
	scendere::keypair key1;
	// Broadcast a confirm so others should know this is a rep node
	wallet0->send_action (scendere::dev::genesis_key.pub, key1.pub, scendere::Mxrb_ratio);
	ASSERT_EQ (0, node1.rep_crawler.representatives (1).size ());
	system.deadline_set (10s);
	auto done (false);
	while (!done)
	{
		auto reps (node1.rep_crawler.representatives (1));
		if (!reps.empty ())
		{
			if (!reps[0].weight.is_zero ())
			{
				done = true;
			}
		}
		ASSERT_NO_ERROR (system.poll ());
	}
}

TEST (node, rep_weight)
{
	scendere::system system;
	auto add_node = [&system] {
		auto node = std::make_shared<scendere::node> (system.io_ctx, scendere::get_available_port (), scendere::unique_path (), system.logging, system.work);
		node->start ();
		system.nodes.push_back (node);
		return node;
	};
	auto & node = *add_node ();
	auto & node1 = *add_node ();
	auto & node2 = *add_node ();
	auto & node3 = *add_node ();
	scendere::keypair keypair1;
	scendere::keypair keypair2;
	scendere::block_builder builder;
	auto amount_pr (node.minimum_principal_weight () + 100);
	auto amount_not_pr (node.minimum_principal_weight () - 100);
	std::shared_ptr<scendere::block> block1 = builder
										  .state ()
										  .account (scendere::dev::genesis_key.pub)
										  .previous (scendere::dev::genesis->hash ())
										  .representative (scendere::dev::genesis_key.pub)
										  .balance (scendere::dev::constants.genesis_amount - amount_not_pr)
										  .link (keypair1.pub)
										  .sign (scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub)
										  .work (*system.work.generate (scendere::dev::genesis->hash ()))
										  .build ();
	std::shared_ptr<scendere::block> block2 = builder
										  .state ()
										  .account (keypair1.pub)
										  .previous (0)
										  .representative (keypair1.pub)
										  .balance (amount_not_pr)
										  .link (block1->hash ())
										  .sign (keypair1.prv, keypair1.pub)
										  .work (*system.work.generate (keypair1.pub))
										  .build ();
	std::shared_ptr<scendere::block> block3 = builder
										  .state ()
										  .account (scendere::dev::genesis_key.pub)
										  .previous (block1->hash ())
										  .representative (scendere::dev::genesis_key.pub)
										  .balance (scendere::dev::constants.genesis_amount - amount_not_pr - amount_pr)
										  .link (keypair2.pub)
										  .sign (scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub)
										  .work (*system.work.generate (block1->hash ()))
										  .build ();
	std::shared_ptr<scendere::block> block4 = builder
										  .state ()
										  .account (keypair2.pub)
										  .previous (0)
										  .representative (keypair2.pub)
										  .balance (amount_pr)
										  .link (block3->hash ())
										  .sign (keypair2.prv, keypair2.pub)
										  .work (*system.work.generate (keypair2.pub))
										  .build ();
	{
		auto transaction = node.store.tx_begin_write ();
		ASSERT_EQ (scendere::process_result::progress, node.ledger.process (transaction, *block1).code);
		ASSERT_EQ (scendere::process_result::progress, node.ledger.process (transaction, *block2).code);
		ASSERT_EQ (scendere::process_result::progress, node.ledger.process (transaction, *block3).code);
		ASSERT_EQ (scendere::process_result::progress, node.ledger.process (transaction, *block4).code);
	}
	ASSERT_TRUE (node.rep_crawler.representatives (1).empty ());
	std::shared_ptr<scendere::transport::channel> channel1 = scendere::establish_tcp (system, node, node1.network.endpoint ());
	ASSERT_NE (nullptr, channel1);
	std::shared_ptr<scendere::transport::channel> channel2 = scendere::establish_tcp (system, node, node2.network.endpoint ());
	ASSERT_NE (nullptr, channel2);
	std::shared_ptr<scendere::transport::channel> channel3 = scendere::establish_tcp (system, node, node3.network.endpoint ());
	ASSERT_NE (nullptr, channel3);
	auto vote0 = std::make_shared<scendere::vote> (scendere::dev::genesis_key.pub, scendere::dev::genesis_key.prv, 0, 0, scendere::dev::genesis);
	auto vote1 = std::make_shared<scendere::vote> (keypair1.pub, keypair1.prv, 0, 0, scendere::dev::genesis);
	auto vote2 = std::make_shared<scendere::vote> (keypair2.pub, keypair2.prv, 0, 0, scendere::dev::genesis);
	node.rep_crawler.response (channel1, vote0);
	node.rep_crawler.response (channel2, vote1);
	node.rep_crawler.response (channel3, vote2);
	ASSERT_TIMELY (5s, node.rep_crawler.representative_count () == 2);
	// Make sure we get the rep with the most weight first
	auto reps (node.rep_crawler.representatives (1));
	ASSERT_EQ (1, reps.size ());
	ASSERT_EQ (node.balance (scendere::dev::genesis_key.pub), reps[0].weight.number ());
	ASSERT_EQ (scendere::dev::genesis_key.pub, reps[0].account);
	ASSERT_EQ (*channel1, reps[0].channel_ref ());
	ASSERT_TRUE (node.rep_crawler.is_pr (*channel1));
	ASSERT_FALSE (node.rep_crawler.is_pr (*channel2));
	ASSERT_TRUE (node.rep_crawler.is_pr (*channel3));
}

TEST (node, rep_remove)
{
	scendere::system system;
	scendere::node_flags node_flags;
	node_flags.disable_udp = false;
	auto & node = *system.add_node (node_flags);
	scendere::keypair keypair1;
	scendere::keypair keypair2;
	scendere::block_builder builder;
	std::shared_ptr<scendere::block> block1 = builder
										  .state ()
										  .account (scendere::dev::genesis_key.pub)
										  .previous (scendere::dev::genesis->hash ())
										  .representative (scendere::dev::genesis_key.pub)
										  .balance (scendere::dev::constants.genesis_amount - node.minimum_principal_weight () * 2)
										  .link (keypair1.pub)
										  .sign (scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub)
										  .work (*system.work.generate (scendere::dev::genesis->hash ()))
										  .build ();
	std::shared_ptr<scendere::block> block2 = builder
										  .state ()
										  .account (keypair1.pub)
										  .previous (0)
										  .representative (keypair1.pub)
										  .balance (node.minimum_principal_weight () * 2)
										  .link (block1->hash ())
										  .sign (keypair1.prv, keypair1.pub)
										  .work (*system.work.generate (keypair1.pub))
										  .build ();
	std::shared_ptr<scendere::block> block3 = builder
										  .state ()
										  .account (scendere::dev::genesis_key.pub)
										  .previous (block1->hash ())
										  .representative (scendere::dev::genesis_key.pub)
										  .balance (scendere::dev::constants.genesis_amount - node.minimum_principal_weight () * 4)
										  .link (keypair2.pub)
										  .sign (scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub)
										  .work (*system.work.generate (block1->hash ()))
										  .build ();
	std::shared_ptr<scendere::block> block4 = builder
										  .state ()
										  .account (keypair2.pub)
										  .previous (0)
										  .representative (keypair2.pub)
										  .balance (node.minimum_principal_weight () * 2)
										  .link (block3->hash ())
										  .sign (keypair2.prv, keypair2.pub)
										  .work (*system.work.generate (keypair2.pub))
										  .build ();
	{
		auto transaction = node.store.tx_begin_write ();
		ASSERT_EQ (scendere::process_result::progress, node.ledger.process (transaction, *block1).code);
		ASSERT_EQ (scendere::process_result::progress, node.ledger.process (transaction, *block2).code);
		ASSERT_EQ (scendere::process_result::progress, node.ledger.process (transaction, *block3).code);
		ASSERT_EQ (scendere::process_result::progress, node.ledger.process (transaction, *block4).code);
	}
	// Add inactive UDP representative channel
	scendere::endpoint endpoint0 (boost::asio::ip::address_v6::loopback (), scendere::test_node_port ());
	std::shared_ptr<scendere::transport::channel> channel0 (std::make_shared<scendere::transport::channel_udp> (node.network.udp_channels, endpoint0, node.network_params.network.protocol_version));
	auto channel_udp = node.network.udp_channels.insert (endpoint0, node.network_params.network.protocol_version);
	ASSERT_NE (channel_udp, nullptr);
	auto vote1 = std::make_shared<scendere::vote> (keypair1.pub, keypair1.prv, 0, 0, scendere::dev::genesis);
	ASSERT_FALSE (node.rep_crawler.response (channel0, vote1));
	ASSERT_TIMELY (5s, node.rep_crawler.representative_count () == 1);
	auto reps (node.rep_crawler.representatives (1));
	ASSERT_EQ (1, reps.size ());
	ASSERT_EQ (node.minimum_principal_weight () * 2, reps[0].weight.number ());
	ASSERT_EQ (keypair1.pub, reps[0].account);
	ASSERT_EQ (*channel0, reps[0].channel_ref ());
	// Modify last_packet_received so the channel is removed faster
	std::chrono::steady_clock::time_point fake_timepoint{};
	node.network.udp_channels.modify (channel_udp, [fake_timepoint] (std::shared_ptr<scendere::transport::channel_udp> const & channel_a) {
		channel_a->set_last_packet_received (fake_timepoint);
	});
	// This UDP channel is not reachable and should timeout
	ASSERT_EQ (1, node.rep_crawler.representative_count ());
	ASSERT_TIMELY (10s, node.rep_crawler.representative_count () == 0);
	// Add working representative
	auto node1 = system.add_node (scendere::node_config (scendere::get_available_port (), system.logging));
	system.wallet (1)->insert_adhoc (scendere::dev::genesis_key.prv);
	auto channel1 (node.network.find_channel (node1->network.endpoint ()));
	ASSERT_NE (nullptr, channel1);
	auto vote2 = std::make_shared<scendere::vote> (scendere::dev::genesis_key.pub, scendere::dev::genesis_key.prv, 0, 0, scendere::dev::genesis);
	node.rep_crawler.response (channel1, vote2);
	ASSERT_TIMELY (10s, node.rep_crawler.representative_count () == 1);
	auto node2 (std::make_shared<scendere::node> (system.io_ctx, scendere::unique_path (), scendere::node_config (scendere::get_available_port (), system.logging), system.work));
	node2->start ();
	std::weak_ptr<scendere::node> node_w (node.shared ());
	auto vote3 = std::make_shared<scendere::vote> (keypair2.pub, keypair2.prv, 0, 0, scendere::dev::genesis);
	node.network.tcp_channels.start_tcp (node2->network.endpoint ());
	std::shared_ptr<scendere::transport::channel> channel2;
	ASSERT_TIMELY (10s, (channel2 = node.network.tcp_channels.find_channel (scendere::transport::map_endpoint_to_tcp (node2->network.endpoint ()))) != nullptr);
	ASSERT_FALSE (node.rep_crawler.response (channel2, vote3));
	ASSERT_TIMELY (10s, node.rep_crawler.representative_count () == 2);
	node2->stop ();
	ASSERT_TIMELY (10s, node.rep_crawler.representative_count () == 1);
	reps = node.rep_crawler.representatives (1);
	ASSERT_EQ (scendere::dev::genesis_key.pub, reps[0].account);
	ASSERT_EQ (1, node.network.size ());
	auto list (node.network.list (1));
	ASSERT_EQ (node1->network.endpoint (), list[0]->get_endpoint ());
}

TEST (node, rep_connection_close)
{
	scendere::system system (2);
	auto & node1 (*system.nodes[0]);
	auto & node2 (*system.nodes[1]);
	// Add working representative (node 2)
	system.wallet (1)->insert_adhoc (scendere::dev::genesis_key.prv);
	ASSERT_TIMELY (10s, node1.rep_crawler.representative_count () == 1);
	node2.stop ();
	// Remove representative with closed channel
	ASSERT_TIMELY (10s, node1.rep_crawler.representative_count () == 0);
}

// Test that nodes can disable representative voting
TEST (node, no_voting)
{
	scendere::system system (1);
	auto & node0 (*system.nodes[0]);
	scendere::node_config node_config (scendere::get_available_port (), system.logging);
	node_config.enable_voting = false;
	system.add_node (node_config);

	auto wallet0 (system.wallet (0));
	auto wallet1 (system.wallet (1));
	// Node1 has a rep
	wallet1->insert_adhoc (scendere::dev::genesis_key.prv);
	scendere::keypair key1;
	wallet1->insert_adhoc (key1.prv);
	// Broadcast a confirm so others should know this is a rep node
	wallet1->send_action (scendere::dev::genesis_key.pub, key1.pub, scendere::Mxrb_ratio);
	ASSERT_TIMELY (10s, node0.active.empty ());
	ASSERT_EQ (0, node0.stats.count (scendere::stat::type::message, scendere::stat::detail::confirm_ack, scendere::stat::dir::in));
}

TEST (node, send_callback)
{
	scendere::system system (1);
	auto & node0 (*system.nodes[0]);
	scendere::keypair key2;
	system.wallet (0)->insert_adhoc (scendere::dev::genesis_key.prv);
	system.wallet (0)->insert_adhoc (key2.prv);
	node0.config.callback_address = "localhost";
	node0.config.callback_port = 8010;
	node0.config.callback_target = "/";
	ASSERT_NE (nullptr, system.wallet (0)->send_action (scendere::dev::genesis_key.pub, key2.pub, node0.config.receive_minimum.number ()));
	ASSERT_TIMELY (10s, node0.balance (key2.pub).is_zero ());
	ASSERT_EQ (std::numeric_limits<scendere::uint128_t>::max () - node0.config.receive_minimum.number (), node0.balance (scendere::dev::genesis_key.pub));
}

TEST (node, balance_observer)
{
	scendere::system system (1);
	auto & node1 (*system.nodes[0]);
	std::atomic<int> balances (0);
	scendere::keypair key;
	node1.observers.account_balance.add ([&key, &balances] (scendere::account const & account_a, bool is_pending) {
		if (key.pub == account_a && is_pending)
		{
			balances++;
		}
		else if (scendere::dev::genesis_key.pub == account_a && !is_pending)
		{
			balances++;
		}
	});
	system.wallet (0)->insert_adhoc (scendere::dev::genesis_key.prv);
	system.wallet (0)->send_action (scendere::dev::genesis_key.pub, key.pub, 1);
	system.deadline_set (10s);
	auto done (false);
	while (!done)
	{
		auto ec = system.poll ();
		done = balances.load () == 2;
		ASSERT_NO_ERROR (ec);
	}
}

TEST (node, bootstrap_connection_scaling)
{
	scendere::system system (1);
	auto & node1 (*system.nodes[0]);
	ASSERT_EQ (34, node1.bootstrap_initiator.connections->target_connections (5000, 1));
	ASSERT_EQ (4, node1.bootstrap_initiator.connections->target_connections (0, 1));
	ASSERT_EQ (64, node1.bootstrap_initiator.connections->target_connections (50000, 1));
	ASSERT_EQ (64, node1.bootstrap_initiator.connections->target_connections (10000000000, 1));
	ASSERT_EQ (32, node1.bootstrap_initiator.connections->target_connections (5000, 0));
	ASSERT_EQ (1, node1.bootstrap_initiator.connections->target_connections (0, 0));
	ASSERT_EQ (64, node1.bootstrap_initiator.connections->target_connections (50000, 0));
	ASSERT_EQ (64, node1.bootstrap_initiator.connections->target_connections (10000000000, 0));
	ASSERT_EQ (36, node1.bootstrap_initiator.connections->target_connections (5000, 2));
	ASSERT_EQ (8, node1.bootstrap_initiator.connections->target_connections (0, 2));
	ASSERT_EQ (64, node1.bootstrap_initiator.connections->target_connections (50000, 2));
	ASSERT_EQ (64, node1.bootstrap_initiator.connections->target_connections (10000000000, 2));
	node1.config.bootstrap_connections = 128;
	ASSERT_EQ (64, node1.bootstrap_initiator.connections->target_connections (0, 1));
	ASSERT_EQ (64, node1.bootstrap_initiator.connections->target_connections (50000, 1));
	ASSERT_EQ (64, node1.bootstrap_initiator.connections->target_connections (0, 2));
	ASSERT_EQ (64, node1.bootstrap_initiator.connections->target_connections (50000, 2));
	node1.config.bootstrap_connections_max = 256;
	ASSERT_EQ (128, node1.bootstrap_initiator.connections->target_connections (0, 1));
	ASSERT_EQ (256, node1.bootstrap_initiator.connections->target_connections (50000, 1));
	ASSERT_EQ (256, node1.bootstrap_initiator.connections->target_connections (0, 2));
	ASSERT_EQ (256, node1.bootstrap_initiator.connections->target_connections (50000, 2));
	node1.config.bootstrap_connections_max = 0;
	ASSERT_EQ (1, node1.bootstrap_initiator.connections->target_connections (0, 1));
	ASSERT_EQ (1, node1.bootstrap_initiator.connections->target_connections (50000, 1));
}

// Test stat counting at both type and detail levels
TEST (node, stat_counting)
{
	scendere::system system (1);
	auto & node1 (*system.nodes[0]);
	node1.stats.add (scendere::stat::type::ledger, scendere::stat::dir::in, 1);
	node1.stats.add (scendere::stat::type::ledger, scendere::stat::dir::in, 5);
	node1.stats.inc (scendere::stat::type::ledger, scendere::stat::dir::in);
	node1.stats.inc (scendere::stat::type::ledger, scendere::stat::detail::send, scendere::stat::dir::in);
	node1.stats.inc (scendere::stat::type::ledger, scendere::stat::detail::send, scendere::stat::dir::in);
	node1.stats.inc (scendere::stat::type::ledger, scendere::stat::detail::receive, scendere::stat::dir::in);
	ASSERT_EQ (10, node1.stats.count (scendere::stat::type::ledger, scendere::stat::dir::in));
	ASSERT_EQ (2, node1.stats.count (scendere::stat::type::ledger, scendere::stat::detail::send, scendere::stat::dir::in));
	ASSERT_EQ (1, node1.stats.count (scendere::stat::type::ledger, scendere::stat::detail::receive, scendere::stat::dir::in));
	node1.stats.add (scendere::stat::type::ledger, scendere::stat::dir::in, 0);
	ASSERT_EQ (10, node1.stats.count (scendere::stat::type::ledger, scendere::stat::dir::in));
}

TEST (node, stat_histogram)
{
	scendere::system system (1);
	auto & node1 (*system.nodes[0]);

	// Specific bins
	node1.stats.define_histogram (scendere::stat::type::vote, scendere::stat::detail::confirm_req, scendere::stat::dir::in, { 1, 6, 10, 16 });
	node1.stats.update_histogram (scendere::stat::type::vote, scendere::stat::detail::confirm_req, scendere::stat::dir::in, 1, 50);
	auto histogram_req (node1.stats.get_histogram (scendere::stat::type::vote, scendere::stat::detail::confirm_req, scendere::stat::dir::in));
	ASSERT_EQ (histogram_req->get_bins ()[0].value, 50);

	// Uniform distribution (12 bins, width 1); also test clamping 100 to the last bin
	node1.stats.define_histogram (scendere::stat::type::vote, scendere::stat::detail::confirm_ack, scendere::stat::dir::in, { 1, 13 }, 12);
	node1.stats.update_histogram (scendere::stat::type::vote, scendere::stat::detail::confirm_ack, scendere::stat::dir::in, 1);
	node1.stats.update_histogram (scendere::stat::type::vote, scendere::stat::detail::confirm_ack, scendere::stat::dir::in, 8, 10);
	node1.stats.update_histogram (scendere::stat::type::vote, scendere::stat::detail::confirm_ack, scendere::stat::dir::in, 100);

	auto histogram_ack (node1.stats.get_histogram (scendere::stat::type::vote, scendere::stat::detail::confirm_ack, scendere::stat::dir::in));
	ASSERT_EQ (histogram_ack->get_bins ()[0].value, 1);
	ASSERT_EQ (histogram_ack->get_bins ()[7].value, 10);
	ASSERT_EQ (histogram_ack->get_bins ()[11].value, 1);

	// Uniform distribution (2 bins, width 5); add 1 to each bin
	node1.stats.define_histogram (scendere::stat::type::vote, scendere::stat::detail::confirm_ack, scendere::stat::dir::out, { 1, 11 }, 2);
	node1.stats.update_histogram (scendere::stat::type::vote, scendere::stat::detail::confirm_ack, scendere::stat::dir::out, 1, 1);
	node1.stats.update_histogram (scendere::stat::type::vote, scendere::stat::detail::confirm_ack, scendere::stat::dir::out, 6, 1);

	auto histogram_ack_out (node1.stats.get_histogram (scendere::stat::type::vote, scendere::stat::detail::confirm_ack, scendere::stat::dir::out));
	ASSERT_EQ (histogram_ack_out->get_bins ()[0].value, 1);
	ASSERT_EQ (histogram_ack_out->get_bins ()[1].value, 1);
}

TEST (node, online_reps)
{
	scendere::system system (1);
	auto & node1 (*system.nodes[0]);
	// 1 sample of minimum weight
	ASSERT_EQ (node1.config.online_weight_minimum, node1.online_reps.trended ());
	auto vote (std::make_shared<scendere::vote> ());
	ASSERT_EQ (0, node1.online_reps.online ());
	node1.online_reps.observe (scendere::dev::genesis_key.pub);
	ASSERT_EQ (scendere::dev::constants.genesis_amount, node1.online_reps.online ());
	// 1 minimum, 1 maximum
	ASSERT_EQ (node1.config.online_weight_minimum, node1.online_reps.trended ());
	node1.online_reps.sample ();
	ASSERT_EQ (scendere::dev::constants.genesis_amount, node1.online_reps.trended ());
	node1.online_reps.clear ();
	// 2 minimum, 1 maximum
	node1.online_reps.sample ();
	ASSERT_EQ (node1.config.online_weight_minimum, node1.online_reps.trended ());
}

namespace scendere
{
TEST (node, online_reps_rep_crawler)
{
	scendere::system system;
	scendere::node_flags flags;
	flags.disable_rep_crawler = true;
	auto & node1 = *system.add_node (flags);
	auto vote = std::make_shared<scendere::vote> (scendere::dev::genesis_key.pub, scendere::dev::genesis_key.prv, scendere::milliseconds_since_epoch (), 0, std::vector<scendere::block_hash>{ scendere::dev::genesis->hash () });
	ASSERT_EQ (0, node1.online_reps.online ());
	// Without rep crawler
	node1.vote_processor.vote_blocking (vote, std::make_shared<scendere::transport::channel_loopback> (node1));
	ASSERT_EQ (0, node1.online_reps.online ());
	// After inserting to rep crawler
	{
		scendere::lock_guard<scendere::mutex> guard (node1.rep_crawler.probable_reps_mutex);
		node1.rep_crawler.active.insert (scendere::dev::genesis->hash ());
	}
	node1.vote_processor.vote_blocking (vote, std::make_shared<scendere::transport::channel_loopback> (node1));
	ASSERT_EQ (scendere::dev::constants.genesis_amount, node1.online_reps.online ());
}
}

TEST (node, online_reps_election)
{
	scendere::system system;
	scendere::node_flags flags;
	flags.disable_rep_crawler = true;
	auto & node1 = *system.add_node (flags);
	// Start election
	scendere::keypair key;
	scendere::state_block_builder builder;
	auto send1 = builder.make_block ()
				 .account (scendere::dev::genesis_key.pub)
				 .previous (scendere::dev::genesis->hash ())
				 .representative (scendere::dev::genesis_key.pub)
				 .balance (scendere::dev::constants.genesis_amount - scendere::Gxrb_ratio)
				 .link (key.pub)
				 .sign (scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub)
				 .work (*node1.work_generate_blocking (scendere::dev::genesis->hash ()))
				 .build_shared ();
	node1.process_active (send1);
	node1.block_processor.flush ();
	node1.scheduler.flush ();
	ASSERT_EQ (1, node1.active.size ());
	// Process vote for ongoing election
	auto vote = std::make_shared<scendere::vote> (scendere::dev::genesis_key.pub, scendere::dev::genesis_key.prv, scendere::milliseconds_since_epoch (), 0, std::vector<scendere::block_hash>{ send1->hash () });
	ASSERT_EQ (0, node1.online_reps.online ());
	node1.vote_processor.vote_blocking (vote, std::make_shared<scendere::transport::channel_loopback> (node1));
	ASSERT_EQ (scendere::dev::constants.genesis_amount - scendere::Gxrb_ratio, node1.online_reps.online ());
}

TEST (node, block_confirm)
{
	std::vector<scendere::transport::transport_type> types{ scendere::transport::transport_type::tcp, scendere::transport::transport_type::udp };
	for (auto & type : types)
	{
		scendere::node_flags node_flags;
		if (type == scendere::transport::transport_type::udp)
		{
			node_flags.disable_tcp_realtime = true;
			node_flags.disable_bootstrap_listener = true;
			node_flags.disable_udp = false;
		}
		scendere::system system (2, type, node_flags);
		auto & node1 (*system.nodes[0]);
		auto & node2 (*system.nodes[1]);
		scendere::keypair key;
		scendere::state_block_builder builder;
		system.wallet (1)->insert_adhoc (scendere::dev::genesis_key.prv);
		auto send1 = builder.make_block ()
					 .account (scendere::dev::genesis_key.pub)
					 .previous (scendere::dev::genesis->hash ())
					 .representative (scendere::dev::genesis_key.pub)
					 .balance (scendere::dev::constants.genesis_amount - scendere::Gxrb_ratio)
					 .link (key.pub)
					 .sign (scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub)
					 .work (*node1.work_generate_blocking (scendere::dev::genesis->hash ()))
					 .build_shared ();
		// A copy is necessary to avoid data races during ledger processing, which sets the sideband
		auto send1_copy = builder.make_block ()
						  .from (*send1)
						  .build_shared ();
		node1.block_processor.add (send1);
		node2.block_processor.add (send1_copy);
		ASSERT_TIMELY (5s, node1.ledger.block_or_pruned_exists (send1->hash ()) && node2.ledger.block_or_pruned_exists (send1_copy->hash ()));
		ASSERT_TRUE (node1.ledger.block_or_pruned_exists (send1->hash ()));
		ASSERT_TRUE (node2.ledger.block_or_pruned_exists (send1_copy->hash ()));
		// Confirm send1 on node2 so it can vote for send2
		node2.block_confirm (send1_copy);
		auto election = node2.active.election (send1_copy->qualified_root ());
		ASSERT_NE (nullptr, election);
		ASSERT_TIMELY (10s, node1.active.list_recently_cemented ().size () == 1);
	}
}

TEST (node, block_arrival)
{
	scendere::system system (1);
	auto & node (*system.nodes[0]);
	ASSERT_EQ (0, node.block_arrival.arrival.size ());
	scendere::block_hash hash1 (1);
	node.block_arrival.add (hash1);
	ASSERT_EQ (1, node.block_arrival.arrival.size ());
	node.block_arrival.add (hash1);
	ASSERT_EQ (1, node.block_arrival.arrival.size ());
	scendere::block_hash hash2 (2);
	node.block_arrival.add (hash2);
	ASSERT_EQ (2, node.block_arrival.arrival.size ());
}

TEST (node, block_arrival_size)
{
	scendere::system system (1);
	auto & node (*system.nodes[0]);
	auto time (std::chrono::steady_clock::now () - scendere::block_arrival::arrival_time_min - std::chrono::seconds (5));
	scendere::block_hash hash (0);
	for (auto i (0); i < scendere::block_arrival::arrival_size_min * 2; ++i)
	{
		node.block_arrival.arrival.push_back (scendere::block_arrival_info{ time, hash });
		++hash.qwords[0];
	}
	ASSERT_EQ (scendere::block_arrival::arrival_size_min * 2, node.block_arrival.arrival.size ());
	node.block_arrival.recent (0);
	ASSERT_EQ (scendere::block_arrival::arrival_size_min, node.block_arrival.arrival.size ());
}

TEST (node, block_arrival_time)
{
	scendere::system system (1);
	auto & node (*system.nodes[0]);
	auto time (std::chrono::steady_clock::now ());
	scendere::block_hash hash (0);
	for (auto i (0); i < scendere::block_arrival::arrival_size_min * 2; ++i)
	{
		node.block_arrival.arrival.push_back (scendere::block_arrival_info{ time, hash });
		++hash.qwords[0];
	}
	ASSERT_EQ (scendere::block_arrival::arrival_size_min * 2, node.block_arrival.arrival.size ());
	node.block_arrival.recent (0);
	ASSERT_EQ (scendere::block_arrival::arrival_size_min * 2, node.block_arrival.arrival.size ());
}

TEST (node, confirm_quorum)
{
	scendere::system system (1);
	auto & node1 = *system.nodes[0];
	system.wallet (0)->insert_adhoc (scendere::dev::genesis_key.prv);
	// Put greater than node.delta () in pending so quorum can't be reached
	scendere::amount new_balance = node1.online_reps.delta () - scendere::Gxrb_ratio;
	auto send1 = scendere::state_block_builder ()
				 .account (scendere::dev::genesis_key.pub)
				 .previous (scendere::dev::genesis->hash ())
				 .representative (scendere::dev::genesis_key.pub)
				 .balance (new_balance)
				 .link (scendere::dev::genesis_key.pub)
				 .sign (scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub)
				 .work (*node1.work_generate_blocking (scendere::dev::genesis->hash ()))
				 .build_shared ();
	ASSERT_EQ (scendere::process_result::progress, node1.process (*send1).code);
	system.wallet (0)->send_action (scendere::dev::genesis_key.pub, scendere::dev::genesis_key.pub, new_balance.number ());
	ASSERT_TIMELY (2s, node1.active.election (send1->qualified_root ()));
	auto election = node1.active.election (send1->qualified_root ());
	ASSERT_NE (nullptr, election);
	ASSERT_FALSE (election->confirmed ());
	ASSERT_EQ (1, election->votes ().size ());
	ASSERT_EQ (0, node1.balance (scendere::dev::genesis_key.pub));
}

TEST (node, local_votes_cache)
{
	scendere::system system;
	scendere::node_config node_config (scendere::get_available_port (), system.logging);
	node_config.frontiers_confirmation = scendere::frontiers_confirmation_mode::disabled;
	node_config.receive_minimum = scendere::dev::constants.genesis_amount;
	auto & node (*system.add_node (node_config));
	scendere::state_block_builder builder;
	auto send1 = builder.make_block ()
				 .account (scendere::dev::genesis_key.pub)
				 .previous (scendere::dev::genesis->hash ())
				 .representative (scendere::dev::genesis_key.pub)
				 .balance (scendere::dev::constants.genesis_amount - scendere::Gxrb_ratio)
				 .link (scendere::dev::genesis_key.pub)
				 .sign (scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub)
				 .work (*node.work_generate_blocking (scendere::dev::genesis->hash ()))
				 .build_shared ();
	auto send2 = builder.make_block ()
				 .account (scendere::dev::genesis_key.pub)
				 .previous (send1->hash ())
				 .representative (scendere::dev::genesis_key.pub)
				 .balance (scendere::dev::constants.genesis_amount - 2 * scendere::Gxrb_ratio)
				 .link (scendere::dev::genesis_key.pub)
				 .sign (scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub)
				 .work (*node.work_generate_blocking (send1->hash ()))
				 .build_shared ();
	auto send3 = builder.make_block ()
				 .account (scendere::dev::genesis_key.pub)
				 .previous (send2->hash ())
				 .representative (scendere::dev::genesis_key.pub)
				 .balance (scendere::dev::constants.genesis_amount - 3 * scendere::Gxrb_ratio)
				 .link (scendere::dev::genesis_key.pub)
				 .sign (scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub)
				 .work (*node.work_generate_blocking (send2->hash ()))
				 .build_shared ();
	{
		auto transaction (node.store.tx_begin_write ());
		ASSERT_EQ (scendere::process_result::progress, node.ledger.process (transaction, *send1).code);
		ASSERT_EQ (scendere::process_result::progress, node.ledger.process (transaction, *send2).code);
	}
	// Confirm blocks to allow voting
	node.block_confirm (send2);
	auto election = node.active.election (send2->qualified_root ());
	ASSERT_NE (nullptr, election);
	election->force_confirm ();
	ASSERT_TIMELY (3s, node.ledger.cache.cemented_count == 3);
	system.wallet (0)->insert_adhoc (scendere::dev::genesis_key.prv);
	scendere::confirm_req message1{ scendere::dev::network_params.network, send1 };
	scendere::confirm_req message2{ scendere::dev::network_params.network, send2 };
	auto channel (node.network.udp_channels.create (node.network.endpoint ()));
	node.network.inbound (message1, channel);
	ASSERT_TIMELY (3s, node.stats.count (scendere::stat::type::requests, scendere::stat::detail::requests_generated_votes) == 1);
	node.network.inbound (message2, channel);
	ASSERT_TIMELY (3s, node.stats.count (scendere::stat::type::requests, scendere::stat::detail::requests_generated_votes) == 2);
	for (auto i (0); i < 100; ++i)
	{
		node.network.inbound (message1, channel);
		node.network.inbound (message2, channel);
	}
	for (int i = 0; i < 4; ++i)
	{
		ASSERT_NO_ERROR (system.poll (node.aggregator.max_delay));
	}
	// Make sure a new vote was not generated
	ASSERT_TIMELY (3s, node.stats.count (scendere::stat::type::requests, scendere::stat::detail::requests_generated_votes) == 2);
	// Max cache
	{
		auto transaction (node.store.tx_begin_write ());
		ASSERT_EQ (scendere::process_result::progress, node.ledger.process (transaction, *send3).code);
	}
	scendere::confirm_req message3{ scendere::dev::network_params.network, send3 };
	for (auto i (0); i < 100; ++i)
	{
		node.network.inbound (message3, channel);
	}
	for (int i = 0; i < 4; ++i)
	{
		ASSERT_NO_ERROR (system.poll (node.aggregator.max_delay));
	}
	ASSERT_TIMELY (3s, node.stats.count (scendere::stat::type::requests, scendere::stat::detail::requests_generated_votes) == 3);
	ASSERT_FALSE (node.history.votes (send1->root (), send1->hash ()).empty ());
	ASSERT_FALSE (node.history.votes (send2->root (), send2->hash ()).empty ());
	ASSERT_FALSE (node.history.votes (send3->root (), send3->hash ()).empty ());
}

// Test disabled because it's failing intermittently.
// PR in which it got disabled: https://github.com/scenderecurrency/scendere-node/pull/3532
// Issue for investigating it: https://github.com/scenderecurrency/scendere-node/issues/3481
TEST (node, DISABLED_local_votes_cache_batch)
{
	scendere::system system;
	scendere::node_config node_config (scendere::get_available_port (), system.logging);
	node_config.frontiers_confirmation = scendere::frontiers_confirmation_mode::disabled;
	auto & node (*system.add_node (node_config));
	ASSERT_GE (node.network_params.voting.max_cache, 2);
	system.wallet (0)->insert_adhoc (scendere::dev::genesis_key.prv);
	scendere::keypair key1;
	auto send1 = scendere::state_block_builder ()
				 .account (scendere::dev::genesis_key.pub)
				 .previous (scendere::dev::genesis->hash ())
				 .representative (scendere::dev::genesis_key.pub)
				 .balance (scendere::dev::constants.genesis_amount - scendere::Gxrb_ratio)
				 .link (key1.pub)
				 .sign (scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub)
				 .work (*node.work_generate_blocking (scendere::dev::genesis->hash ()))
				 .build_shared ();
	ASSERT_EQ (scendere::process_result::progress, node.ledger.process (node.store.tx_begin_write (), *send1).code);
	node.confirmation_height_processor.add (send1);
	ASSERT_TIMELY (5s, node.ledger.block_confirmed (node.store.tx_begin_read (), send1->hash ()));
	auto send2 = scendere::state_block_builder ()
				 .account (scendere::dev::genesis_key.pub)
				 .previous (send1->hash ())
				 .representative (scendere::dev::genesis_key.pub)
				 .balance (scendere::dev::constants.genesis_amount - 2 * scendere::Gxrb_ratio)
				 .link (scendere::dev::genesis_key.pub)
				 .sign (scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub)
				 .work (*node.work_generate_blocking (send1->hash ()))
				 .build_shared ();
	ASSERT_EQ (scendere::process_result::progress, node.ledger.process (node.store.tx_begin_write (), *send2).code);
	auto receive1 = scendere::state_block_builder ()
					.account (key1.pub)
					.previous (0)
					.representative (scendere::dev::genesis_key.pub)
					.balance (scendere::Gxrb_ratio)
					.link (send1->hash ())
					.sign (key1.prv, key1.pub)
					.work (*node.work_generate_blocking (key1.pub))
					.build_shared ();
	ASSERT_EQ (scendere::process_result::progress, node.ledger.process (node.store.tx_begin_write (), *receive1).code);
	std::vector<std::pair<scendere::block_hash, scendere::root>> batch{ { send2->hash (), send2->root () }, { receive1->hash (), receive1->root () } };
	scendere::confirm_req message{ scendere::dev::network_params.network, batch };
	auto channel (node.network.udp_channels.create (node.network.endpoint ()));
	// Generates and sends one vote for both hashes which is then cached
	node.network.inbound (message, channel);
	ASSERT_TIMELY (3s, node.stats.count (scendere::stat::type::message, scendere::stat::detail::confirm_ack, scendere::stat::dir::out) == 1);
	ASSERT_EQ (1, node.stats.count (scendere::stat::type::message, scendere::stat::detail::confirm_ack, scendere::stat::dir::out));
	ASSERT_FALSE (node.history.votes (send2->root (), send2->hash ()).empty ());
	ASSERT_FALSE (node.history.votes (receive1->root (), receive1->hash ()).empty ());
	// Only one confirm_ack should be sent if all hashes are part of the same vote
	node.network.inbound (message, channel);
	ASSERT_TIMELY (3s, node.stats.count (scendere::stat::type::message, scendere::stat::detail::confirm_ack, scendere::stat::dir::out) == 2);
	ASSERT_EQ (2, node.stats.count (scendere::stat::type::message, scendere::stat::detail::confirm_ack, scendere::stat::dir::out));
	// Test when votes are different
	node.history.erase (send2->root ());
	node.history.erase (receive1->root ());
	node.network.inbound (scendere::confirm_req{ scendere::dev::network_params.network, send2->hash (), send2->root () }, channel);
	ASSERT_TIMELY (3s, node.stats.count (scendere::stat::type::message, scendere::stat::detail::confirm_ack, scendere::stat::dir::out) == 3);
	ASSERT_EQ (3, node.stats.count (scendere::stat::type::message, scendere::stat::detail::confirm_ack, scendere::stat::dir::out));
	node.network.inbound (scendere::confirm_req{ scendere::dev::network_params.network, receive1->hash (), receive1->root () }, channel);
	ASSERT_TIMELY (3s, node.stats.count (scendere::stat::type::message, scendere::stat::detail::confirm_ack, scendere::stat::dir::out) == 4);
	ASSERT_EQ (4, node.stats.count (scendere::stat::type::message, scendere::stat::detail::confirm_ack, scendere::stat::dir::out));
	// There are two different votes, so both should be sent in response
	node.network.inbound (message, channel);
	ASSERT_TIMELY (3s, node.stats.count (scendere::stat::type::message, scendere::stat::detail::confirm_ack, scendere::stat::dir::out) == 6);
	ASSERT_EQ (6, node.stats.count (scendere::stat::type::message, scendere::stat::detail::confirm_ack, scendere::stat::dir::out));
}

TEST (node, local_votes_cache_generate_new_vote)
{
	scendere::system system;
	scendere::node_config node_config (scendere::get_available_port (), system.logging);
	node_config.frontiers_confirmation = scendere::frontiers_confirmation_mode::disabled;
	auto & node (*system.add_node (node_config));
	system.wallet (0)->insert_adhoc (scendere::dev::genesis_key.prv);
	// Repsond with cached vote
	scendere::confirm_req message1{ scendere::dev::network_params.network, scendere::dev::genesis };
	auto channel (node.network.udp_channels.create (node.network.endpoint ()));
	node.network.inbound (message1, channel);
	ASSERT_TIMELY (3s, !node.history.votes (scendere::dev::genesis->root (), scendere::dev::genesis->hash ()).empty ());
	auto votes1 (node.history.votes (scendere::dev::genesis->root (), scendere::dev::genesis->hash ()));
	ASSERT_EQ (1, votes1.size ());
	ASSERT_EQ (1, votes1[0]->blocks.size ());
	ASSERT_EQ (scendere::dev::genesis->hash (), boost::get<scendere::block_hash> (votes1[0]->blocks[0]));
	ASSERT_TIMELY (3s, node.stats.count (scendere::stat::type::requests, scendere::stat::detail::requests_generated_votes) == 1);
	auto send1 = scendere::state_block_builder ()
				 .account (scendere::dev::genesis_key.pub)
				 .previous (scendere::dev::genesis->hash ())
				 .representative (scendere::dev::genesis_key.pub)
				 .balance (scendere::dev::constants.genesis_amount - scendere::Gxrb_ratio)
				 .link (scendere::dev::genesis_key.pub)
				 .sign (scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub)
				 .work (*node.work_generate_blocking (scendere::dev::genesis->hash ()))
				 .build_shared ();
	ASSERT_EQ (scendere::process_result::progress, node.process (*send1).code);
	// One of the hashes is cached
	std::vector<std::pair<scendere::block_hash, scendere::root>> roots_hashes{ std::make_pair (scendere::dev::genesis->hash (), scendere::dev::genesis->root ()), std::make_pair (send1->hash (), send1->root ()) };
	scendere::confirm_req message2{ scendere::dev::network_params.network, roots_hashes };
	node.network.inbound (message2, channel);
	ASSERT_TIMELY (3s, !node.history.votes (send1->root (), send1->hash ()).empty ());
	auto votes2 (node.history.votes (send1->root (), send1->hash ()));
	ASSERT_EQ (1, votes2.size ());
	ASSERT_EQ (1, votes2[0]->blocks.size ());
	ASSERT_TIMELY (3s, node.stats.count (scendere::stat::type::requests, scendere::stat::detail::requests_generated_votes) == 2);
	ASSERT_FALSE (node.history.votes (scendere::dev::genesis->root (), scendere::dev::genesis->hash ()).empty ());
	ASSERT_FALSE (node.history.votes (send1->root (), send1->hash ()).empty ());
	// First generated + again cached + new generated
	ASSERT_TIMELY (3s, 3 == node.stats.count (scendere::stat::type::message, scendere::stat::detail::confirm_ack, scendere::stat::dir::out));
}

TEST (node, local_votes_cache_fork)
{
	scendere::system system;
	scendere::node_flags node_flags;
	node_flags.disable_bootstrap_bulk_push_client = true;
	node_flags.disable_bootstrap_bulk_pull_server = true;
	node_flags.disable_bootstrap_listener = true;
	node_flags.disable_lazy_bootstrap = true;
	node_flags.disable_legacy_bootstrap = true;
	node_flags.disable_wallet_bootstrap = true;
	scendere::node_config node_config (scendere::get_available_port (), system.logging);
	node_config.frontiers_confirmation = scendere::frontiers_confirmation_mode::disabled;
	auto & node1 (*system.add_node (node_config, node_flags));
	system.wallet (0)->insert_adhoc (scendere::dev::genesis_key.prv);
	auto send1 = scendere::state_block_builder ()
				 .account (scendere::dev::genesis_key.pub)
				 .previous (scendere::dev::genesis->hash ())
				 .representative (scendere::dev::genesis_key.pub)
				 .balance (scendere::dev::constants.genesis_amount - scendere::Gxrb_ratio)
				 .link (scendere::dev::genesis_key.pub)
				 .sign (scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub)
				 .work (*node1.work_generate_blocking (scendere::dev::genesis->hash ()))
				 .build_shared ();
	auto send1_fork = scendere::state_block_builder ()
					  .account (scendere::dev::genesis_key.pub)
					  .previous (scendere::dev::genesis->hash ())
					  .representative (scendere::dev::genesis_key.pub)
					  .balance (scendere::dev::constants.genesis_amount - 2 * scendere::Gxrb_ratio)
					  .link (scendere::dev::genesis_key.pub)
					  .sign (scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub)
					  .work (*node1.work_generate_blocking (scendere::dev::genesis->hash ()))
					  .build_shared ();
	ASSERT_EQ (scendere::process_result::progress, node1.process (*send1).code);
	// Cache vote
	auto vote (std::make_shared<scendere::vote> (scendere::dev::genesis_key.pub, scendere::dev::genesis_key.prv, 0, 0, std::vector<scendere::block_hash> (1, send1->hash ())));
	node1.vote_processor.vote (vote, std::make_shared<scendere::transport::channel_loopback> (node1));
	node1.history.add (send1->root (), send1->hash (), vote);
	auto votes2 (node1.history.votes (send1->root (), send1->hash ()));
	ASSERT_EQ (1, votes2.size ());
	ASSERT_EQ (1, votes2[0]->blocks.size ());
	// Start election for forked block
	node_config.peering_port = scendere::get_available_port ();
	auto & node2 (*system.add_node (node_config, node_flags));
	node2.process_active (send1_fork);
	node2.block_processor.flush ();
	ASSERT_TIMELY (5s, node2.ledger.block_or_pruned_exists (send1->hash ()));
}

TEST (node, vote_republish)
{
	scendere::system system (2);
	auto & node1 (*system.nodes[0]);
	auto & node2 (*system.nodes[1]);
	scendere::keypair key2;
	// by not setting a private key on node1's wallet, it is stopped from voting
	system.wallet (1)->insert_adhoc (key2.prv);
	scendere::send_block_builder builder;
	auto send1 = builder.make_block ()
				 .previous (scendere::dev::genesis->hash ())
				 .destination (key2.pub)
				 .balance (std::numeric_limits<scendere::uint128_t>::max () - node1.config.receive_minimum.number ())
				 .sign (scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub)
				 .work (*system.work.generate (scendere::dev::genesis->hash ()))
				 .build_shared ();
	auto send2 = builder.make_block ()
				 .previous (scendere::dev::genesis->hash ())
				 .destination (key2.pub)
				 .balance (std::numeric_limits<scendere::uint128_t>::max () - node1.config.receive_minimum.number () * 2)
				 .sign (scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub)
				 .work (*system.work.generate (scendere::dev::genesis->hash ()))
				 .build_shared ();
	node1.process_active (send1);
	ASSERT_TIMELY (5s, node2.block (send1->hash ()));
	node1.process_active (send2);
	auto vote (std::make_shared<scendere::vote> (scendere::dev::genesis_key.pub, scendere::dev::genesis_key.prv, scendere::vote::timestamp_max, scendere::vote::duration_max, send2));
	ASSERT_TRUE (node1.active.active (*send1));
	ASSERT_TIMELY (10s, node2.active.active (*send1));
	node1.vote_processor.vote (vote, std::make_shared<scendere::transport::channel_loopback> (node1));
	ASSERT_TIMELY (10s, node1.block (send2->hash ()));
	ASSERT_TIMELY (10s, node2.block (send2->hash ()));
	ASSERT_FALSE (node1.block (send1->hash ()));
	ASSERT_FALSE (node2.block (send1->hash ()));
	ASSERT_TIMELY (10s, node2.balance (key2.pub) == node1.config.receive_minimum.number () * 2);
	ASSERT_TIMELY (10s, node1.balance (key2.pub) == node1.config.receive_minimum.number () * 2);
}

TEST (node, vote_by_hash_bundle)
{
	// Keep max_hashes above system to ensure it is kept in scope as votes can be added during system destruction
	std::atomic<size_t> max_hashes{ 0 };
	scendere::system system (1);
	auto & node = *system.nodes[0];
	scendere::state_block_builder builder;
	std::vector<std::shared_ptr<scendere::state_block>> blocks;
	auto block = builder.make_block ()
				 .account (scendere::dev::genesis_key.pub)
				 .previous (scendere::dev::genesis->hash ())
				 .representative (scendere::dev::genesis_key.pub)
				 .balance (scendere::dev::constants.genesis_amount - 1)
				 .link (scendere::dev::genesis_key.pub)
				 .sign (scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub)
				 .work (*system.work.generate (scendere::dev::genesis->hash ()))
				 .build_shared ();
	blocks.push_back (block);
	ASSERT_EQ (scendere::process_result::progress, node.ledger.process (node.store.tx_begin_write (), *blocks.back ()).code);
	for (auto i = 2; i < 200; ++i)
	{
		auto block = builder.make_block ()
					 .from (*blocks.back ())
					 .previous (blocks.back ()->hash ())
					 .balance (scendere::dev::constants.genesis_amount - i)
					 .sign (scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub)
					 .work (*system.work.generate (blocks.back ()->hash ()))
					 .build_shared ();
		blocks.push_back (block);
		ASSERT_EQ (scendere::process_result::progress, node.ledger.process (node.store.tx_begin_write (), *blocks.back ()).code);
	}
	node.block_confirm (blocks.back ());
	auto election = node.active.election (blocks.back ()->qualified_root ());
	ASSERT_NE (nullptr, election);
	election->force_confirm ();
	system.wallet (0)->insert_adhoc (scendere::dev::genesis_key.prv);
	scendere::keypair key1;
	system.wallet (0)->insert_adhoc (key1.prv);

	system.nodes[0]->observers.vote.add ([&max_hashes] (std::shared_ptr<scendere::vote> const & vote_a, std::shared_ptr<scendere::transport::channel> const &, scendere::vote_code) {
		if (vote_a->blocks.size () > max_hashes)
		{
			max_hashes = vote_a->blocks.size ();
		}
	});

	for (auto const & block : blocks)
	{
		system.nodes[0]->active.generator.add (block->root (), block->hash ());
	}

	// Verify that bundling occurs. While reaching 12 should be common on most hardware in release mode,
	// we set this low enough to allow the test to pass on CI/with sanitizers.
	ASSERT_TIMELY (20s, max_hashes.load () >= 3);
}

TEST (node, vote_by_hash_republish)
{
	scendere::system system{ 2 };
	auto & node1 = *system.nodes[0];
	auto & node2 = *system.nodes[1];
	scendere::keypair key2;
	system.wallet (1)->insert_adhoc (key2.prv);
	scendere::send_block_builder builder;
	auto send1 = builder.make_block ()
				 .previous (scendere::dev::genesis->hash ())
				 .destination (key2.pub)
				 .balance (std::numeric_limits<scendere::uint128_t>::max () - node1.config.receive_minimum.number ())
				 .sign (scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub)
				 .work (*system.work.generate (scendere::dev::genesis->hash ()))
				 .build_shared ();
	auto send2 = builder.make_block ()
				 .previous (scendere::dev::genesis->hash ())
				 .destination (key2.pub)
				 .balance (std::numeric_limits<scendere::uint128_t>::max () - node1.config.receive_minimum.number () * 2)
				 .sign (scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub)
				 .work (*system.work.generate (scendere::dev::genesis->hash ()))
				 .build_shared ();
	node1.process_active (send1);
	ASSERT_TIMELY (5s, node2.active.active (*send1));
	node1.process_active (send2);
	std::vector<scendere::block_hash> vote_blocks;
	vote_blocks.push_back (send2->hash ());
	auto vote = std::make_shared<scendere::vote> (scendere::dev::genesis_key.pub, scendere::dev::genesis_key.prv, scendere::vote::timestamp_max, scendere::vote::duration_max, vote_blocks); // Final vote for confirmation
	ASSERT_TRUE (node1.active.active (*send1));
	ASSERT_TRUE (node2.active.active (*send1));
	node1.vote_processor.vote (vote, std::make_shared<scendere::transport::channel_loopback> (node1));
	ASSERT_TIMELY (10s, node1.block (send2->hash ()));
	ASSERT_TIMELY (10s, node2.block (send2->hash ()));
	ASSERT_FALSE (node1.block (send1->hash ()));
	ASSERT_FALSE (node2.block (send1->hash ()));
	ASSERT_TIMELY (5s, node2.balance (key2.pub) == node1.config.receive_minimum.number () * 2);
	ASSERT_TIMELY (10s, node1.balance (key2.pub) == node1.config.receive_minimum.number () * 2);
}

// Test disabled because it's failing intermittently.
// PR in which it got disabled: https://github.com/scenderecurrency/scendere-node/pull/3629
// Issue for investigating it: https://github.com/scenderecurrency/scendere-node/issues/3638
TEST (node, DISABLED_vote_by_hash_epoch_block_republish)
{
	scendere::system system (2);
	auto & node1 (*system.nodes[0]);
	auto & node2 (*system.nodes[1]);
	scendere::keypair key2;
	system.wallet (1)->insert_adhoc (key2.prv);
	auto send1 = scendere::send_block_builder ()
				 .previous (scendere::dev::genesis->hash ())
				 .destination (key2.pub)
				 .balance (std::numeric_limits<scendere::uint128_t>::max () - node1.config.receive_minimum.number ())
				 .sign (scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub)
				 .work (*system.work.generate (scendere::dev::genesis->hash ()))
				 .build_shared ();
	auto epoch1 = scendere::state_block_builder ()
				  .account (scendere::dev::genesis->account ())
				  .previous (scendere::dev::genesis->hash ())
				  .representative (scendere::dev::genesis->account ())
				  .balance (scendere::dev::constants.genesis_amount)
				  .link (node1.ledger.epoch_link (scendere::epoch::epoch_1))
				  .sign (scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub)
				  .work (*system.work.generate (scendere::dev::genesis->hash ()))
				  .build_shared ();
	node1.process_active (send1);
	ASSERT_TIMELY (5s, node2.active.active (*send1));
	node1.active.publish (epoch1);
	std::vector<scendere::block_hash> vote_blocks;
	vote_blocks.push_back (epoch1->hash ());
	auto vote (std::make_shared<scendere::vote> (scendere::dev::genesis_key.pub, scendere::dev::genesis_key.prv, 0, 0, vote_blocks));
	ASSERT_TRUE (node1.active.active (*send1));
	ASSERT_TRUE (node2.active.active (*send1));
	node1.vote_processor.vote (vote, std::make_shared<scendere::transport::channel_loopback> (node1));
	ASSERT_TIMELY (10s, node1.block (epoch1->hash ()));
	ASSERT_TIMELY (10s, node2.block (epoch1->hash ()));
	ASSERT_FALSE (node1.block (send1->hash ()));
	ASSERT_FALSE (node2.block (send1->hash ()));
}

TEST (node, epoch_conflict_confirm)
{
	scendere::system system;
	scendere::node_config node_config (scendere::get_available_port (), system.logging);
	node_config.frontiers_confirmation = scendere::frontiers_confirmation_mode::disabled;
	auto node0 = system.add_node (node_config);
	node_config.peering_port = scendere::get_available_port ();
	auto node1 = system.add_node (node_config);
	scendere::keypair key;
	scendere::keypair epoch_signer (scendere::dev::genesis_key);
	scendere::state_block_builder builder;
	auto send = builder.make_block ()
				.account (scendere::dev::genesis_key.pub)
				.previous (scendere::dev::genesis->hash ())
				.representative (scendere::dev::genesis_key.pub)
				.balance (scendere::dev::constants.genesis_amount - 1)
				.link (key.pub)
				.sign (scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub)
				.work (*system.work.generate (scendere::dev::genesis->hash ()))
				.build_shared ();
	auto open = builder.make_block ()
				.account (key.pub)
				.previous (0)
				.representative (key.pub)
				.balance (1)
				.link (send->hash ())
				.sign (key.prv, key.pub)
				.work (*system.work.generate (key.pub))
				.build_shared ();
	auto change = builder.make_block ()
				  .account (key.pub)
				  .previous (open->hash ())
				  .representative (key.pub)
				  .balance (1)
				  .link (0)
				  .sign (key.prv, key.pub)
				  .work (*system.work.generate (open->hash ()))
				  .build_shared ();
	auto send2 = builder.make_block ()
				 .account (scendere::dev::genesis_key.pub)
				 .previous (send->hash ())
				 .representative (scendere::dev::genesis_key.pub)
				 .balance (scendere::dev::constants.genesis_amount - 2)
				 .link (open->hash ())
				 .sign (scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub)
				 .work (*system.work.generate (send->hash ()))
				 .build_shared ();
	auto epoch_open = builder.make_block ()
					  .account (change->root ().as_account ())
					  .previous (0)
					  .representative (0)
					  .balance (0)
					  .link (node0->ledger.epoch_link (scendere::epoch::epoch_1))
					  .sign (epoch_signer.prv, epoch_signer.pub)
					  .work (*system.work.generate (open->hash ()))
					  .build_shared ();
	ASSERT_EQ (scendere::process_result::progress, node1->process (*send).code);
	ASSERT_EQ (scendere::process_result::progress, node1->process (*send2).code);
	ASSERT_EQ (scendere::process_result::progress, node1->process (*open).code);
	// Confirm block in node1 to allow generating votes
	node1->block_confirm (open);
	auto election (node1->active.election (open->qualified_root ()));
	ASSERT_NE (nullptr, election);
	election->force_confirm ();
	ASSERT_TIMELY (3s, node1->block_confirmed (open->hash ()));
	ASSERT_EQ (scendere::process_result::progress, node0->process (*send).code);
	ASSERT_EQ (scendere::process_result::progress, node0->process (*send2).code);
	ASSERT_EQ (scendere::process_result::progress, node0->process (*open).code);
	node0->process_active (change);
	node0->process_active (epoch_open);
	ASSERT_TIMELY (10s, node0->block (change->hash ()) && node0->block (epoch_open->hash ()) && node1->block (change->hash ()) && node1->block (epoch_open->hash ()));
	// Confirm blocks in node1 to allow generating votes
	scendere::blocks_confirm (*node1, { change, epoch_open }, true /* forced */);
	ASSERT_TIMELY (3s, node1->block_confirmed (change->hash ()) && node1->block_confirmed (epoch_open->hash ()));
	// Start elections for node0
	scendere::blocks_confirm (*node0, { change, epoch_open });
	ASSERT_EQ (2, node0->active.size ());
	{
		scendere::lock_guard<scendere::mutex> lock (node0->active.mutex);
		ASSERT_TRUE (node0->active.blocks.find (change->hash ()) != node0->active.blocks.end ());
		ASSERT_TRUE (node0->active.blocks.find (epoch_open->hash ()) != node0->active.blocks.end ());
	}
	system.wallet (1)->insert_adhoc (scendere::dev::genesis_key.prv);
	ASSERT_TIMELY (5s, node0->active.election (change->qualified_root ()) == nullptr);
	ASSERT_TIMELY (5s, node0->active.empty ());
	{
		auto transaction (node0->store.tx_begin_read ());
		ASSERT_TRUE (node0->ledger.store.block.exists (transaction, change->hash ()));
		ASSERT_TRUE (node0->ledger.store.block.exists (transaction, epoch_open->hash ()));
	}
}

// Test disabled because it's failing intermittently.
// PR in which it got disabled: https://github.com/scenderecurrency/scendere-node/pull/3526
// Issue for investigating it: https://github.com/scenderecurrency/scendere-node/issues/3527
TEST (node, DISABLED_fork_invalid_block_signature)
{
	scendere::system system;
	scendere::node_flags node_flags;
	// Disabling republishing + waiting for a rollback before sending the correct vote below fixes an intermittent failure in this test
	// If these are taken out, one of two things may cause the test two fail often:
	// - Block *send2* might get processed before the rollback happens, simply due to timings, with code "fork", and not be processed again. Waiting for the rollback fixes this issue.
	// - Block *send1* might get processed again after the rollback happens, which causes *send2* to be processed with code "fork". Disabling block republishing ensures "send1" is not processed again.
	// An alternative would be to repeatedly flood the correct vote
	node_flags.disable_block_processor_republishing = true;
	auto & node1 (*system.add_node (node_flags));
	auto & node2 (*system.add_node (node_flags));
	scendere::keypair key2;
	scendere::send_block_builder builder;
	auto send1 = builder.make_block ()
				 .previous (scendere::dev::genesis->hash ())
				 .destination (key2.pub)
				 .balance (std::numeric_limits<scendere::uint128_t>::max () - node1.config.receive_minimum.number ())
				 .sign (scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub)
				 .work (*system.work.generate (scendere::dev::genesis->hash ()))
				 .build_shared ();
	auto send2 = builder.make_block ()
				 .previous (scendere::dev::genesis->hash ())
				 .destination (key2.pub)
				 .balance (std::numeric_limits<scendere::uint128_t>::max () - node1.config.receive_minimum.number () * 2)
				 .sign (scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub)
				 .work (*system.work.generate (scendere::dev::genesis->hash ()))
				 .build_shared ();
	auto send2_corrupt (std::make_shared<scendere::send_block> (*send2));
	send2_corrupt->signature = scendere::signature (123);
	auto vote (std::make_shared<scendere::vote> (scendere::dev::genesis_key.pub, scendere::dev::genesis_key.prv, 0, 0, send2));
	auto vote_corrupt (std::make_shared<scendere::vote> (scendere::dev::genesis_key.pub, scendere::dev::genesis_key.prv, 0, 0, send2_corrupt));

	node1.process_active (send1);
	ASSERT_TIMELY (5s, node1.block (send1->hash ()));
	// Send the vote with the corrupt block signature
	node2.network.flood_vote (vote_corrupt, 1.0f);
	// Wait for the rollback
	ASSERT_TIMELY (5s, node1.stats.count (scendere::stat::type::rollback, scendere::stat::detail::all));
	// Send the vote with the correct block
	node2.network.flood_vote (vote, 1.0f);
	ASSERT_TIMELY (10s, !node1.block (send1->hash ()));
	ASSERT_TIMELY (10s, node1.block (send2->hash ()));
	ASSERT_EQ (node1.block (send2->hash ())->block_signature (), send2->block_signature ());
}

TEST (node, fork_election_invalid_block_signature)
{
	scendere::system system (1);
	auto & node1 (*system.nodes[0]);
	scendere::block_builder builder;
	auto send1 = builder.state ()
				 .account (scendere::dev::genesis_key.pub)
				 .previous (scendere::dev::genesis->hash ())
				 .representative (scendere::dev::genesis_key.pub)
				 .balance (scendere::dev::constants.genesis_amount - scendere::Gxrb_ratio)
				 .link (scendere::dev::genesis_key.pub)
				 .work (*system.work.generate (scendere::dev::genesis->hash ()))
				 .sign (scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub)
				 .build_shared ();
	auto send2 = builder.state ()
				 .account (scendere::dev::genesis_key.pub)
				 .previous (scendere::dev::genesis->hash ())
				 .representative (scendere::dev::genesis_key.pub)
				 .balance (scendere::dev::constants.genesis_amount - 2 * scendere::Gxrb_ratio)
				 .link (scendere::dev::genesis_key.pub)
				 .work (*system.work.generate (scendere::dev::genesis->hash ()))
				 .sign (scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub)
				 .build_shared ();
	auto send3 = builder.state ()
				 .account (scendere::dev::genesis_key.pub)
				 .previous (scendere::dev::genesis->hash ())
				 .representative (scendere::dev::genesis_key.pub)
				 .balance (scendere::dev::constants.genesis_amount - 2 * scendere::Gxrb_ratio)
				 .link (scendere::dev::genesis_key.pub)
				 .work (*system.work.generate (scendere::dev::genesis->hash ()))
				 .sign (scendere::dev::genesis_key.prv, 0) // Invalid signature
				 .build_shared ();
	auto channel1 (node1.network.udp_channels.create (node1.network.endpoint ()));
	node1.network.inbound (scendere::publish{ scendere::dev::network_params.network, send1 }, channel1);
	ASSERT_TIMELY (5s, node1.active.active (send1->qualified_root ()));
	auto election (node1.active.election (send1->qualified_root ()));
	ASSERT_NE (nullptr, election);
	ASSERT_EQ (1, election->blocks ().size ());
	node1.network.inbound (scendere::publish{ scendere::dev::network_params.network, send3 }, channel1);
	node1.network.inbound (scendere::publish{ scendere::dev::network_params.network, send2 }, channel1);
	ASSERT_TIMELY (3s, election->blocks ().size () > 1);
	ASSERT_EQ (election->blocks ()[send2->hash ()]->block_signature (), send2->block_signature ());
}

TEST (node, block_processor_signatures)
{
	scendere::system system0 (1);
	auto & node1 (*system0.nodes[0]);
	system0.wallet (0)->insert_adhoc (scendere::dev::genesis_key.prv);
	scendere::block_hash latest (system0.nodes[0]->latest (scendere::dev::genesis_key.pub));
	scendere::state_block_builder builder;
	scendere::keypair key1;
	scendere::keypair key2;
	scendere::keypair key3;
	auto send1 = builder.make_block ()
				 .account (scendere::dev::genesis_key.pub)
				 .previous (latest)
				 .representative (scendere::dev::genesis_key.pub)
				 .balance (scendere::dev::constants.genesis_amount - scendere::Gxrb_ratio)
				 .link (key1.pub)
				 .sign (scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub)
				 .work (*node1.work_generate_blocking (latest))
				 .build_shared ();
	auto send2 = builder.make_block ()
				 .account (scendere::dev::genesis_key.pub)
				 .previous (send1->hash ())
				 .representative (scendere::dev::genesis_key.pub)
				 .balance (scendere::dev::constants.genesis_amount - 2 * scendere::Gxrb_ratio)
				 .link (key2.pub)
				 .sign (scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub)
				 .work (*node1.work_generate_blocking (send1->hash ()))
				 .build_shared ();
	auto send3 = builder.make_block ()
				 .account (scendere::dev::genesis_key.pub)
				 .previous (send2->hash ())
				 .representative (scendere::dev::genesis_key.pub)
				 .balance (scendere::dev::constants.genesis_amount - 3 * scendere::Gxrb_ratio)
				 .link (key3.pub)
				 .sign (scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub)
				 .work (*node1.work_generate_blocking (send2->hash ()))
				 .build_shared ();
	// Invalid signature bit
	auto send4 = builder.make_block ()
				 .account (scendere::dev::genesis_key.pub)
				 .previous (send3->hash ())
				 .representative (scendere::dev::genesis_key.pub)
				 .balance (scendere::dev::constants.genesis_amount - 4 * scendere::Gxrb_ratio)
				 .link (key3.pub)
				 .sign (scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub)
				 .work (*node1.work_generate_blocking (send3->hash ()))
				 .build_shared ();
	send4->signature.bytes[32] ^= 0x1;
	// Invalid signature bit (force)
	std::shared_ptr<scendere::block> send5 = builder.make_block ()
										 .account (scendere::dev::genesis_key.pub)
										 .previous (send3->hash ())
										 .representative (scendere::dev::genesis_key.pub)
										 .balance (scendere::dev::constants.genesis_amount - 5 * scendere::Gxrb_ratio)
										 .link (key3.pub)
										 .sign (scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub)
										 .work (*node1.work_generate_blocking (send3->hash ()))
										 .build_shared ();
	auto signature = send5->block_signature ();
	signature.bytes[31] ^= 0x1;
	send5->signature_set (signature);
	// Invalid signature to unchecked
	{
		auto transaction (node1.store.tx_begin_write ());
		node1.unchecked.put (send5->previous (), send5);
	}
	auto receive1 = builder.make_block ()
					.account (key1.pub)
					.previous (0)
					.representative (scendere::dev::genesis_key.pub)
					.balance (scendere::Gxrb_ratio)
					.link (send1->hash ())
					.sign (key1.prv, key1.pub)
					.work (*node1.work_generate_blocking (key1.pub))
					.build_shared ();
	auto receive2 = builder.make_block ()
					.account (key2.pub)
					.previous (0)
					.representative (scendere::dev::genesis_key.pub)
					.balance (scendere::Gxrb_ratio)
					.link (send2->hash ())
					.sign (key2.prv, key2.pub)
					.work (*node1.work_generate_blocking (key2.pub))
					.build_shared ();
	// Invalid private key
	auto receive3 = builder.make_block ()
					.account (key3.pub)
					.previous (0)
					.representative (scendere::dev::genesis_key.pub)
					.balance (scendere::Gxrb_ratio)
					.link (send3->hash ())
					.sign (key2.prv, key3.pub)
					.work (*node1.work_generate_blocking (key3.pub))
					.build_shared ();
	node1.process_active (send1);
	node1.process_active (send2);
	node1.process_active (send3);
	node1.process_active (send4);
	node1.process_active (receive1);
	node1.process_active (receive2);
	node1.process_active (receive3);
	node1.block_processor.flush ();
	node1.block_processor.force (send5);
	node1.block_processor.flush ();
	auto transaction (node1.store.tx_begin_read ());
	ASSERT_TRUE (node1.store.block.exists (transaction, send1->hash ()));
	ASSERT_TRUE (node1.store.block.exists (transaction, send2->hash ()));
	ASSERT_TRUE (node1.store.block.exists (transaction, send3->hash ()));
	ASSERT_FALSE (node1.store.block.exists (transaction, send4->hash ()));
	ASSERT_FALSE (node1.store.block.exists (transaction, send5->hash ()));
	ASSERT_TRUE (node1.store.block.exists (transaction, receive1->hash ()));
	ASSERT_TRUE (node1.store.block.exists (transaction, receive2->hash ()));
	ASSERT_FALSE (node1.store.block.exists (transaction, receive3->hash ()));
}

/*
 *  State blocks go through a different signature path, ensure invalidly signed state blocks are rejected
 *  This test can freeze if the wake conditions in block_processor::flush are off, for that reason this is done async here
 */
TEST (node, block_processor_reject_state)
{
	scendere::system system (1);
	auto & node (*system.nodes[0]);
	scendere::state_block_builder builder;
	auto send1 = builder.make_block ()
				 .account (scendere::dev::genesis_key.pub)
				 .previous (scendere::dev::genesis->hash ())
				 .representative (scendere::dev::genesis_key.pub)
				 .balance (scendere::dev::constants.genesis_amount - scendere::Gxrb_ratio)
				 .link (scendere::dev::genesis_key.pub)
				 .sign (scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub)
				 .work (*node.work_generate_blocking (scendere::dev::genesis->hash ()))
				 .build_shared ();
	send1->signature.bytes[0] ^= 1;
	ASSERT_FALSE (node.ledger.block_or_pruned_exists (send1->hash ()));
	node.process_active (send1);
	auto flushed = std::async (std::launch::async, [&node] { node.block_processor.flush (); });
	ASSERT_NE (std::future_status::timeout, flushed.wait_for (5s));
	ASSERT_FALSE (node.ledger.block_or_pruned_exists (send1->hash ()));
	auto send2 = builder.make_block ()
				 .account (scendere::dev::genesis_key.pub)
				 .previous (scendere::dev::genesis->hash ())
				 .representative (scendere::dev::genesis_key.pub)
				 .balance (scendere::dev::constants.genesis_amount - 2 * scendere::Gxrb_ratio)
				 .link (scendere::dev::genesis_key.pub)
				 .sign (scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub)
				 .work (*node.work_generate_blocking (scendere::dev::genesis->hash ()))
				 .build_shared ();
	node.process_active (send2);
	auto flushed2 = std::async (std::launch::async, [&node] { node.block_processor.flush (); });
	ASSERT_NE (std::future_status::timeout, flushed2.wait_for (5s));
	ASSERT_TRUE (node.ledger.block_or_pruned_exists (send2->hash ()));
}

TEST (node, block_processor_full)
{
	scendere::system system;
	scendere::node_flags node_flags;
	node_flags.force_use_write_database_queue = true;
	node_flags.block_processor_full_size = 3;
	auto & node = *system.add_node (scendere::node_config (scendere::get_available_port (), system.logging), node_flags);
	scendere::state_block_builder builder;
	auto send1 = builder.make_block ()
				 .account (scendere::dev::genesis_key.pub)
				 .previous (scendere::dev::genesis->hash ())
				 .representative (scendere::dev::genesis_key.pub)
				 .balance (scendere::dev::constants.genesis_amount - scendere::Gxrb_ratio)
				 .link (scendere::dev::genesis_key.pub)
				 .sign (scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub)
				 .work (*node.work_generate_blocking (scendere::dev::genesis->hash ()))
				 .build_shared ();
	auto send2 = builder.make_block ()
				 .account (scendere::dev::genesis_key.pub)
				 .previous (send1->hash ())
				 .representative (scendere::dev::genesis_key.pub)
				 .balance (scendere::dev::constants.genesis_amount - 2 * scendere::Gxrb_ratio)
				 .link (scendere::dev::genesis_key.pub)
				 .sign (scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub)
				 .work (*node.work_generate_blocking (send1->hash ()))
				 .build_shared ();
	auto send3 = builder.make_block ()
				 .account (scendere::dev::genesis_key.pub)
				 .previous (send2->hash ())
				 .representative (scendere::dev::genesis_key.pub)
				 .balance (scendere::dev::constants.genesis_amount - 3 * scendere::Gxrb_ratio)
				 .link (scendere::dev::genesis_key.pub)
				 .sign (scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub)
				 .work (*node.work_generate_blocking (send2->hash ()))
				 .build_shared ();
	// The write guard prevents block processor doing any writes
	auto write_guard = node.write_database_queue.wait (scendere::writer::testing);
	node.block_processor.add (send1);
	ASSERT_FALSE (node.block_processor.full ());
	node.block_processor.add (send2);
	ASSERT_FALSE (node.block_processor.full ());
	node.block_processor.add (send3);
	// Block processor may be not full during state blocks signatures verification
	ASSERT_TIMELY (2s, node.block_processor.full ());
}

TEST (node, block_processor_half_full)
{
	scendere::system system;
	scendere::node_flags node_flags;
	node_flags.block_processor_full_size = 6;
	node_flags.force_use_write_database_queue = true;
	auto & node = *system.add_node (scendere::node_config (scendere::get_available_port (), system.logging), node_flags);
	scendere::state_block_builder builder;
	auto send1 = builder.make_block ()
				 .account (scendere::dev::genesis_key.pub)
				 .previous (scendere::dev::genesis->hash ())
				 .representative (scendere::dev::genesis_key.pub)
				 .balance (scendere::dev::constants.genesis_amount - scendere::Gxrb_ratio)
				 .link (scendere::dev::genesis_key.pub)
				 .sign (scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub)
				 .work (*node.work_generate_blocking (scendere::dev::genesis->hash ()))
				 .build_shared ();
	auto send2 = builder.make_block ()
				 .account (scendere::dev::genesis_key.pub)
				 .previous (send1->hash ())
				 .representative (scendere::dev::genesis_key.pub)
				 .balance (scendere::dev::constants.genesis_amount - 2 * scendere::Gxrb_ratio)
				 .link (scendere::dev::genesis_key.pub)
				 .sign (scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub)
				 .work (*node.work_generate_blocking (send1->hash ()))
				 .build_shared ();
	auto send3 = builder.make_block ()
				 .account (scendere::dev::genesis_key.pub)
				 .previous (send2->hash ())
				 .representative (scendere::dev::genesis_key.pub)
				 .balance (scendere::dev::constants.genesis_amount - 3 * scendere::Gxrb_ratio)
				 .link (scendere::dev::genesis_key.pub)
				 .sign (scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub)
				 .work (*node.work_generate_blocking (send2->hash ()))
				 .build_shared ();
	// The write guard prevents block processor doing any writes
	auto write_guard = node.write_database_queue.wait (scendere::writer::testing);
	node.block_processor.add (send1);
	ASSERT_FALSE (node.block_processor.half_full ());
	node.block_processor.add (send2);
	ASSERT_FALSE (node.block_processor.half_full ());
	node.block_processor.add (send3);
	// Block processor may be not half_full during state blocks signatures verification
	ASSERT_TIMELY (2s, node.block_processor.half_full ());
	ASSERT_FALSE (node.block_processor.full ());
}

TEST (node, confirm_back)
{
	scendere::system system (1);
	scendere::keypair key;
	auto & node (*system.nodes[0]);
	auto genesis_start_balance (node.balance (scendere::dev::genesis_key.pub));
	auto send1 = scendere::send_block_builder ()
				 .previous (scendere::dev::genesis->hash ())
				 .destination (key.pub)
				 .balance (genesis_start_balance - 1)
				 .sign (scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub)
				 .work (*system.work.generate (scendere::dev::genesis->hash ()))
				 .build_shared ();
	scendere::state_block_builder builder;
	auto open = builder.make_block ()
				.account (key.pub)
				.previous (0)
				.representative (key.pub)
				.balance (1)
				.link (send1->hash ())
				.sign (key.prv, key.pub)
				.work (*system.work.generate (key.pub))
				.build_shared ();
	auto send2 = builder.make_block ()
				 .account (key.pub)
				 .previous (open->hash ())
				 .representative (key.pub)
				 .balance (0)
				 .link (scendere::dev::genesis_key.pub)
				 .sign (key.prv, key.pub)
				 .work (*system.work.generate (open->hash ()))
				 .build_shared ();
	node.process_active (send1);
	node.process_active (open);
	node.process_active (send2);
	scendere::blocks_confirm (node, { send1, open, send2 });
	ASSERT_EQ (3, node.active.size ());
	std::vector<scendere::block_hash> vote_blocks;
	vote_blocks.push_back (send2->hash ());
	auto vote (std::make_shared<scendere::vote> (scendere::dev::genesis_key.pub, scendere::dev::genesis_key.prv, scendere::vote::timestamp_max, scendere::vote::duration_max, vote_blocks));
	node.vote_processor.vote_blocking (vote, std::make_shared<scendere::transport::channel_loopback> (node));
	ASSERT_TIMELY (10s, node.active.empty ());
}

TEST (node, peers)
{
	scendere::system system (1);
	auto node1 (system.nodes[0]);
	ASSERT_TRUE (node1->network.empty ());

	auto node2 (std::make_shared<scendere::node> (system.io_ctx, scendere::get_available_port (), scendere::unique_path (), system.logging, system.work));
	system.nodes.push_back (node2);

	auto endpoint = node1->network.endpoint ();
	scendere::endpoint_key endpoint_key{ endpoint.address ().to_v6 ().to_bytes (), endpoint.port () };
	auto & store = node2->store;
	{
		// Add a peer to the database
		auto transaction (store.tx_begin_write ());
		store.peer.put (transaction, endpoint_key);

		// Add a peer which is not contactable
		store.peer.put (transaction, scendere::endpoint_key{ boost::asio::ip::address_v6::any ().to_bytes (), 55555 });
	}

	node2->start ();
	ASSERT_TIMELY (10s, !node2->network.empty () && !node1->network.empty ())
	// Wait to finish TCP node ID handshakes
	ASSERT_TIMELY (10s, node1->bootstrap.realtime_count != 0 && node2->bootstrap.realtime_count != 0);
	// Confirm that the peers match with the endpoints we are expecting
	ASSERT_EQ (1, node1->network.size ());
	auto list1 (node1->network.list (2));
	ASSERT_EQ (node2->network.endpoint (), list1[0]->get_endpoint ());
	ASSERT_EQ (scendere::transport::transport_type::tcp, list1[0]->get_type ());
	ASSERT_EQ (1, node2->network.size ());
	auto list2 (node2->network.list (2));
	ASSERT_EQ (node1->network.endpoint (), list2[0]->get_endpoint ());
	ASSERT_EQ (scendere::transport::transport_type::tcp, list2[0]->get_type ());
	// Stop the peer node and check that it is removed from the store
	node1->stop ();

	ASSERT_TIMELY (10s, node2->network.size () != 1);

	ASSERT_TRUE (node2->network.empty ());

	// Uncontactable peer should not be stored
	auto transaction (store.tx_begin_read ());
	ASSERT_EQ (store.peer.count (transaction), 1);
	ASSERT_TRUE (store.peer.exists (transaction, endpoint_key));

	node2->stop ();
}

TEST (node, peer_cache_restart)
{
	scendere::system system (1);
	auto node1 (system.nodes[0]);
	ASSERT_TRUE (node1->network.empty ());
	auto endpoint = node1->network.endpoint ();
	scendere::endpoint_key endpoint_key{ endpoint.address ().to_v6 ().to_bytes (), endpoint.port () };
	auto path (scendere::unique_path ());
	{
		auto node2 (std::make_shared<scendere::node> (system.io_ctx, scendere::get_available_port (), path, system.logging, system.work));
		system.nodes.push_back (node2);
		auto & store = node2->store;
		{
			// Add a peer to the database
			auto transaction (store.tx_begin_write ());
			store.peer.put (transaction, endpoint_key);
		}
		node2->start ();
		ASSERT_TIMELY (10s, !node2->network.empty ());
		// Confirm that the peers match with the endpoints we are expecting
		auto list (node2->network.list (2));
		ASSERT_EQ (node1->network.endpoint (), list[0]->get_endpoint ());
		ASSERT_EQ (1, node2->network.size ());
		node2->stop ();
	}
	// Restart node
	{
		scendere::node_flags node_flags;
		node_flags.read_only = true;
		auto node3 (std::make_shared<scendere::node> (system.io_ctx, scendere::get_available_port (), path, system.logging, system.work, node_flags));
		system.nodes.push_back (node3);
		// Check cached peers after restart
		node3->network.start ();
		node3->add_initial_peers ();

		auto & store = node3->store;
		{
			auto transaction (store.tx_begin_read ());
			ASSERT_EQ (store.peer.count (transaction), 1);
			ASSERT_TRUE (store.peer.exists (transaction, endpoint_key));
		}
		ASSERT_TIMELY (10s, !node3->network.empty ());
		// Confirm that the peers match with the endpoints we are expecting
		auto list (node3->network.list (2));
		ASSERT_EQ (node1->network.endpoint (), list[0]->get_endpoint ());
		ASSERT_EQ (1, node3->network.size ());
		node3->stop ();
	}
}

TEST (node, unchecked_cleanup)
{
	scendere::system system{};
	scendere::node_flags node_flags{};
	node_flags.disable_unchecked_cleanup = true;
	scendere::keypair key{};
	auto & node = *system.add_node (node_flags);
	auto open = scendere::state_block_builder ()
				.account (key.pub)
				.previous (0)
				.representative (key.pub)
				.balance (1)
				.link (key.pub)
				.sign (key.prv, key.pub)
				.work (*system.work.generate (key.pub))
				.build_shared ();
	std::vector<uint8_t> bytes;
	{
		scendere::vectorstream stream (bytes);
		open->serialize (stream);
	}
	// Add to the blocks filter
	// Should be cleared after unchecked cleanup
	ASSERT_FALSE (node.network.publish_filter.apply (bytes.data (), bytes.size ()));
	node.process_active (open);
	// Waits for the open block to get saved in the database
	ASSERT_TIMELY (15s, 1 == node.unchecked.count (node.store.tx_begin_read ()));
	node.config.unchecked_cutoff_time = std::chrono::seconds (2);
	ASSERT_EQ (1, node.unchecked.count (node.store.tx_begin_read ()));
	std::this_thread::sleep_for (std::chrono::seconds (1));
	node.unchecked_cleanup ();
	ASSERT_TRUE (node.network.publish_filter.apply (bytes.data (), bytes.size ()));
	ASSERT_EQ (1, node.unchecked.count (node.store.tx_begin_read ()));
	std::this_thread::sleep_for (std::chrono::seconds (2));
	node.unchecked_cleanup ();
	ASSERT_FALSE (node.network.publish_filter.apply (bytes.data (), bytes.size ()));
	ASSERT_EQ (0, node.unchecked.count (node.store.tx_begin_read ()));
}

/** This checks that a node can be opened (without being blocked) when a write lock is held elsewhere */
TEST (node, dont_write_lock_node)
{
	auto path = scendere::unique_path ();

	std::promise<void> write_lock_held_promise;
	std::promise<void> finished_promise;
	std::thread ([&path, &write_lock_held_promise, &finished_promise] () {
		scendere::logger_mt logger;
		auto store = scendere::make_store (logger, path, scendere::dev::constants, false, true);
		{
			scendere::ledger_cache ledger_cache;
			auto transaction (store->tx_begin_write ());
			store->initialize (transaction, ledger_cache);
		}

		// Hold write lock open until main thread is done needing it
		auto transaction (store->tx_begin_write ());
		write_lock_held_promise.set_value ();
		finished_promise.get_future ().wait ();
	})
	.detach ();

	write_lock_held_promise.get_future ().wait ();

	// Check inactive node can finish executing while a write lock is open
	scendere::inactive_node node (path, scendere::inactive_node_flag_defaults ());
	finished_promise.set_value ();
}

TEST (node, bidirectional_tcp)
{
#ifdef _WIN32
	if (scendere::rocksdb_config::using_rocksdb_in_tests ())
	{
		// Don't test this in rocksdb mode
		return;
	}
#endif
	scendere::system system;
	scendere::node_flags node_flags;
	// Disable bootstrap to start elections for new blocks
	node_flags.disable_legacy_bootstrap = true;
	node_flags.disable_lazy_bootstrap = true;
	node_flags.disable_wallet_bootstrap = true;
	scendere::node_config node_config (scendere::get_available_port (), system.logging);
	node_config.frontiers_confirmation = scendere::frontiers_confirmation_mode::disabled;
	auto node1 = system.add_node (node_config, node_flags);
	node_config.peering_port = scendere::get_available_port ();
	node_config.tcp_incoming_connections_max = 0; // Disable incoming TCP connections for node 2
	auto node2 = system.add_node (node_config, node_flags);
	// Check network connections
	ASSERT_EQ (1, node1->network.size ());
	ASSERT_EQ (1, node2->network.size ());
	auto list1 (node1->network.list (1));
	ASSERT_EQ (scendere::transport::transport_type::tcp, list1[0]->get_type ());
	ASSERT_NE (node2->network.endpoint (), list1[0]->get_endpoint ()); // Ephemeral port
	ASSERT_EQ (node2->node_id.pub, list1[0]->get_node_id ());
	auto list2 (node2->network.list (1));
	ASSERT_EQ (scendere::transport::transport_type::tcp, list2[0]->get_type ());
	ASSERT_EQ (node1->network.endpoint (), list2[0]->get_endpoint ());
	ASSERT_EQ (node1->node_id.pub, list2[0]->get_node_id ());
	// Test block propagation from node 1
	scendere::keypair key;
	scendere::state_block_builder builder;
	auto send1 = builder.make_block ()
				 .account (scendere::dev::genesis_key.pub)
				 .previous (scendere::dev::genesis->hash ())
				 .representative (scendere::dev::genesis_key.pub)
				 .balance (scendere::dev::constants.genesis_amount - scendere::Gxrb_ratio)
				 .link (key.pub)
				 .sign (scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub)
				 .work (*node1->work_generate_blocking (scendere::dev::genesis->hash ()))
				 .build_shared ();
	node1->process_active (send1);
	ASSERT_TIMELY (10s, node1->ledger.block_or_pruned_exists (send1->hash ()) && node2->ledger.block_or_pruned_exists (send1->hash ()));
	// Test block confirmation from node 1 (add representative to node 1)
	system.wallet (0)->insert_adhoc (scendere::dev::genesis_key.prv);
	// Wait to find new reresentative
	ASSERT_TIMELY (10s, node2->rep_crawler.representative_count () != 0);
	/* Wait for confirmation
	To check connection we need only node 2 confirmation status
	Node 1 election can be unconfirmed because representative private key was inserted after election start (and node 2 isn't flooding new votes to principal representatives) */
	bool confirmed (false);
	system.deadline_set (10s);
	while (!confirmed)
	{
		auto transaction2 (node2->store.tx_begin_read ());
		confirmed = node2->ledger.block_confirmed (transaction2, send1->hash ());
		ASSERT_NO_ERROR (system.poll ());
	}
	// Test block propagation & confirmation from node 2 (remove representative from node 1)
	{
		auto transaction (system.wallet (0)->wallets.tx_begin_write ());
		system.wallet (0)->store.erase (transaction, scendere::dev::genesis_key.pub);
	}
	/* Test block propagation from node 2
	Node 2 has only ephemeral TCP port open. Node 1 cannot establish connection to node 2 listening port */
	auto send2 = builder.make_block ()
				 .account (scendere::dev::genesis_key.pub)
				 .previous (send1->hash ())
				 .representative (scendere::dev::genesis_key.pub)
				 .balance (scendere::dev::constants.genesis_amount - 2 * scendere::Gxrb_ratio)
				 .link (key.pub)
				 .sign (scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub)
				 .work (*node1->work_generate_blocking (send1->hash ()))
				 .build_shared ();
	node2->process_active (send2);
	node2->block_processor.flush ();
	ASSERT_TIMELY (10s, node1->ledger.block_or_pruned_exists (send2->hash ()) && node2->ledger.block_or_pruned_exists (send2->hash ()));
	// Test block confirmation from node 2 (add representative to node 2)
	system.wallet (1)->insert_adhoc (scendere::dev::genesis_key.prv);
	// Wait to find changed reresentative
	ASSERT_TIMELY (10s, node1->rep_crawler.representative_count () != 0);
	/* Wait for confirmation
	To check connection we need only node 1 confirmation status
	Node 2 election can be unconfirmed because representative private key was inserted after election start (and node 1 isn't flooding new votes to principal representatives) */
	confirmed = false;
	system.deadline_set (20s);
	while (!confirmed)
	{
		auto transaction1 (node1->store.tx_begin_read ());
		confirmed = node1->ledger.block_confirmed (transaction1, send2->hash ());
		ASSERT_NO_ERROR (system.poll ());
	}
}

// Tests that local blocks are flooded to all principal representatives
// Sanitizers or running within valgrind use different timings and number of nodes
TEST (node, aggressive_flooding)
{
	scendere::system system;
	scendere::node_flags node_flags;
	node_flags.disable_request_loop = true;
	node_flags.disable_block_processor_republishing = true;
	node_flags.disable_bootstrap_bulk_push_client = true;
	node_flags.disable_bootstrap_bulk_pull_server = true;
	node_flags.disable_bootstrap_listener = true;
	node_flags.disable_lazy_bootstrap = true;
	node_flags.disable_legacy_bootstrap = true;
	node_flags.disable_wallet_bootstrap = true;
	auto & node1 (*system.add_node (node_flags));
	auto & wallet1 (*system.wallet (0));
	wallet1.insert_adhoc (scendere::dev::genesis_key.prv);
	std::vector<std::pair<std::shared_ptr<scendere::node>, std::shared_ptr<scendere::wallet>>> nodes_wallets;
	bool const sanitizer_or_valgrind (is_sanitizer_build || scendere::running_within_valgrind ());
	nodes_wallets.resize (!sanitizer_or_valgrind ? 5 : 3);

	std::generate (nodes_wallets.begin (), nodes_wallets.end (), [&system, node_flags] () {
		scendere::node_config node_config (scendere::get_available_port (), system.logging);
		auto node (system.add_node (node_config, node_flags));
		return std::make_pair (node, system.wallet (system.nodes.size () - 1));
	});

	// This test is only valid if a non-aggressive flood would not reach every peer
	ASSERT_TIMELY (5s, node1.network.size () == nodes_wallets.size ());
	ASSERT_LT (node1.network.fanout (), nodes_wallets.size ());

	// Each new node should see genesis representative
	ASSERT_TIMELY (10s, std::all_of (nodes_wallets.begin (), nodes_wallets.end (), [] (auto const & node_wallet) { return node_wallet.first->rep_crawler.principal_representatives ().size () != 0; }));

	// Send a large amount to create a principal representative in each node
	auto large_amount = (scendere::dev::constants.genesis_amount / 2) / nodes_wallets.size ();
	std::vector<std::shared_ptr<scendere::block>> genesis_blocks;
	for (auto & node_wallet : nodes_wallets)
	{
		scendere::keypair keypair;
		node_wallet.second->store.representative_set (node_wallet.first->wallets.tx_begin_write (), keypair.pub);
		node_wallet.second->insert_adhoc (keypair.prv);
		auto block (wallet1.send_action (scendere::dev::genesis_key.pub, keypair.pub, large_amount));
		ASSERT_NE (nullptr, block);
		genesis_blocks.push_back (block);
	}

	// Ensure all nodes have the full genesis chain
	for (auto & node_wallet : nodes_wallets)
	{
		for (auto const & block : genesis_blocks)
		{
			auto process_result (node_wallet.first->process (*block));
			ASSERT_TRUE (scendere::process_result::progress == process_result.code || scendere::process_result::old == process_result.code);
		}
		ASSERT_EQ (node1.latest (scendere::dev::genesis_key.pub), node_wallet.first->latest (scendere::dev::genesis_key.pub));
		ASSERT_EQ (genesis_blocks.back ()->hash (), node_wallet.first->latest (scendere::dev::genesis_key.pub));
		// Confirm blocks for rep crawler & receiving
		scendere::blocks_confirm (*node_wallet.first, { genesis_blocks.back () }, true);
	}
	scendere::blocks_confirm (node1, { genesis_blocks.back () }, true);

	// Wait until all genesis blocks are received
	auto all_received = [&nodes_wallets] () {
		return std::all_of (nodes_wallets.begin (), nodes_wallets.end (), [] (auto const & node_wallet) {
			auto local_representative (node_wallet.second->store.representative (node_wallet.first->wallets.tx_begin_read ()));
			return node_wallet.first->ledger.account_balance (node_wallet.first->store.tx_begin_read (), local_representative) > 0;
		});
	};

	ASSERT_TIMELY (!sanitizer_or_valgrind ? 10s : 40s, all_received ());

	ASSERT_TIMELY (!sanitizer_or_valgrind ? 10s : 40s, node1.ledger.cache.block_count == 1 + 2 * nodes_wallets.size ());

	// Wait until the main node sees all representatives
	ASSERT_TIMELY (!sanitizer_or_valgrind ? 10s : 40s, node1.rep_crawler.principal_representatives ().size () == nodes_wallets.size ());

	// Generate blocks and ensure they are sent to all representatives
	scendere::state_block_builder builder;
	std::shared_ptr<scendere::state_block> block{};
	{
		auto transaction (node1.store.tx_begin_read ());
		block = builder.make_block ()
				.account (scendere::dev::genesis_key.pub)
				.representative (scendere::dev::genesis_key.pub)
				.previous (node1.ledger.latest (transaction, scendere::dev::genesis_key.pub))
				.balance (node1.ledger.account_balance (transaction, scendere::dev::genesis_key.pub) - 1)
				.link (scendere::dev::genesis_key.pub)
				.sign (scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub)
				.work (*node1.work_generate_blocking (node1.ledger.latest (transaction, scendere::dev::genesis_key.pub)))
				.build ();
	}
	// Processing locally goes through the aggressive block flooding path
	ASSERT_EQ (scendere::process_result::progress, node1.process_local (block).code);

	auto all_have_block = [&nodes_wallets] (scendere::block_hash const & hash_a) {
		return std::all_of (nodes_wallets.begin (), nodes_wallets.end (), [hash = hash_a] (auto const & node_wallet) {
			return node_wallet.first->block (hash) != nullptr;
		});
	};

	ASSERT_TIMELY (!sanitizer_or_valgrind ? 5s : 25s, all_have_block (block->hash ()));

	// Do the same for a wallet block
	auto wallet_block = wallet1.send_sync (scendere::dev::genesis_key.pub, scendere::dev::genesis_key.pub, 10);
	ASSERT_TIMELY (!sanitizer_or_valgrind ? 5s : 25s, all_have_block (wallet_block));

	// All blocks: genesis + (send+open) for each representative + 2 local blocks
	// The main node only sees all blocks if other nodes are flooding their PR's open block to all other PRs
	ASSERT_EQ (1 + 2 * nodes_wallets.size () + 2, node1.ledger.cache.block_count);
}

TEST (node, node_sequence)
{
	scendere::system system (3);
	ASSERT_EQ (0, system.nodes[0]->node_seq);
	ASSERT_EQ (0, system.nodes[0]->node_seq);
	ASSERT_EQ (1, system.nodes[1]->node_seq);
	ASSERT_EQ (2, system.nodes[2]->node_seq);
}

TEST (node, rollback_vote_self)
{
	scendere::system system;
	scendere::node_flags flags;
	flags.disable_request_loop = true;
	auto & node = *system.add_node (flags);
	scendere::state_block_builder builder;
	scendere::keypair key;
	auto weight = node.online_reps.delta ();
	auto send1 = builder.make_block ()
				 .account (scendere::dev::genesis_key.pub)
				 .previous (scendere::dev::genesis->hash ())
				 .representative (scendere::dev::genesis_key.pub)
				 .link (key.pub)
				 .balance (scendere::dev::constants.genesis_amount - weight)
				 .sign (scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub)
				 .work (*system.work.generate (scendere::dev::genesis->hash ()))
				 .build_shared ();
	auto open = builder.make_block ()
				.account (key.pub)
				.previous (0)
				.representative (key.pub)
				.link (send1->hash ())
				.balance (weight)
				.sign (key.prv, key.pub)
				.work (*system.work.generate (key.pub))
				.build_shared ();
	auto send2 = builder.make_block ()
				 .from (*send1)
				 .previous (send1->hash ())
				 .balance (send1->balance ().number () - 1)
				 .link (scendere::dev::genesis_key.pub)
				 .sign (scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub)
				 .work (*system.work.generate (send1->hash ()))
				 .build_shared ();
	auto fork = builder.make_block ()
				.from (*send2)
				.balance (send2->balance ().number () - 2)
				.sign (scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub)
				.build_shared ();
	ASSERT_EQ (scendere::process_result::progress, node.process (*send1).code);
	ASSERT_EQ (scendere::process_result::progress, node.process (*open).code);
	// Confirm blocks to allow voting
	node.block_confirm (open);
	auto election = node.active.election (open->qualified_root ());
	ASSERT_NE (nullptr, election);
	election->force_confirm ();
	ASSERT_TIMELY (5s, node.ledger.cache.cemented_count == 3);
	ASSERT_EQ (weight, node.weight (key.pub));
	node.process_active (send2);
	node.block_processor.flush ();
	node.scheduler.flush ();
	node.process_active (fork);
	node.block_processor.flush ();
	node.scheduler.flush ();
	election = node.active.election (send2->qualified_root ());
	ASSERT_NE (nullptr, election);
	ASSERT_EQ (2, election->blocks ().size ());
	// Insert genesis key in the wallet
	system.wallet (0)->insert_adhoc (scendere::dev::genesis_key.prv);
	{
		// The write guard prevents the block processor from performing the rollback
		auto write_guard = node.write_database_queue.wait (scendere::writer::testing);
		{
			ASSERT_EQ (1, election->votes ().size ());
			// Vote with key to switch the winner
			election->vote (key.pub, 0, fork->hash ());
			ASSERT_EQ (2, election->votes ().size ());
			// The winner changed
			ASSERT_EQ (election->winner (), fork);
		}
		// Even without the rollback being finished, the aggregator must reply with a vote for the new winner, not the old one
		ASSERT_TRUE (node.history.votes (send2->root (), send2->hash ()).empty ());
		ASSERT_TRUE (node.history.votes (fork->root (), fork->hash ()).empty ());
		auto & node2 = *system.add_node ();
		auto channel (node.network.udp_channels.create (node2.network.endpoint ()));
		node.aggregator.add (channel, { { send2->hash (), send2->root () } });
		ASSERT_TIMELY (5s, !node.history.votes (fork->root (), fork->hash ()).empty ());
		ASSERT_TRUE (node.history.votes (send2->root (), send2->hash ()).empty ());

		// Going out of the scope allows the rollback to complete
	}
	// A vote is eventually generated from the local representative
	ASSERT_TIMELY (5s, 3 == election->votes ().size ());
	auto votes (election->votes ());
	auto vote (votes.find (scendere::dev::genesis_key.pub));
	ASSERT_NE (votes.end (), vote);
	ASSERT_EQ (fork->hash (), vote->second.hash);
}

TEST (node, rollback_gap_source)
{
	scendere::system system;
	scendere::node_config node_config (scendere::get_available_port (), system.logging);
	node_config.frontiers_confirmation = scendere::frontiers_confirmation_mode::disabled;
	auto & node = *system.add_node (node_config);
	scendere::state_block_builder builder;
	scendere::keypair key;
	auto send1 = builder.make_block ()
				 .account (scendere::dev::genesis_key.pub)
				 .previous (scendere::dev::genesis->hash ())
				 .representative (scendere::dev::genesis_key.pub)
				 .link (key.pub)
				 .balance (scendere::dev::constants.genesis_amount - 1)
				 .sign (scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub)
				 .work (*system.work.generate (scendere::dev::genesis->hash ()))
				 .build_shared ();
	auto fork = builder.make_block ()
				.account (key.pub)
				.previous (0)
				.representative (key.pub)
				.link (send1->hash ())
				.balance (1)
				.sign (key.prv, key.pub)
				.work (*system.work.generate (key.pub))
				.build_shared ();
	auto send2 = builder.make_block ()
				 .from (*send1)
				 .previous (send1->hash ())
				 .balance (send1->balance ().number () - 1)
				 .link (key.pub)
				 .sign (scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub)
				 .work (*system.work.generate (send1->hash ()))
				 .build_shared ();
	auto open = builder.make_block ()
				.from (*fork)
				.link (send2->hash ())
				.sign (key.prv, key.pub)
				.build_shared ();
	ASSERT_EQ (scendere::process_result::progress, node.process (*send1).code);
	ASSERT_EQ (scendere::process_result::progress, node.process (*fork).code);
	// Node has fork & doesn't have source for correct block open (send2)
	ASSERT_EQ (nullptr, node.block (send2->hash ()));
	// Start election for fork
	scendere::blocks_confirm (node, { fork });
	{
		auto election = node.active.election (fork->qualified_root ());
		ASSERT_NE (nullptr, election);
		// Process conflicting block for election
		node.process_active (open);
		node.block_processor.flush ();
		ASSERT_EQ (2, election->blocks ().size ());
		ASSERT_EQ (1, election->votes ().size ());
		// Confirm open
		auto vote1 (std::make_shared<scendere::vote> (scendere::dev::genesis_key.pub, scendere::dev::genesis_key.prv, scendere::vote::timestamp_max, scendere::vote::duration_max, std::vector<scendere::block_hash> (1, open->hash ())));
		node.vote_processor.vote (vote1, std::make_shared<scendere::transport::channel_loopback> (node));
		ASSERT_TIMELY (5s, election->votes ().size () == 2);
		ASSERT_TIMELY (3s, election->confirmed ());
	}
	// Wait for the rollback (attempt to replace fork with open)
	ASSERT_TIMELY (5s, node.stats.count (scendere::stat::type::rollback, scendere::stat::detail::open) == 1);
	ASSERT_TIMELY (5s, node.active.empty ());
	// But replacing is not possible (missing source block - send2)
	node.block_processor.flush ();
	ASSERT_EQ (nullptr, node.block (open->hash ()));
	ASSERT_EQ (nullptr, node.block (fork->hash ()));
	// Fork can be returned by some other forked node or attacker
	node.process_active (fork);
	node.block_processor.flush ();
	ASSERT_NE (nullptr, node.block (fork->hash ()));
	// With send2 block in ledger election can start again to remove fork block
	ASSERT_EQ (scendere::process_result::progress, node.process (*send2).code);
	scendere::blocks_confirm (node, { fork });
	{
		auto election = node.active.election (fork->qualified_root ());
		ASSERT_NE (nullptr, election);
		// Process conflicting block for election
		node.process_active (open);
		node.block_processor.flush ();
		ASSERT_EQ (2, election->blocks ().size ());
		// Confirm open (again)
		auto vote1 (std::make_shared<scendere::vote> (scendere::dev::genesis_key.pub, scendere::dev::genesis_key.prv, scendere::vote::timestamp_max, scendere::vote::duration_max, std::vector<scendere::block_hash> (1, open->hash ())));
		node.vote_processor.vote (vote1, std::make_shared<scendere::transport::channel_loopback> (node));
		ASSERT_TIMELY (5s, election->votes ().size () == 2);
	}
	// Wait for new rollback
	ASSERT_TIMELY (5s, node.stats.count (scendere::stat::type::rollback, scendere::stat::detail::open) == 2);
	// Now fork block should be replaced with open
	node.block_processor.flush ();
	ASSERT_NE (nullptr, node.block (open->hash ()));
	ASSERT_EQ (nullptr, node.block (fork->hash ()));
}

// Confirm a complex dependency graph starting from the first block
TEST (node, dependency_graph)
{
	scendere::system system;
	scendere::node_config config (scendere::get_available_port (), system.logging);
	config.frontiers_confirmation = scendere::frontiers_confirmation_mode::disabled;
	auto & node = *system.add_node (config);

	scendere::state_block_builder builder;
	scendere::keypair key1, key2, key3;

	// Send to key1
	auto gen_send1 = builder.make_block ()
					 .account (scendere::dev::genesis_key.pub)
					 .previous (scendere::dev::genesis->hash ())
					 .representative (scendere::dev::genesis_key.pub)
					 .link (key1.pub)
					 .balance (scendere::dev::constants.genesis_amount - 1)
					 .sign (scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub)
					 .work (*system.work.generate (scendere::dev::genesis->hash ()))
					 .build_shared ();
	// Receive from genesis
	auto key1_open = builder.make_block ()
					 .account (key1.pub)
					 .previous (0)
					 .representative (key1.pub)
					 .link (gen_send1->hash ())
					 .balance (1)
					 .sign (key1.prv, key1.pub)
					 .work (*system.work.generate (key1.pub))
					 .build ();
	// Send to genesis
	auto key1_send1 = builder.make_block ()
					  .account (key1.pub)
					  .previous (key1_open->hash ())
					  .representative (key1.pub)
					  .link (scendere::dev::genesis_key.pub)
					  .balance (0)
					  .sign (key1.prv, key1.pub)
					  .work (*system.work.generate (key1_open->hash ()))
					  .build ();
	// Receive from key1
	auto gen_receive = builder.make_block ()
					   .from (*gen_send1)
					   .previous (gen_send1->hash ())
					   .link (key1_send1->hash ())
					   .balance (scendere::dev::constants.genesis_amount)
					   .sign (scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub)
					   .work (*system.work.generate (gen_send1->hash ()))
					   .build ();
	// Send to key2
	auto gen_send2 = builder.make_block ()
					 .from (*gen_receive)
					 .previous (gen_receive->hash ())
					 .link (key2.pub)
					 .balance (gen_receive->balance ().number () - 2)
					 .sign (scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub)
					 .work (*system.work.generate (gen_receive->hash ()))
					 .build ();
	// Receive from genesis
	auto key2_open = builder.make_block ()
					 .account (key2.pub)
					 .previous (0)
					 .representative (key2.pub)
					 .link (gen_send2->hash ())
					 .balance (2)
					 .sign (key2.prv, key2.pub)
					 .work (*system.work.generate (key2.pub))
					 .build ();
	// Send to key3
	auto key2_send1 = builder.make_block ()
					  .account (key2.pub)
					  .previous (key2_open->hash ())
					  .representative (key2.pub)
					  .link (key3.pub)
					  .balance (1)
					  .sign (key2.prv, key2.pub)
					  .work (*system.work.generate (key2_open->hash ()))
					  .build ();
	// Receive from key2
	auto key3_open = builder.make_block ()
					 .account (key3.pub)
					 .previous (0)
					 .representative (key3.pub)
					 .link (key2_send1->hash ())
					 .balance (1)
					 .sign (key3.prv, key3.pub)
					 .work (*system.work.generate (key3.pub))
					 .build ();
	// Send to key1
	auto key2_send2 = builder.make_block ()
					  .from (*key2_send1)
					  .previous (key2_send1->hash ())
					  .link (key1.pub)
					  .balance (key2_send1->balance ().number () - 1)
					  .sign (key2.prv, key2.pub)
					  .work (*system.work.generate (key2_send1->hash ()))
					  .build ();
	// Receive from key2
	auto key1_receive = builder.make_block ()
						.from (*key1_send1)
						.previous (key1_send1->hash ())
						.link (key2_send2->hash ())
						.balance (key1_send1->balance ().number () + 1)
						.sign (key1.prv, key1.pub)
						.work (*system.work.generate (key1_send1->hash ()))
						.build ();
	// Send to key3
	auto key1_send2 = builder.make_block ()
					  .from (*key1_receive)
					  .previous (key1_receive->hash ())
					  .link (key3.pub)
					  .balance (key1_receive->balance ().number () - 1)
					  .sign (key1.prv, key1.pub)
					  .work (*system.work.generate (key1_receive->hash ()))
					  .build ();
	// Receive from key1
	auto key3_receive = builder.make_block ()
						.from (*key3_open)
						.previous (key3_open->hash ())
						.link (key1_send2->hash ())
						.balance (key3_open->balance ().number () + 1)
						.sign (key3.prv, key3.pub)
						.work (*system.work.generate (key3_open->hash ()))
						.build ();
	// Upgrade key3
	auto key3_epoch = builder.make_block ()
					  .from (*key3_receive)
					  .previous (key3_receive->hash ())
					  .link (node.ledger.epoch_link (scendere::epoch::epoch_1))
					  .balance (key3_receive->balance ())
					  .sign (scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub)
					  .work (*system.work.generate (key3_receive->hash ()))
					  .build ();

	ASSERT_EQ (scendere::process_result::progress, node.process (*gen_send1).code);
	ASSERT_EQ (scendere::process_result::progress, node.process (*key1_open).code);
	ASSERT_EQ (scendere::process_result::progress, node.process (*key1_send1).code);
	ASSERT_EQ (scendere::process_result::progress, node.process (*gen_receive).code);
	ASSERT_EQ (scendere::process_result::progress, node.process (*gen_send2).code);
	ASSERT_EQ (scendere::process_result::progress, node.process (*key2_open).code);
	ASSERT_EQ (scendere::process_result::progress, node.process (*key2_send1).code);
	ASSERT_EQ (scendere::process_result::progress, node.process (*key3_open).code);
	ASSERT_EQ (scendere::process_result::progress, node.process (*key2_send2).code);
	ASSERT_EQ (scendere::process_result::progress, node.process (*key1_receive).code);
	ASSERT_EQ (scendere::process_result::progress, node.process (*key1_send2).code);
	ASSERT_EQ (scendere::process_result::progress, node.process (*key3_receive).code);
	ASSERT_EQ (scendere::process_result::progress, node.process (*key3_epoch).code);
	ASSERT_TRUE (node.active.empty ());

	// Hash -> Ancestors
	std::unordered_map<scendere::block_hash, std::vector<scendere::block_hash>> dependency_graph{
		{ key1_open->hash (), { gen_send1->hash () } },
		{ key1_send1->hash (), { key1_open->hash () } },
		{ gen_receive->hash (), { gen_send1->hash (), key1_open->hash () } },
		{ gen_send2->hash (), { gen_receive->hash () } },
		{ key2_open->hash (), { gen_send2->hash () } },
		{ key2_send1->hash (), { key2_open->hash () } },
		{ key3_open->hash (), { key2_send1->hash () } },
		{ key2_send2->hash (), { key2_send1->hash () } },
		{ key1_receive->hash (), { key1_send1->hash (), key2_send2->hash () } },
		{ key1_send2->hash (), { key1_send1->hash () } },
		{ key3_receive->hash (), { key3_open->hash (), key1_send2->hash () } },
		{ key3_epoch->hash (), { key3_receive->hash () } },
	};
	ASSERT_EQ (node.ledger.cache.block_count - 2, dependency_graph.size ());

	// Start an election for the first block of the dependency graph, and ensure all blocks are eventually confirmed
	system.wallet (0)->insert_adhoc (scendere::dev::genesis_key.prv);
	node.block_confirm (gen_send1);

	ASSERT_NO_ERROR (system.poll_until_true (15s, [&] {
		// Not many blocks should be active simultaneously
		EXPECT_LT (node.active.size (), 6);
		scendere::lock_guard<scendere::mutex> guard (node.active.mutex);

		// Ensure that active blocks have their ancestors confirmed
		auto error = std::any_of (dependency_graph.cbegin (), dependency_graph.cend (), [&] (auto entry) {
			if (node.active.blocks.count (entry.first))
			{
				for (auto ancestor : entry.second)
				{
					if (!node.block_confirmed (ancestor))
					{
						return true;
					}
				}
			}
			return false;
		});

		EXPECT_FALSE (error);
		return error || node.ledger.cache.cemented_count == node.ledger.cache.block_count;
	}));
	ASSERT_EQ (node.ledger.cache.cemented_count, node.ledger.cache.block_count);
	ASSERT_TIMELY (5s, node.active.empty ());
}

// Confirm a complex dependency graph. Uses frontiers confirmation which will fail to
// confirm a frontier optimistically then fallback to pessimistic confirmation.
TEST (node, dependency_graph_frontier)
{
	scendere::system system;
	scendere::node_config config (scendere::get_available_port (), system.logging);
	config.frontiers_confirmation = scendere::frontiers_confirmation_mode::disabled;
	auto & node1 = *system.add_node (config);
	config.peering_port = scendere::get_available_port ();
	config.frontiers_confirmation = scendere::frontiers_confirmation_mode::always;
	auto & node2 = *system.add_node (config);

	scendere::state_block_builder builder;
	scendere::keypair key1, key2, key3;

	// Send to key1
	auto gen_send1 = builder.make_block ()
					 .account (scendere::dev::genesis_key.pub)
					 .previous (scendere::dev::genesis->hash ())
					 .representative (scendere::dev::genesis_key.pub)
					 .link (key1.pub)
					 .balance (scendere::dev::constants.genesis_amount - 1)
					 .sign (scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub)
					 .work (*system.work.generate (scendere::dev::genesis->hash ()))
					 .build_shared ();
	// Receive from genesis
	auto key1_open = builder.make_block ()
					 .account (key1.pub)
					 .previous (0)
					 .representative (key1.pub)
					 .link (gen_send1->hash ())
					 .balance (1)
					 .sign (key1.prv, key1.pub)
					 .work (*system.work.generate (key1.pub))
					 .build ();
	// Send to genesis
	auto key1_send1 = builder.make_block ()
					  .account (key1.pub)
					  .previous (key1_open->hash ())
					  .representative (key1.pub)
					  .link (scendere::dev::genesis_key.pub)
					  .balance (0)
					  .sign (key1.prv, key1.pub)
					  .work (*system.work.generate (key1_open->hash ()))
					  .build ();
	// Receive from key1
	auto gen_receive = builder.make_block ()
					   .from (*gen_send1)
					   .previous (gen_send1->hash ())
					   .link (key1_send1->hash ())
					   .balance (scendere::dev::constants.genesis_amount)
					   .sign (scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub)
					   .work (*system.work.generate (gen_send1->hash ()))
					   .build ();
	// Send to key2
	auto gen_send2 = builder.make_block ()
					 .from (*gen_receive)
					 .previous (gen_receive->hash ())
					 .link (key2.pub)
					 .balance (gen_receive->balance ().number () - 2)
					 .sign (scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub)
					 .work (*system.work.generate (gen_receive->hash ()))
					 .build ();
	// Receive from genesis
	auto key2_open = builder.make_block ()
					 .account (key2.pub)
					 .previous (0)
					 .representative (key2.pub)
					 .link (gen_send2->hash ())
					 .balance (2)
					 .sign (key2.prv, key2.pub)
					 .work (*system.work.generate (key2.pub))
					 .build ();
	// Send to key3
	auto key2_send1 = builder.make_block ()
					  .account (key2.pub)
					  .previous (key2_open->hash ())
					  .representative (key2.pub)
					  .link (key3.pub)
					  .balance (1)
					  .sign (key2.prv, key2.pub)
					  .work (*system.work.generate (key2_open->hash ()))
					  .build ();
	// Receive from key2
	auto key3_open = builder.make_block ()
					 .account (key3.pub)
					 .previous (0)
					 .representative (key3.pub)
					 .link (key2_send1->hash ())
					 .balance (1)
					 .sign (key3.prv, key3.pub)
					 .work (*system.work.generate (key3.pub))
					 .build ();
	// Send to key1
	auto key2_send2 = builder.make_block ()
					  .from (*key2_send1)
					  .previous (key2_send1->hash ())
					  .link (key1.pub)
					  .balance (key2_send1->balance ().number () - 1)
					  .sign (key2.prv, key2.pub)
					  .work (*system.work.generate (key2_send1->hash ()))
					  .build ();
	// Receive from key2
	auto key1_receive = builder.make_block ()
						.from (*key1_send1)
						.previous (key1_send1->hash ())
						.link (key2_send2->hash ())
						.balance (key1_send1->balance ().number () + 1)
						.sign (key1.prv, key1.pub)
						.work (*system.work.generate (key1_send1->hash ()))
						.build ();
	// Send to key3
	auto key1_send2 = builder.make_block ()
					  .from (*key1_receive)
					  .previous (key1_receive->hash ())
					  .link (key3.pub)
					  .balance (key1_receive->balance ().number () - 1)
					  .sign (key1.prv, key1.pub)
					  .work (*system.work.generate (key1_receive->hash ()))
					  .build ();
	// Receive from key1
	auto key3_receive = builder.make_block ()
						.from (*key3_open)
						.previous (key3_open->hash ())
						.link (key1_send2->hash ())
						.balance (key3_open->balance ().number () + 1)
						.sign (key3.prv, key3.pub)
						.work (*system.work.generate (key3_open->hash ()))
						.build ();
	// Upgrade key3
	auto key3_epoch = builder.make_block ()
					  .from (*key3_receive)
					  .previous (key3_receive->hash ())
					  .link (node1.ledger.epoch_link (scendere::epoch::epoch_1))
					  .balance (key3_receive->balance ())
					  .sign (scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub)
					  .work (*system.work.generate (key3_receive->hash ()))
					  .build ();

	for (auto const & node : system.nodes)
	{
		auto transaction (node->store.tx_begin_write ());
		ASSERT_EQ (scendere::process_result::progress, node->ledger.process (transaction, *gen_send1).code);
		ASSERT_EQ (scendere::process_result::progress, node->ledger.process (transaction, *key1_open).code);
		ASSERT_EQ (scendere::process_result::progress, node->ledger.process (transaction, *key1_send1).code);
		ASSERT_EQ (scendere::process_result::progress, node->ledger.process (transaction, *gen_receive).code);
		ASSERT_EQ (scendere::process_result::progress, node->ledger.process (transaction, *gen_send2).code);
		ASSERT_EQ (scendere::process_result::progress, node->ledger.process (transaction, *key2_open).code);
		ASSERT_EQ (scendere::process_result::progress, node->ledger.process (transaction, *key2_send1).code);
		ASSERT_EQ (scendere::process_result::progress, node->ledger.process (transaction, *key3_open).code);
		ASSERT_EQ (scendere::process_result::progress, node->ledger.process (transaction, *key2_send2).code);
		ASSERT_EQ (scendere::process_result::progress, node->ledger.process (transaction, *key1_receive).code);
		ASSERT_EQ (scendere::process_result::progress, node->ledger.process (transaction, *key1_send2).code);
		ASSERT_EQ (scendere::process_result::progress, node->ledger.process (transaction, *key3_receive).code);
		ASSERT_EQ (scendere::process_result::progress, node->ledger.process (transaction, *key3_epoch).code);
	}

	// node1 can vote, but only on the first block
	system.wallet (0)->insert_adhoc (scendere::dev::genesis_key.prv);

	ASSERT_TIMELY (10s, node2.active.active (gen_send1->qualified_root ()));
	node1.block_confirm (gen_send1);

	ASSERT_TIMELY (15s, node1.ledger.cache.cemented_count == node1.ledger.cache.block_count);
	ASSERT_TIMELY (15s, node2.ledger.cache.cemented_count == node2.ledger.cache.block_count);
}

namespace scendere
{
TEST (node, deferred_dependent_elections)
{
	scendere::system system;
	scendere::node_flags flags;
	flags.disable_request_loop = true;
	auto & node = *system.add_node (flags);
	auto & node2 = *system.add_node (flags); // node2 will be used to ensure all blocks are being propagated

	scendere::state_block_builder builder;
	scendere::keypair key;
	auto send1 = builder.make_block ()
				 .account (scendere::dev::genesis_key.pub)
				 .previous (scendere::dev::genesis->hash ())
				 .representative (scendere::dev::genesis_key.pub)
				 .link (key.pub)
				 .balance (scendere::dev::constants.genesis_amount - 1)
				 .sign (scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub)
				 .work (*system.work.generate (scendere::dev::genesis->hash ()))
				 .build_shared ();
	auto open = builder.make_block ()
				.account (key.pub)
				.previous (0)
				.representative (key.pub)
				.link (send1->hash ())
				.balance (1)
				.sign (key.prv, key.pub)
				.work (*system.work.generate (key.pub))
				.build_shared ();
	auto send2 = builder.make_block ()
				 .from (*send1)
				 .previous (send1->hash ())
				 .balance (send1->balance ().number () - 1)
				 .link (key.pub)
				 .sign (scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub)
				 .work (*system.work.generate (send1->hash ()))
				 .build_shared ();
	auto receive = builder.make_block ()
				   .from (*open)
				   .previous (open->hash ())
				   .link (send2->hash ())
				   .balance (2)
				   .sign (key.prv, key.pub)
				   .work (*system.work.generate (open->hash ()))
				   .build_shared ();
	auto fork = builder.make_block ()
				.from (*receive)
				.representative (scendere::dev::genesis_key.pub) // was key.pub
				.sign (key.prv, key.pub)
				.build_shared ();
	node.process_local (send1);
	node.block_processor.flush ();
	node.scheduler.flush ();
	auto election_send1 = node.active.election (send1->qualified_root ());
	ASSERT_NE (nullptr, election_send1);

	// Should process and republish but not start an election for any dependent blocks
	node.process_local (open);
	node.process_local (send2);
	node.block_processor.flush ();
	node.scheduler.flush ();
	ASSERT_TRUE (node.block (open->hash ()));
	ASSERT_TRUE (node.block (send2->hash ()));
	ASSERT_FALSE (node.active.active (open->qualified_root ()));
	ASSERT_FALSE (node.active.active (send2->qualified_root ()));
	ASSERT_TIMELY (2s, node2.block (open->hash ()));
	ASSERT_TIMELY (2s, node2.block (send2->hash ()));

	// Re-processing older blocks with updated work also does not start an election
	node.work_generate_blocking (*open, scendere::dev::network_params.work.difficulty (*open) + 1);
	node.process_local (open);
	node.block_processor.flush ();
	node.scheduler.flush ();
	ASSERT_FALSE (node.active.active (open->qualified_root ()));

	// It is however possible to manually start an election from elsewhere
	node.block_confirm (open);
	ASSERT_TRUE (node.active.active (open->qualified_root ()));
	node.active.erase (*open);
	ASSERT_FALSE (node.active.active (open->qualified_root ()));

	/// The election was dropped but it's still not possible to restart it
	node.work_generate_blocking (*open, scendere::dev::network_params.work.difficulty (*open) + 1);
	ASSERT_FALSE (node.active.active (open->qualified_root ()));
	node.process_local (open);
	node.block_processor.flush ();
	node.scheduler.flush ();
	ASSERT_FALSE (node.active.active (open->qualified_root ()));

	// Frontier confirmation also starts elections
	ASSERT_NO_ERROR (system.poll_until_true (5s, [&node, &send2] {
		scendere::unique_lock<scendere::mutex> lock{ node.active.mutex };
		node.active.frontiers_confirmation (lock);
		lock.unlock ();
		return node.active.election (send2->qualified_root ()) != nullptr;
	}));

	// Drop both elections
	node.active.erase (*open);
	ASSERT_FALSE (node.active.active (open->qualified_root ()));
	node.active.erase (*send2);
	ASSERT_FALSE (node.active.active (send2->qualified_root ()));

	// Confirming send1 will automatically start elections for the dependents
	election_send1->force_confirm ();
	ASSERT_TIMELY (2s, node.block_confirmed (send1->hash ()));
	ASSERT_TIMELY (2s, node.active.active (open->qualified_root ()) && node.active.active (send2->qualified_root ()));
	auto election_open = node.active.election (open->qualified_root ());
	ASSERT_NE (nullptr, election_open);
	auto election_send2 = node.active.election (send2->qualified_root ());
	ASSERT_NE (nullptr, election_open);

	// Confirm one of the dependents of the receive but not the other, to ensure both have to be confirmed to start an election on processing
	ASSERT_EQ (scendere::process_result::progress, node.process (*receive).code);
	ASSERT_FALSE (node.active.active (receive->qualified_root ()));
	election_open->force_confirm ();
	ASSERT_TIMELY (2s, node.block_confirmed (open->hash ()));
	ASSERT_FALSE (node.ledger.dependents_confirmed (node.store.tx_begin_read (), *receive));
	std::this_thread::sleep_for (500ms);
	ASSERT_FALSE (node.active.active (receive->qualified_root ()));
	ASSERT_FALSE (node.ledger.rollback (node.store.tx_begin_write (), receive->hash ()));
	ASSERT_FALSE (node.block (receive->hash ()));
	node.process_local (receive);
	node.block_processor.flush ();
	node.scheduler.flush ();
	ASSERT_TRUE (node.block (receive->hash ()));
	ASSERT_FALSE (node.active.active (receive->qualified_root ()));

	// Processing a fork will also not start an election
	ASSERT_EQ (scendere::process_result::fork, node.process (*fork).code);
	node.process_local (fork);
	node.block_processor.flush ();
	node.scheduler.flush ();
	ASSERT_FALSE (node.active.active (receive->qualified_root ()));

	// Confirming the other dependency allows starting an election from a fork
	election_send2->force_confirm ();
	ASSERT_TIMELY (2s, node.block_confirmed (send2->hash ()));
	ASSERT_TIMELY (2s, node.active.active (receive->qualified_root ()));
}
}

TEST (rep_crawler, recently_confirmed)
{
	scendere::system system (1);
	auto & node1 (*system.nodes[0]);
	ASSERT_EQ (1, node1.ledger.cache.block_count);
	auto const block = scendere::dev::genesis;
	node1.active.add_recently_confirmed (block->qualified_root (), block->hash ());
	auto & node2 (*system.add_node ());
	system.wallet (1)->insert_adhoc (scendere::dev::genesis_key.prv);
	auto channel = node1.network.find_channel (node2.network.endpoint ());
	ASSERT_NE (nullptr, channel);
	node1.rep_crawler.query (channel);
	ASSERT_TIMELY (3s, node1.rep_crawler.representative_count () == 1);
}

namespace scendere
{
TEST (rep_crawler, local)
{
	scendere::system system;
	scendere::node_flags flags;
	flags.disable_rep_crawler = true;
	auto & node = *system.add_node (flags);
	auto loopback = std::make_shared<scendere::transport::channel_loopback> (node);
	auto vote = std::make_shared<scendere::vote> (scendere::dev::genesis_key.pub, scendere::dev::genesis_key.prv, 0, 0, std::vector{ scendere::dev::genesis->hash () });
	{
		scendere::lock_guard<scendere::mutex> guard (node.rep_crawler.probable_reps_mutex);
		node.rep_crawler.active.insert (scendere::dev::genesis->hash ());
		node.rep_crawler.responses.emplace_back (loopback, vote);
	}
	node.rep_crawler.validate ();
	ASSERT_EQ (0, node.rep_crawler.representative_count ());
}
}

// Test that a node configured with `enable_pruning` and `max_pruning_age = 1s` will automatically
// prune old confirmed blocks without explicitly saying `node.ledger_pruning` in the unit test
TEST (node, pruning_automatic)
{
	scendere::system system{};

	scendere::node_config node_config{ scendere::get_available_port (), system.logging };
	// TODO: remove after allowing pruned voting
	node_config.enable_voting = false;
	node_config.max_pruning_age = std::chrono::seconds (1);

	scendere::node_flags node_flags{};
	node_flags.enable_pruning = true;

	auto & node1 = *system.add_node (node_config, node_flags);
	scendere::keypair key1{};
	scendere::send_block_builder builder{};
	auto latest_hash = scendere::dev::genesis->hash ();

	auto send1 = builder.make_block ()
				 .previous (latest_hash)
				 .destination (key1.pub)
				 .balance (scendere::dev::constants.genesis_amount - scendere::Gxrb_ratio)
				 .sign (scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub)
				 .work (*system.work.generate (latest_hash))
				 .build_shared ();
	node1.process_active (send1);

	latest_hash = send1->hash ();
	auto send2 = builder.make_block ()
				 .previous (latest_hash)
				 .destination (key1.pub)
				 .balance (0)
				 .sign (scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub)
				 .work (*system.work.generate (latest_hash))
				 .build_shared ();
	node1.process_active (send2);

	// Force-confirm both blocks
	node1.process_confirmed (scendere::election_status{ send1 });
	ASSERT_TIMELY (5s, node1.block_confirmed (send1->hash ()));
	node1.process_confirmed (scendere::election_status{ send2 });
	ASSERT_TIMELY (5s, node1.block_confirmed (send2->hash ()));

	// Check pruning result
	ASSERT_EQ (3, node1.ledger.cache.block_count);
	ASSERT_TIMELY (5s, node1.ledger.cache.pruned_count == 1);
	ASSERT_TIMELY (5s, node1.store.pruned.count (node1.store.tx_begin_read ()) == 1);
	ASSERT_EQ (1, node1.ledger.cache.pruned_count);
	ASSERT_EQ (3, node1.ledger.cache.block_count);

	ASSERT_TRUE (node1.ledger.block_or_pruned_exists (scendere::dev::genesis->hash ()));
	ASSERT_TRUE (node1.ledger.block_or_pruned_exists (send1->hash ()));
	ASSERT_TRUE (node1.ledger.block_or_pruned_exists (send2->hash ()));
}

TEST (node, pruning_age)
{
	scendere::system system{};

	scendere::node_config node_config{ scendere::get_available_port (), system.logging };
	// TODO: remove after allowing pruned voting
	node_config.enable_voting = false;

	scendere::node_flags node_flags{};
	node_flags.enable_pruning = true;

	auto & node1 = *system.add_node (node_config, node_flags);
	scendere::keypair key1{};
	scendere::send_block_builder builder{};
	auto latest_hash = scendere::dev::genesis->hash ();

	auto send1 = builder.make_block ()
				 .previous (latest_hash)
				 .destination (key1.pub)
				 .balance (scendere::dev::constants.genesis_amount - scendere::Gxrb_ratio)
				 .sign (scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub)
				 .work (*system.work.generate (latest_hash))
				 .build_shared ();
	node1.process_active (send1);

	latest_hash = send1->hash ();
	auto send2 = builder.make_block ()
				 .previous (latest_hash)
				 .destination (key1.pub)
				 .balance (0)
				 .sign (scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub)
				 .work (*system.work.generate (latest_hash))
				 .build_shared ();
	node1.process_active (send2);

	// Force-confirm both blocks
	node1.process_confirmed (scendere::election_status{ send1 });
	ASSERT_TIMELY (5s, node1.block_confirmed (send1->hash ()));
	node1.process_confirmed (scendere::election_status{ send2 });
	ASSERT_TIMELY (5s, node1.block_confirmed (send2->hash ()));

	// Three blocks in total, nothing pruned yet
	ASSERT_EQ (0, node1.ledger.cache.pruned_count);
	ASSERT_EQ (3, node1.ledger.cache.block_count);

	// Pruning with default age 1 day
	node1.ledger_pruning (1, true, false);
	ASSERT_EQ (0, node1.ledger.cache.pruned_count);
	ASSERT_EQ (3, node1.ledger.cache.block_count);

	// Pruning with max age 0
	node1.config.max_pruning_age = std::chrono::seconds{ 0 };
	node1.ledger_pruning (1, true, false);
	ASSERT_EQ (1, node1.ledger.cache.pruned_count);
	ASSERT_EQ (3, node1.ledger.cache.block_count);

	ASSERT_TRUE (node1.ledger.block_or_pruned_exists (scendere::dev::genesis->hash ()));
	ASSERT_TRUE (node1.ledger.block_or_pruned_exists (send1->hash ()));
	ASSERT_TRUE (node1.ledger.block_or_pruned_exists (send2->hash ()));
}

// Test that a node configured with `enable_pruning` will
// prune DEEP-enough confirmed blocks by explicitly saying `node.ledger_pruning` in the unit test
TEST (node, pruning_depth)
{
	scendere::system system{};

	scendere::node_config node_config{ scendere::get_available_port (), system.logging };
	// TODO: remove after allowing pruned voting
	node_config.enable_voting = false;

	scendere::node_flags node_flags{};
	node_flags.enable_pruning = true;

	auto & node1 = *system.add_node (node_config, node_flags);
	scendere::keypair key1{};
	scendere::send_block_builder builder{};
	auto latest_hash = scendere::dev::genesis->hash ();

	auto send1 = builder.make_block ()
				 .previous (latest_hash)
				 .destination (key1.pub)
				 .balance (scendere::dev::constants.genesis_amount - scendere::Gxrb_ratio)
				 .sign (scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub)
				 .work (*system.work.generate (latest_hash))
				 .build_shared ();
	node1.process_active (send1);

	latest_hash = send1->hash ();
	auto send2 = builder.make_block ()
				 .previous (latest_hash)
				 .destination (key1.pub)
				 .balance (0)
				 .sign (scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub)
				 .work (*system.work.generate (latest_hash))
				 .build_shared ();
	node1.process_active (send2);

	// Force-confirm both blocks
	node1.process_confirmed (scendere::election_status{ send1 });
	ASSERT_TIMELY (5s, node1.block_confirmed (send1->hash ()));
	node1.process_confirmed (scendere::election_status{ send2 });
	ASSERT_TIMELY (5s, node1.block_confirmed (send2->hash ()));

	// Three blocks in total, nothing pruned yet
	ASSERT_EQ (0, node1.ledger.cache.pruned_count);
	ASSERT_EQ (3, node1.ledger.cache.block_count);

	// Pruning with default depth (unlimited)
	node1.ledger_pruning (1, true, false);
	ASSERT_EQ (0, node1.ledger.cache.pruned_count);
	ASSERT_EQ (3, node1.ledger.cache.block_count);

	// Pruning with max depth 1
	node1.config.max_pruning_depth = 1;
	node1.ledger_pruning (1, true, false);
	ASSERT_EQ (1, node1.ledger.cache.pruned_count);
	ASSERT_EQ (3, node1.ledger.cache.block_count);

	ASSERT_TRUE (node1.ledger.block_or_pruned_exists (scendere::dev::genesis->hash ()));
	ASSERT_TRUE (node1.ledger.block_or_pruned_exists (send1->hash ()));
	ASSERT_TRUE (node1.ledger.block_or_pruned_exists (send2->hash ()));
}
