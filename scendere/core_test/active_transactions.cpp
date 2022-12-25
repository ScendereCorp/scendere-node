#include <scendere/lib/jsonconfig.hpp>
#include <scendere/node/election.hpp>
#include <scendere/test_common/system.hpp>
#include <scendere/test_common/testutil.hpp>

#include <gtest/gtest.h>

#include <numeric>

using namespace std::chrono_literals;

namespace scendere
{
// Tests that an election can be confirmed as the result of a confirmation request
//
// Set-up:
// - node1 with:
//       - enabled frontiers_confirmation (default) -> allows it to confirm blocks and subsequently generates votes
// - node2 with:
//       - disabled rep crawler -> this inhibits node2 from learning that node1 is a rep
//
// Steps:
// - create a block (send1), process it locally (without creating an election for it) on node1
// - process send1 (as incoming from network -- process_active) on node2
// - expect that election has been started for send1 on node2, but no confirmation_requests are sent for it
// - stick genesis key into node1, then add node1 as a rep to node2's probable reps list
// - expect at least one confirmation_request for the election (having been sent to node1 -- which is a rep now)
// - expect a (non-final) vote to come back
// - expected confirmation_request count has increased -- two round trips for the election to get confirmed
// - expect election is confirmed

TEST (active_transactions, confirm_election_by_request)
{
	scendere::system system{};
	auto & node1 = *system.add_node ();

	scendere::node_flags node_flags2{};
	node_flags2.disable_rep_crawler = true;
	auto & node2 = *system.add_node (node_flags2);

	auto send1 = scendere::state_block_builder{}.make_block ().account (scendere::dev::genesis_key.pub).representative (scendere::dev::genesis_key.pub).previous (scendere::dev::genesis->hash ()).link (scendere::public_key ()).balance (scendere::dev::constants.genesis_amount - 100).sign (scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub).work (*system.work.generate (scendere::dev::genesis->hash ())).build_shared ();

	// Process send1 locally on node1
	ASSERT_EQ (scendere::process_result::progress, node1.process (*send1).code);

	// Start an election for send1 on node2
	node2.process_active (send1);
	std::shared_ptr<scendere::election> election{};
	ASSERT_TIMELY (5s, (election = node2.active.election (send1->qualified_root ())) != nullptr);

	// Expect that node2 has nobody to send a confirmation_request to (no reps)
	ASSERT_EQ (0, election->confirmation_request_count);

	// Add key to node1
	system.wallet (0)->insert_adhoc (scendere::dev::genesis_key.prv);

	// Get random peer list (of size 1) from node2 -- so basically just node1
	auto const peers = node2.network.random_set (1);
	ASSERT_FALSE (peers.empty ());

	// Add representative (node1) to disabled rep crawler of node2
	{
		scendere::lock_guard<scendere::mutex> guard (node2.rep_crawler.probable_reps_mutex);
		node2.rep_crawler.probable_reps.emplace (scendere::dev::genesis_key.pub, scendere::dev::constants.genesis_amount, *peers.cbegin ());
	}

	// At least one confirmation request sent to the freshly inserted rep (node1)
	std::size_t confirm_req_count{};
	ASSERT_TIMELY (5s, (confirm_req_count = election->confirmation_request_count) > 0);

	// Expect a (non-final) vote come back
	ASSERT_TIMELY (5s, election->votes ().size () > 1);

	// There need to be 2 round trips in order for the election to get confirmed
	ASSERT_TIMELY (5s, election->confirmation_request_count > confirm_req_count);

	// Expect election was confirmed
	ASSERT_TIMELY (5s, election->confirmed ());
	ASSERT_TIMELY (5s, node1.block_confirmed (send1->hash ()));
	ASSERT_TIMELY (5s, node2.block_confirmed (send1->hash ()));
}
}

namespace scendere
{
TEST (active_transactions, confirm_frontier)
{
	scendere::system system;
	scendere::node_flags node_flags;
	node_flags.disable_request_loop = true;
	// Voting node
	auto & node1 = *system.add_node (node_flags);
	scendere::node_flags node_flags2;
	// The rep crawler would otherwise request confirmations in order to find representatives
	node_flags2.disable_rep_crawler = true;
	auto & node2 = *system.add_node (node_flags2);

	// Add key to node1
	system.wallet (0)->insert_adhoc (scendere::dev::genesis_key.prv);
	// Add representative to disabled rep crawler
	auto peers (node2.network.random_set (1));
	ASSERT_FALSE (peers.empty ());
	{
		scendere::lock_guard<scendere::mutex> guard (node2.rep_crawler.probable_reps_mutex);
		node2.rep_crawler.probable_reps.emplace (scendere::dev::genesis_key.pub, scendere::dev::constants.genesis_amount, *peers.begin ());
	}

	scendere::state_block_builder builder;
	auto send = builder
				.account (scendere::dev::genesis_key.pub)
				.previous (scendere::dev::genesis->hash ())
				.representative (scendere::dev::genesis_key.pub)
				.balance (scendere::dev::constants.genesis_amount - 100)
				.link (scendere::public_key ())
				.sign (scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub)
				.work (*system.work.generate (scendere::dev::genesis->hash ()))
				.build_shared ();
	auto send_copy = builder.make_block ().from (*send).build_shared ();
	ASSERT_EQ (scendere::process_result::progress, node1.process (*send).code);
	node1.confirmation_height_processor.add (send);
	ASSERT_TIMELY (5s, node1.ledger.block_confirmed (node1.store.tx_begin_read (), send->hash ()));
	ASSERT_EQ (scendere::process_result::progress, node2.process (*send_copy).code);
	ASSERT_TIMELY (5s, !node2.active.empty ());
	// Save election to check request count afterwards
	auto election2 = node2.active.election (send->qualified_root ());
	ASSERT_NE (nullptr, election2);
	ASSERT_TIMELY (5s, node2.ledger.cache.cemented_count == 2 && node2.active.empty ());
	ASSERT_GT (election2->confirmation_request_count, 0u);
}
}

TEST (active_transactions, keep_local)
{
	scendere::system system{};

	scendere::node_config node_config{ scendere::get_available_port (), system.logging };
	node_config.enable_voting = false;
	// Bound to 2, won't drop wallet created transactions, but good to test dropping remote
	node_config.active_elections_size = 2;
	// Disable frontier confirmation to allow the test to finish before
	node_config.frontiers_confirmation = scendere::frontiers_confirmation_mode::disabled;

	auto & node = *system.add_node (node_config);
	auto & wallet (*system.wallet (0));

	scendere::keypair key1{};
	scendere::keypair key2{};
	scendere::keypair key3{};
	scendere::keypair key4{};
	scendere::keypair key5{};
	scendere::keypair key6{};

	wallet.insert_adhoc (scendere::dev::genesis_key.prv);
	auto const send1 = wallet.send_action (scendere::dev::genesis_key.pub, key1.pub, node.config.receive_minimum.number ());
	auto const send2 = wallet.send_action (scendere::dev::genesis_key.pub, key2.pub, node.config.receive_minimum.number ());
	auto const send3 = wallet.send_action (scendere::dev::genesis_key.pub, key3.pub, node.config.receive_minimum.number ());
	auto const send4 = wallet.send_action (scendere::dev::genesis_key.pub, key4.pub, node.config.receive_minimum.number ());
	auto const send5 = wallet.send_action (scendere::dev::genesis_key.pub, key5.pub, node.config.receive_minimum.number ());
	auto const send6 = wallet.send_action (scendere::dev::genesis_key.pub, key6.pub, node.config.receive_minimum.number ());

	// force-confirm blocks
	for (auto const & block : { send1, send2, send3, send4, send5, send6 })
	{
		std::shared_ptr<scendere::election> election{};
		ASSERT_TIMELY (5s, (election = node.active.election (block->qualified_root ())) != nullptr);
		node.process_confirmed (scendere::election_status{ block });
		election->force_confirm ();
		ASSERT_TIMELY (5s, node.block_confirmed (block->hash ()));
	}

	scendere::state_block_builder builder{};
	const auto receive1 = builder.make_block ()
						  .account (key1.pub)
						  .previous (0)
						  .representative (key1.pub)
						  .balance (node.config.receive_minimum.number ())
						  .link (send1->hash ())
						  .sign (key1.prv, key1.pub)
						  .work (*system.work.generate (key1.pub))
						  .build_shared ();
	const auto receive2 = builder.make_block ()
						  .account (key2.pub)
						  .previous (0)
						  .representative (key2.pub)
						  .balance (node.config.receive_minimum.number ())
						  .link (send2->hash ())
						  .sign (key2.prv, key2.pub)
						  .work (*system.work.generate (key2.pub))
						  .build_shared ();
	const auto receive3 = builder.make_block ()
						  .account (key3.pub)
						  .previous (0)
						  .representative (key3.pub)
						  .balance (node.config.receive_minimum.number ())
						  .link (send3->hash ())
						  .sign (key3.prv, key3.pub)
						  .work (*system.work.generate (key3.pub))
						  .build_shared ();
	node.process_active (receive1);
	node.process_active (receive2);
	node.process_active (receive3);

	/// bound elections, should drop after one loop
	ASSERT_TIMELY (5s, node.active.size () == node_config.active_elections_size);
	// ASSERT_EQ (1, node.scheduler.size ());
}

TEST (active_transactions, inactive_votes_cache)
{
	scendere::system system (1);
	auto & node = *system.nodes[0];
	scendere::block_hash latest (node.latest (scendere::dev::genesis_key.pub));
	scendere::keypair key;
	auto send = scendere::send_block_builder ()
				.previous (latest)
				.destination (key.pub)
				.balance (scendere::dev::constants.genesis_amount - 100)
				.sign (scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub)
				.work (*system.work.generate (latest))
				.build_shared ();
	auto vote (std::make_shared<scendere::vote> (scendere::dev::genesis_key.pub, scendere::dev::genesis_key.prv, scendere::vote::timestamp_max, scendere::vote::duration_max, std::vector<scendere::block_hash> (1, send->hash ())));
	node.vote_processor.vote (vote, std::make_shared<scendere::transport::channel_loopback> (node));
	ASSERT_TIMELY (5s, node.active.inactive_votes_cache_size () == 1);
	node.process_active (send);
	node.block_processor.flush ();
	ASSERT_TIMELY (5s, node.ledger.block_confirmed (node.store.tx_begin_read (), send->hash ()));
	ASSERT_EQ (1, node.stats.count (scendere::stat::type::election, scendere::stat::detail::vote_cached));
}

TEST (active_transactions, inactive_votes_cache_non_final)
{
	scendere::system system (1);
	auto & node = *system.nodes[0];
	scendere::block_hash latest (node.latest (scendere::dev::genesis_key.pub));
	scendere::keypair key;
	auto send = scendere::send_block_builder ()
				.previous (latest)
				.destination (key.pub)
				.balance (scendere::dev::constants.genesis_amount - 100)
				.sign (scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub)
				.work (*system.work.generate (latest))
				.build_shared ();
	auto vote (std::make_shared<scendere::vote> (scendere::dev::genesis_key.pub, scendere::dev::genesis_key.prv, 0, 0, std::vector<scendere::block_hash> (1, send->hash ()))); // Non-final vote
	node.vote_processor.vote (vote, std::make_shared<scendere::transport::channel_loopback> (node));
	ASSERT_TIMELY (5s, node.active.inactive_votes_cache_size () == 1);
	node.process_active (send);
	node.block_processor.flush ();
	ASSERT_TIMELY (5s, node.stats.count (scendere::stat::type::election, scendere::stat::detail::vote_cached) == 1);
	auto election = node.active.election (send->qualified_root ());
	ASSERT_NE (nullptr, election);
	ASSERT_FALSE (election->confirmed ());
	ASSERT_EQ (scendere::dev::constants.genesis_amount - 100, election->tally ().begin ()->first);
}

TEST (active_transactions, inactive_votes_cache_fork)
{
	scendere::system system{ 1 };
	auto & node = *system.nodes[0];

	auto const latest = node.latest (scendere::dev::genesis_key.pub);
	scendere::keypair key{};

	scendere::send_block_builder builder{};
	auto send1 = builder.make_block ()
				 .previous (latest)
				 .destination (key.pub)
				 .balance (scendere::dev::constants.genesis_amount - 100)
				 .sign (scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub)
				 .work (*system.work.generate (latest))
				 .build_shared ();

	auto send2 = builder.make_block ()
				 .previous (latest)
				 .destination (key.pub)
				 .balance (scendere::dev::constants.genesis_amount - 200)
				 .sign (scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub)
				 .work (*system.work.generate (latest))
				 .build_shared ();

	auto const vote = std::make_shared<scendere::vote> (scendere::dev::genesis_key.pub, scendere::dev::genesis_key.prv, scendere::vote::timestamp_max, scendere::vote::duration_max, std::vector<scendere::block_hash> (1, send1->hash ()));
	node.vote_processor.vote (vote, std::make_shared<scendere::transport::channel_loopback> (node));
	ASSERT_TIMELY (5s, node.active.inactive_votes_cache_size () == 1);

	node.process_active (send2);

	std::shared_ptr<scendere::election> election{};
	ASSERT_TIMELY (5s, (election = node.active.election (send1->qualified_root ())) != nullptr);

	node.process_active (send1);
	ASSERT_TIMELY (5s, election->blocks ().size () == 2);
	ASSERT_TIMELY (5s, node.block_confirmed (send1->hash ()));
	ASSERT_EQ (1, node.stats.count (scendere::stat::type::election, scendere::stat::detail::vote_cached));
}

TEST (active_transactions, inactive_votes_cache_existing_vote)
{
	scendere::system system;
	scendere::node_config node_config (scendere::get_available_port (), system.logging);
	node_config.frontiers_confirmation = scendere::frontiers_confirmation_mode::disabled;
	auto & node = *system.add_node (node_config);
	scendere::block_hash latest (node.latest (scendere::dev::genesis_key.pub));
	scendere::keypair key;
	scendere::block_builder builder;
	auto send = builder.send ()
				.previous (latest)
				.destination (key.pub)
				.balance (scendere::dev::constants.genesis_amount - 100 * scendere::Gxrb_ratio)
				.sign (scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub)
				.work (*system.work.generate (latest))
				.build_shared ();
	auto open = builder.state ()
				.account (key.pub)
				.previous (0)
				.representative (key.pub)
				.balance (100 * scendere::Gxrb_ratio)
				.link (send->hash ())
				.sign (key.prv, key.pub)
				.work (*system.work.generate (key.pub))
				.build_shared ();
	node.process_active (send);
	node.block_processor.add (open);
	node.block_processor.flush ();
	ASSERT_TIMELY (5s, node.active.size () == 1);
	auto election (node.active.election (send->qualified_root ()));
	ASSERT_NE (nullptr, election);
	ASSERT_GT (node.weight (key.pub), node.minimum_principal_weight ());
	// Insert vote
	auto vote1 (std::make_shared<scendere::vote> (key.pub, key.prv, scendere::vote::timestamp_min * 1, 0, std::vector<scendere::block_hash> (1, send->hash ())));
	node.vote_processor.vote (vote1, std::make_shared<scendere::transport::channel_loopback> (node));
	ASSERT_TIMELY (5s, election->votes ().size () == 2);
	ASSERT_EQ (1, node.stats.count (scendere::stat::type::election, scendere::stat::detail::vote_new));
	auto last_vote1 (election->votes ()[key.pub]);
	ASSERT_EQ (send->hash (), last_vote1.hash);
	ASSERT_EQ (scendere::vote::timestamp_min * 1, last_vote1.timestamp);
	// Attempt to change vote with inactive_votes_cache
	scendere::unique_lock<scendere::mutex> active_lock (node.active.mutex);
	node.active.add_inactive_votes_cache (active_lock, send->hash (), key.pub, 0);
	active_lock.unlock ();
	auto cache (node.active.find_inactive_votes_cache (send->hash ()));
	active_lock.lock ();
	ASSERT_EQ (1, cache.voters.size ());
	election->insert_inactive_votes_cache (cache);
	// Check that election data is not changed
	ASSERT_EQ (2, election->votes ().size ());
	auto last_vote2 (election->votes ()[key.pub]);
	ASSERT_EQ (last_vote1.hash, last_vote2.hash);
	ASSERT_EQ (last_vote1.timestamp, last_vote2.timestamp);
	ASSERT_EQ (last_vote1.time, last_vote2.time);
	ASSERT_EQ (0, node.stats.count (scendere::stat::type::election, scendere::stat::detail::vote_cached));
}

// Test disabled because it's failing intermittently.
// PR in which it got disabled: https://github.com/scenderecurrency/scendere-node/pull/3629
// Issue for investigating it: https://github.com/scenderecurrency/scendere-node/issues/3632
TEST (active_transactions, DISABLED_inactive_votes_cache_multiple_votes)
{
	scendere::system system;
	scendere::node_config node_config (scendere::get_available_port (), system.logging);
	node_config.frontiers_confirmation = scendere::frontiers_confirmation_mode::disabled;
	auto & node = *system.add_node (node_config);
	scendere::block_hash latest (node.latest (scendere::dev::genesis_key.pub));
	scendere::keypair key1;
	scendere::block_builder builder;
	auto send1 = builder.send ()
				 .previous (latest)
				 .destination (key1.pub)
				 .balance (scendere::dev::constants.genesis_amount - 100 * scendere::Gxrb_ratio)
				 .sign (scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub)
				 .work (*system.work.generate (latest))
				 .build_shared ();
	auto send2 = builder.send ()
				 .previous (send1->hash ())
				 .destination (key1.pub)
				 .balance (100 * scendere::Gxrb_ratio)
				 .sign (scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub)
				 .work (*system.work.generate (send1->hash ()))
				 .build_shared ();
	auto open = builder.state ()
				.account (key1.pub)
				.previous (0)
				.representative (key1.pub)
				.balance (100 * scendere::Gxrb_ratio)
				.link (send1->hash ())
				.sign (key1.prv, key1.pub)
				.work (*system.work.generate (key1.pub))
				.build_shared ();
	node.block_processor.add (send1);
	node.block_processor.add (send2);
	node.block_processor.add (open);
	node.block_processor.flush ();
	// Process votes
	auto vote1 (std::make_shared<scendere::vote> (key1.pub, key1.prv, 0, 0, std::vector<scendere::block_hash> (1, send1->hash ())));
	node.vote_processor.vote (vote1, std::make_shared<scendere::transport::channel_loopback> (node));
	auto vote2 (std::make_shared<scendere::vote> (scendere::dev::genesis_key.pub, scendere::dev::genesis_key.prv, 0, 0, std::vector<scendere::block_hash> (1, send1->hash ())));
	node.vote_processor.vote (vote2, std::make_shared<scendere::transport::channel_loopback> (node));
	ASSERT_TIMELY (5s, node.active.find_inactive_votes_cache (send1->hash ()).voters.size () == 2);
	ASSERT_EQ (1, node.active.inactive_votes_cache_size ());
	node.scheduler.activate (scendere::dev::genesis_key.pub, node.store.tx_begin_read ());
	node.scheduler.flush ();
	auto election = node.active.election (send1->qualified_root ());
	ASSERT_NE (nullptr, election);
	ASSERT_EQ (3, election->votes ().size ()); // 2 votes and 1 default not_an_acount
	ASSERT_EQ (2, node.stats.count (scendere::stat::type::election, scendere::stat::detail::vote_cached));
}

TEST (active_transactions, inactive_votes_cache_election_start)
{
	scendere::system system;
	scendere::node_config node_config (scendere::get_available_port (), system.logging);
	node_config.frontiers_confirmation = scendere::frontiers_confirmation_mode::disabled;
	auto & node = *system.add_node (node_config);
	scendere::block_hash latest (node.latest (scendere::dev::genesis_key.pub));
	scendere::keypair key1, key2;
	scendere::send_block_builder send_block_builder;
	scendere::state_block_builder state_block_builder;
	auto send1 = send_block_builder.make_block ()
				 .previous (latest)
				 .destination (key1.pub)
				 .balance (scendere::dev::constants.genesis_amount - 5000 * scendere::Gxrb_ratio)
				 .sign (scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub)
				 .work (*system.work.generate (latest))
				 .build_shared ();
	auto send2 = send_block_builder.make_block ()
				 .previous (send1->hash ())
				 .destination (key2.pub)
				 .balance (scendere::dev::constants.genesis_amount - 10000 * scendere::Gxrb_ratio)
				 .sign (scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub)
				 .work (*system.work.generate (send1->hash ()))
				 .build_shared ();
	auto open1 = state_block_builder.make_block ()
				 .account (key1.pub)
				 .previous (0)
				 .representative (key1.pub)
				 .balance (5000 * scendere::Gxrb_ratio)
				 .link (send1->hash ())
				 .sign (key1.prv, key1.pub)
				 .work (*system.work.generate (key1.pub))
				 .build_shared ();
	auto open2 = state_block_builder.make_block ()
				 .account (key2.pub)
				 .previous (0)
				 .representative (key2.pub)
				 .balance (5000 * scendere::Gxrb_ratio)
				 .link (send2->hash ())
				 .sign (key2.prv, key2.pub)
				 .work (*system.work.generate (key2.pub))
				 .build_shared ();
	node.block_processor.add (send1);
	node.block_processor.add (send2);
	node.block_processor.add (open1);
	node.block_processor.add (open2);
	node.block_processor.flush ();
	ASSERT_TIMELY (5s, 5 == node.ledger.cache.block_count);
	ASSERT_TRUE (node.active.empty ());
	ASSERT_EQ (1, node.ledger.cache.cemented_count);
	// These blocks will be processed later
	auto send3 = send_block_builder.make_block ()
				 .previous (send2->hash ())
				 .destination (scendere::keypair ().pub)
				 .balance (send2->balance ().number () - 1)
				 .sign (scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub)
				 .work (*system.work.generate (send2->hash ()))
				 .build_shared ();
	auto send4 = send_block_builder.make_block ()
				 .previous (send3->hash ())
				 .destination (scendere::keypair ().pub)
				 .balance (send3->balance ().number () - 1)
				 .sign (scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub)
				 .work (*system.work.generate (send3->hash ()))
				 .build_shared ();
	// Inactive votes
	std::vector<scendere::block_hash> hashes{ open1->hash (), open2->hash (), send4->hash () };
	auto vote1 (std::make_shared<scendere::vote> (key1.pub, key1.prv, 0, 0, hashes));
	node.vote_processor.vote (vote1, std::make_shared<scendere::transport::channel_loopback> (node));
	ASSERT_TIMELY (5s, node.active.inactive_votes_cache_size () == 3);
	ASSERT_TRUE (node.active.empty ());
	ASSERT_EQ (1, node.ledger.cache.cemented_count);
	// 2 votes are required to start election (dev network)
	auto vote2 (std::make_shared<scendere::vote> (key2.pub, key2.prv, 0, 0, hashes));
	node.vote_processor.vote (vote2, std::make_shared<scendere::transport::channel_loopback> (node));
	// Only open1 & open2 blocks elections should start (send4 is missing previous block in ledger)
	ASSERT_TIMELY (5s, 2 == node.active.size ());
	// Confirm elections with weight quorum
	auto vote0 (std::make_shared<scendere::vote> (scendere::dev::genesis_key.pub, scendere::dev::genesis_key.prv, scendere::vote::timestamp_max, scendere::vote::duration_max, hashes)); // Final vote for confirmation
	node.vote_processor.vote (vote0, std::make_shared<scendere::transport::channel_loopback> (node));
	ASSERT_TIMELY (5s, node.active.empty ());
	ASSERT_TIMELY (5s, 5 == node.ledger.cache.cemented_count);
	// A late block arrival also checks the inactive votes cache
	ASSERT_TRUE (node.active.empty ());
	auto send4_cache (node.active.find_inactive_votes_cache (send4->hash ()));
	ASSERT_EQ (3, send4_cache.voters.size ());
	ASSERT_TRUE (send4_cache.status.bootstrap_started);
	ASSERT_TRUE (send4_cache.status.confirmed);
	ASSERT_TRUE (send4_cache.status.election_started); // already marked even though the block does not exist
	node.process_active (send3);
	node.block_processor.flush ();
	// An election is started for send6 but does not confirm
	ASSERT_TIMELY (5s, 1 == node.active.size ());
	node.vote_processor.flush ();
	ASSERT_FALSE (node.block_confirmed_or_being_confirmed (node.store.tx_begin_read (), send3->hash ()));
	// send7 cannot be voted on but an election should be started from inactive votes
	ASSERT_FALSE (node.ledger.dependents_confirmed (node.store.tx_begin_read (), *send4));
	node.process_active (send4);
	node.block_processor.flush ();
	ASSERT_TIMELY (5s, 7 == node.ledger.cache.cemented_count);
}

namespace scendere
{
TEST (active_transactions, vote_replays)
{
	scendere::system system;
	scendere::node_config node_config (scendere::get_available_port (), system.logging);
	node_config.enable_voting = false;
	node_config.frontiers_confirmation = scendere::frontiers_confirmation_mode::disabled;
	auto & node = *system.add_node (node_config);
	scendere::keypair key;
	scendere::state_block_builder builder;
	auto send1 = builder.make_block ()
				 .account (scendere::dev::genesis_key.pub)
				 .previous (scendere::dev::genesis->hash ())
				 .representative (scendere::dev::genesis_key.pub)
				 .balance (scendere::dev::constants.genesis_amount - scendere::Gxrb_ratio)
				 .link (key.pub)
				 .sign (scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub)
				 .work (*system.work.generate (scendere::dev::genesis->hash ()))
				 .build_shared ();
	ASSERT_NE (nullptr, send1);
	auto open1 = builder.make_block ()
				 .account (key.pub)
				 .previous (0)
				 .representative (key.pub)
				 .balance (scendere::Gxrb_ratio)
				 .link (send1->hash ())
				 .sign (key.prv, key.pub)
				 .work (*system.work.generate (key.pub))
				 .build_shared ();
	ASSERT_NE (nullptr, open1);
	node.process_active (send1);
	node.process_active (open1);
	scendere::blocks_confirm (node, { send1, open1 });
	ASSERT_EQ (2, node.active.size ());
	// First vote is not a replay and confirms the election, second vote should be a replay since the election has confirmed but not yet removed
	auto vote_send1 (std::make_shared<scendere::vote> (scendere::dev::genesis_key.pub, scendere::dev::genesis_key.prv, scendere::vote::timestamp_max, scendere::vote::duration_max, send1));
	ASSERT_EQ (scendere::vote_code::vote, node.active.vote (vote_send1));
	ASSERT_EQ (2, node.active.size ());
	ASSERT_EQ (scendere::vote_code::replay, node.active.vote (vote_send1));
	// Wait until the election is removed, at which point the vote is still a replay since it's been recently confirmed
	ASSERT_TIMELY (3s, node.active.size () == 1);
	ASSERT_EQ (scendere::vote_code::replay, node.active.vote (vote_send1));
	// Open new account
	auto vote_open1 (std::make_shared<scendere::vote> (scendere::dev::genesis_key.pub, scendere::dev::genesis_key.prv, scendere::vote::timestamp_max, scendere::vote::duration_max, open1));
	ASSERT_EQ (scendere::vote_code::vote, node.active.vote (vote_open1));
	ASSERT_EQ (scendere::vote_code::replay, node.active.vote (vote_open1));
	ASSERT_TIMELY (3s, node.active.empty ());
	ASSERT_EQ (scendere::vote_code::replay, node.active.vote (vote_open1));
	ASSERT_EQ (scendere::Gxrb_ratio, node.ledger.weight (key.pub));

	auto send2 = builder.make_block ()
				 .account (key.pub)
				 .previous (open1->hash ())
				 .representative (key.pub)
				 .balance (scendere::Gxrb_ratio - 1)
				 .link (key.pub)
				 .sign (key.prv, key.pub)
				 .work (*system.work.generate (open1->hash ()))
				 .build_shared ();
	ASSERT_NE (nullptr, send2);
	node.process_active (send2);
	scendere::blocks_confirm (node, { send2 });
	ASSERT_EQ (1, node.active.size ());
	auto vote1_send2 (std::make_shared<scendere::vote> (scendere::dev::genesis_key.pub, scendere::dev::genesis_key.prv, scendere::vote::timestamp_max, scendere::vote::duration_max, send2));
	auto vote2_send2 (std::make_shared<scendere::vote> (key.pub, key.prv, 0, 0, send2));
	ASSERT_EQ (scendere::vote_code::vote, node.active.vote (vote2_send2));
	ASSERT_EQ (1, node.active.size ());
	ASSERT_EQ (scendere::vote_code::replay, node.active.vote (vote2_send2));
	ASSERT_EQ (1, node.active.size ());
	ASSERT_EQ (scendere::vote_code::vote, node.active.vote (vote1_send2));
	ASSERT_EQ (1, node.active.size ());
	ASSERT_EQ (scendere::vote_code::replay, node.active.vote (vote1_send2));
	ASSERT_TIMELY (3s, node.active.empty ());
	ASSERT_EQ (0, node.active.size ());
	ASSERT_EQ (scendere::vote_code::replay, node.active.vote (vote1_send2));
	ASSERT_EQ (scendere::vote_code::replay, node.active.vote (vote2_send2));

	// Removing blocks as recently confirmed makes every vote indeterminate
	{
		scendere::lock_guard<scendere::mutex> guard (node.active.mutex);
		node.active.recently_confirmed.clear ();
	}
	ASSERT_EQ (scendere::vote_code::indeterminate, node.active.vote (vote_send1));
	ASSERT_EQ (scendere::vote_code::indeterminate, node.active.vote (vote_open1));
	ASSERT_EQ (scendere::vote_code::indeterminate, node.active.vote (vote1_send2));
	ASSERT_EQ (scendere::vote_code::indeterminate, node.active.vote (vote2_send2));
}
}

// Tests that blocks are correctly cleared from the duplicate filter for unconfirmed elections
TEST (active_transactions, dropped_cleanup)
{
	scendere::system system;
	scendere::node_flags flags;
	flags.disable_request_loop = true;
	auto & node (*system.add_node (flags));

	// Add to network filter to ensure proper cleanup after the election is dropped
	std::vector<uint8_t> block_bytes;
	{
		scendere::vectorstream stream (block_bytes);
		scendere::dev::genesis->serialize (stream);
	}
	ASSERT_FALSE (node.network.publish_filter.apply (block_bytes.data (), block_bytes.size ()));
	ASSERT_TRUE (node.network.publish_filter.apply (block_bytes.data (), block_bytes.size ()));

	node.block_confirm (scendere::dev::genesis);
	node.scheduler.flush ();
	auto election = node.active.election (scendere::dev::genesis->qualified_root ());
	ASSERT_NE (nullptr, election);

	// Not yet removed
	ASSERT_TRUE (node.network.publish_filter.apply (block_bytes.data (), block_bytes.size ()));
	ASSERT_EQ (1, node.active.blocks.count (scendere::dev::genesis->hash ()));

	// Now simulate dropping the election
	ASSERT_FALSE (election->confirmed ());
	node.active.erase (*scendere::dev::genesis);

	// The filter must have been cleared
	ASSERT_FALSE (node.network.publish_filter.apply (block_bytes.data (), block_bytes.size ()));

	// An election was recently dropped
	ASSERT_EQ (1, node.stats.count (scendere::stat::type::election, scendere::stat::detail::election_drop_all));

	// Block cleared from active
	ASSERT_EQ (0, node.active.blocks.count (scendere::dev::genesis->hash ()));

	// Repeat test for a confirmed election
	ASSERT_TRUE (node.network.publish_filter.apply (block_bytes.data (), block_bytes.size ()));
	node.block_confirm (scendere::dev::genesis);
	node.scheduler.flush ();
	election = node.active.election (scendere::dev::genesis->qualified_root ());
	ASSERT_NE (nullptr, election);
	election->force_confirm ();
	ASSERT_TRUE (election->confirmed ());
	node.active.erase (*scendere::dev::genesis);

	// The filter should not have been cleared
	ASSERT_TRUE (node.network.publish_filter.apply (block_bytes.data (), block_bytes.size ()));

	// Not dropped
	ASSERT_EQ (1, node.stats.count (scendere::stat::type::election, scendere::stat::detail::election_drop_all));

	// Block cleared from active
	ASSERT_EQ (0, node.active.blocks.count (scendere::dev::genesis->hash ()));
}

TEST (active_transactions, republish_winner)
{
	scendere::system system;
	scendere::node_config node_config{ scendere::get_available_port (), system.logging };
	node_config.frontiers_confirmation = scendere::frontiers_confirmation_mode::disabled;
	auto & node1 = *system.add_node (node_config);
	node_config.peering_port = scendere::get_available_port ();
	auto & node2 = *system.add_node (node_config);

	scendere::keypair key;
	scendere::state_block_builder builder;
	auto send1 = builder.make_block ()
				 .account (scendere::dev::genesis_key.pub)
				 .previous (scendere::dev::genesis->hash ())
				 .representative (scendere::dev::genesis_key.pub)
				 .balance (scendere::dev::constants.genesis_amount - scendere::Gxrb_ratio)
				 .link (key.pub)
				 .sign (scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub)
				 .work (*system.work.generate (scendere::dev::genesis->hash ()))
				 .build_shared ();

	node1.process_active (send1);
	node1.block_processor.flush ();
	ASSERT_TIMELY (3s, node2.stats.count (scendere::stat::type::message, scendere::stat::detail::publish, scendere::stat::dir::in) == 1);

	// Several forks
	for (auto i (0); i < 5; i++)
	{
		auto fork = builder.make_block ()
					.account (scendere::dev::genesis_key.pub)
					.previous (scendere::dev::genesis->hash ())
					.representative (scendere::dev::genesis_key.pub)
					.balance (scendere::dev::constants.genesis_amount - 1 - i)
					.link (key.pub)
					.sign (scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub)
					.work (*system.work.generate (scendere::dev::genesis->hash ()))
					.build_shared ();
		node1.process_active (fork);
	}
	node1.block_processor.flush ();
	ASSERT_TIMELY (3s, !node1.active.empty ());
	ASSERT_EQ (1, node2.stats.count (scendere::stat::type::message, scendere::stat::detail::publish, scendere::stat::dir::in));

	// Process new fork with vote to change winner
	auto fork = builder.make_block ()
				.account (scendere::dev::genesis_key.pub)
				.previous (scendere::dev::genesis->hash ())
				.representative (scendere::dev::genesis_key.pub)
				.balance (scendere::dev::constants.genesis_amount - 2 * scendere::Gxrb_ratio)
				.link (key.pub)
				.sign (scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub)
				.work (*system.work.generate (scendere::dev::genesis->hash ()))
				.build_shared ();

	node1.process_active (fork);
	node1.block_processor.flush ();
	auto election = node1.active.election (fork->qualified_root ());
	ASSERT_NE (nullptr, election);
	auto vote = std::make_shared<scendere::vote> (scendere::dev::genesis_key.pub, scendere::dev::genesis_key.prv, scendere::vote::timestamp_max, scendere::vote::duration_max, std::vector<scendere::block_hash>{ fork->hash () });
	node1.vote_processor.vote (vote, std::make_shared<scendere::transport::channel_loopback> (node1));
	node1.vote_processor.flush ();
	node1.block_processor.flush ();
	ASSERT_TIMELY (3s, election->confirmed ());
	ASSERT_EQ (fork->hash (), election->status.winner->hash ());
	ASSERT_TIMELY (3s, node2.block_confirmed (fork->hash ()));
}

TEST (active_transactions, fork_filter_cleanup)
{
	scendere::system system{};

	scendere::node_config node_config{ scendere::get_available_port (), system.logging };
	node_config.frontiers_confirmation = scendere::frontiers_confirmation_mode::disabled;

	auto & node1 = *system.add_node (node_config);
	scendere::keypair key{};
	scendere::state_block_builder builder{};
	auto const latest_hash = scendere::dev::genesis->hash ();

	auto send1 = builder.make_block ()
				 .previous (latest_hash)
				 .account (scendere::dev::genesis_key.pub)
				 .representative (scendere::dev::genesis_key.pub)
				 .balance (scendere::dev::constants.genesis_amount - scendere::Gxrb_ratio)
				 .link (key.pub)
				 .sign (scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub)
				 .work (*system.work.generate (latest_hash))
				 .build_shared ();

	std::vector<uint8_t> send_block_bytes{};
	{
		scendere::vectorstream stream{ send_block_bytes };
		send1->serialize (stream);
	}

	// Generate 10 forks to prevent new block insertion to election
	for (auto i = 0; i < 10; ++i)
	{
		auto fork = builder.make_block ()
					.previous (latest_hash)
					.account (scendere::dev::genesis_key.pub)
					.representative (scendere::dev::genesis_key.pub)
					.balance (scendere::dev::constants.genesis_amount - 1 - i)
					.link (key.pub)
					.sign (scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub)
					.work (*system.work.generate (latest_hash))
					.build_shared ();

		node1.process_active (fork);
		ASSERT_TIMELY (5s, node1.active.election (fork->qualified_root ()) != nullptr);
	}

	// All forks were merged into the same election
	std::shared_ptr<scendere::election> election{};
	ASSERT_TIMELY (5s, (election = node1.active.election (send1->qualified_root ())) != nullptr);
	ASSERT_TIMELY (5s, election->blocks ().size () == 10);
	ASSERT_EQ (1, node1.active.size ());

	// Instantiate a new node
	node_config.peering_port = scendere::get_available_port ();
	auto & node2 = *system.add_node (node_config);

	// Process the first initial block on node2
	node2.process_active (send1);
	ASSERT_TIMELY (5s, node2.active.election (send1->qualified_root ()) != nullptr);

	// TODO: questions: why doesn't node2 pick up "fork" from node1? because it connected to node1 after node1
	//                  already process_active()d the fork? shouldn't it broadcast it anyway, even later?
	//
	//                  how about node1 picking up "send1" from node2? we know it does because we assert at
	//                  the end that it is within node1's AEC, but why node1.block_count doesn't increase?
	//
	ASSERT_TIMELY (5s, node2.ledger.cache.block_count == 2);
	ASSERT_TIMELY (5s, node1.ledger.cache.block_count == 2);

	// Block is erased from the duplicate filter
	ASSERT_TIMELY (5s, node1.network.publish_filter.apply (send_block_bytes.data (), send_block_bytes.size ()));
}

TEST (active_transactions, fork_replacement_tally)
{
	scendere::system system;
	scendere::node_config node_config (scendere::get_available_port (), system.logging);
	node_config.frontiers_confirmation = scendere::frontiers_confirmation_mode::disabled;
	auto & node1 (*system.add_node (node_config));

	size_t reps_count = 20;
	size_t const max_blocks = 10;
	std::vector<scendere::keypair> keys (reps_count);
	auto latest (scendere::dev::genesis->hash ());
	auto balance (scendere::dev::constants.genesis_amount);
	auto amount (node1.minimum_principal_weight ());
	scendere::state_block_builder builder;

	// Create 20 representatives & confirm blocks
	for (auto i (0); i < reps_count; i++)
	{
		balance -= amount + i;
		auto send = builder.make_block ()
					.account (scendere::dev::genesis_key.pub)
					.previous (latest)
					.representative (scendere::dev::genesis_key.pub)
					.balance (balance)
					.link (keys[i].pub)
					.sign (scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub)
					.work (*system.work.generate (latest))
					.build_shared ();
		node1.process_active (send);
		latest = send->hash ();
		auto open = builder.make_block ()
					.account (keys[i].pub)
					.previous (0)
					.representative (keys[i].pub)
					.balance (amount + i)
					.link (send->hash ())
					.sign (keys[i].prv, keys[i].pub)
					.work (*system.work.generate (keys[i].pub))
					.build_shared ();
		node1.process_active (open);
		// Confirmation
		auto vote (std::make_shared<scendere::vote> (scendere::dev::genesis_key.pub, scendere::dev::genesis_key.prv, scendere::vote::timestamp_max, scendere::vote::duration_max, std::vector<scendere::block_hash>{ send->hash (), open->hash () }));
		node1.vote_processor.vote (vote, std::make_shared<scendere::transport::channel_loopback> (node1));
	}
	node1.block_processor.flush ();
	ASSERT_TIMELY (5s, node1.ledger.cache.cemented_count == 1 + 2 * reps_count);

	scendere::keypair key;
	auto send_last = builder.make_block ()
					 .account (scendere::dev::genesis_key.pub)
					 .previous (latest)
					 .representative (scendere::dev::genesis_key.pub)
					 .balance (balance - 2 * scendere::Gxrb_ratio)
					 .link (key.pub)
					 .sign (scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub)
					 .work (*system.work.generate (latest))
					 .build_shared ();

	// Forks without votes
	for (auto i (0); i < reps_count; i++)
	{
		auto fork = builder.make_block ()
					.account (scendere::dev::genesis_key.pub)
					.previous (latest)
					.representative (scendere::dev::genesis_key.pub)
					.balance (balance - scendere::Gxrb_ratio - i)
					.link (key.pub)
					.sign (scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub)
					.work (*system.work.generate (latest))
					.build_shared ();
		node1.process_active (fork);
	}
	node1.block_processor.flush ();
	ASSERT_TIMELY (3s, !node1.active.empty ());
	// Check overflow of blocks
	auto election (node1.active.election (send_last->qualified_root ()));
	ASSERT_NE (nullptr, election);
	ASSERT_EQ (max_blocks, election->blocks ().size ());

	// Generate forks with votes to prevent new block insertion to election
	for (auto i (0); i < reps_count; i++)
	{
		auto fork = builder.make_block ()
					.account (scendere::dev::genesis_key.pub)
					.previous (latest)
					.representative (scendere::dev::genesis_key.pub)
					.balance (balance - 1 - i)
					.link (key.pub)
					.sign (scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub)
					.work (*system.work.generate (latest))
					.build_shared ();
		auto vote (std::make_shared<scendere::vote> (keys[i].pub, keys[i].prv, 0, 0, std::vector<scendere::block_hash>{ fork->hash () }));
		node1.vote_processor.vote (vote, std::make_shared<scendere::transport::channel_loopback> (node1));
		node1.vote_processor.flush ();
		node1.process_active (fork);
	}
	node1.block_processor.flush ();
	// Check overflow of blocks
	ASSERT_EQ (max_blocks, election->blocks ().size ());
	// Check that only max weight blocks remains (and start winner)
	auto votes1 (election->votes ());
	ASSERT_EQ (max_blocks, votes1.size ());
	for (auto i (max_blocks + 1); i < reps_count; i++)
	{
		ASSERT_TRUE (votes1.find (keys[i].pub) != votes1.end ());
	}

	// Process correct block
	node_config.peering_port = scendere::get_available_port ();
	auto & node2 (*system.add_node (node_config));
	node2.network.flood_block (send_last);
	ASSERT_TIMELY (3s, node1.stats.count (scendere::stat::type::message, scendere::stat::detail::publish, scendere::stat::dir::in) > 0);
	node1.block_processor.flush ();
	std::this_thread::sleep_for (50ms);

	// Correct block without votes is ignored
	auto blocks1 (election->blocks ());
	ASSERT_EQ (max_blocks, blocks1.size ());
	ASSERT_FALSE (blocks1.find (send_last->hash ()) != blocks1.end ());

	// Process vote for correct block & replace existing lowest tally block
	auto vote (std::make_shared<scendere::vote> (scendere::dev::genesis_key.pub, scendere::dev::genesis_key.prv, 0, 0, std::vector<scendere::block_hash>{ send_last->hash () }));
	node1.vote_processor.vote (vote, std::make_shared<scendere::transport::channel_loopback> (node1));
	node1.vote_processor.flush ();
	node2.network.flood_block (send_last);
	ASSERT_TIMELY (3s, node1.stats.count (scendere::stat::type::message, scendere::stat::detail::publish, scendere::stat::dir::in) > 1);
	node1.block_processor.flush ();
	std::this_thread::sleep_for (50ms);

	auto blocks2 (election->blocks ());
	ASSERT_EQ (max_blocks, blocks2.size ());
	ASSERT_TRUE (blocks2.find (send_last->hash ()) != blocks2.end ());
	auto votes2 (election->votes ());
	ASSERT_EQ (max_blocks, votes2.size ());
	for (auto i (max_blocks + 2); i < reps_count; i++)
	{
		ASSERT_TRUE (votes2.find (keys[i].pub) != votes2.end ());
	}
	ASSERT_FALSE (votes2.find (keys[max_blocks].pub) != votes2.end ());
	ASSERT_FALSE (votes2.find (keys[max_blocks + 1].pub) != votes2.end ());
	ASSERT_TRUE (votes2.find (scendere::dev::genesis_key.pub) != votes2.end ());
}

namespace scendere
{
// Blocks that won an election must always be seen as confirming or cemented
TEST (active_transactions, confirmation_consistency)
{
	scendere::system system;
	scendere::node_config node_config (scendere::get_available_port (), system.logging);
	node_config.frontiers_confirmation = scendere::frontiers_confirmation_mode::disabled;
	auto & node = *system.add_node (node_config);
	system.wallet (0)->insert_adhoc (scendere::dev::genesis_key.prv);
	for (unsigned i = 0; i < 10; ++i)
	{
		auto block (system.wallet (0)->send_action (scendere::dev::genesis_key.pub, scendere::public_key (), node.config.receive_minimum.number ()));
		ASSERT_NE (nullptr, block);
		system.deadline_set (5s);
		while (!node.ledger.block_confirmed (node.store.tx_begin_read (), block->hash ()))
		{
			node.scheduler.activate (scendere::dev::genesis_key.pub, node.store.tx_begin_read ());
			ASSERT_NO_ERROR (system.poll (5ms));
		}
		ASSERT_NO_ERROR (system.poll_until_true (1s, [&node, &block, i] {
			scendere::lock_guard<scendere::mutex> guard (node.active.mutex);
			EXPECT_EQ (i + 1, node.active.recently_confirmed.size ());
			EXPECT_EQ (block->qualified_root (), node.active.recently_confirmed.back ().first);
			return i + 1 == node.active.recently_cemented.size (); // done after a callback
		}));
	}
}
}

// Test disabled because it's failing intermittently.
// PR in which it got disabled: https://github.com/scenderecurrency/scendere-node/pull/3629
// Issue for investigating it: https://github.com/scenderecurrency/scendere-node/issues/3634
TEST (active_transactions, DISABLED_confirm_new)
{
	scendere::system system (1);
	auto & node1 = *system.nodes[0];
	auto send = scendere::send_block_builder ()
				.previous (scendere::dev::genesis->hash ())
				.destination (scendere::public_key ())
				.balance (scendere::dev::constants.genesis_amount - 100)
				.sign (scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub)
				.work (*system.work.generate (scendere::dev::genesis->hash ()))
				.build_shared ();
	node1.process_active (send);
	node1.block_processor.flush ();
	node1.scheduler.flush ();
	ASSERT_EQ (1, node1.active.size ());
	auto & node2 = *system.add_node ();
	// Add key to node2
	system.wallet (1)->insert_adhoc (scendere::dev::genesis_key.prv);
	// Let node2 know about the block
	ASSERT_TIMELY (5s, node2.block (send->hash ()));
	// Wait confirmation
	ASSERT_TIMELY (5s, node1.ledger.cache.cemented_count == 2 && node2.ledger.cache.cemented_count == 2);
}

// Ensures votes are tallied on election::publish even if no vote is inserted through inactive_votes_cache
TEST (active_transactions, conflicting_block_vote_existing_election)
{
	scendere::system system;
	scendere::node_flags node_flags;
	node_flags.disable_request_loop = true;
	auto & node = *system.add_node (node_flags);
	scendere::keypair key;
	scendere::state_block_builder builder;
	auto send = builder.make_block ()
				.account (scendere::dev::genesis_key.pub)
				.previous (scendere::dev::genesis->hash ())
				.representative (scendere::dev::genesis_key.pub)
				.balance (scendere::dev::constants.genesis_amount - 100)
				.link (key.pub)
				.sign (scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub)
				.work (*system.work.generate (scendere::dev::genesis->hash ()))
				.build_shared ();
	auto fork = builder.make_block ()
				.account (scendere::dev::genesis_key.pub)
				.previous (scendere::dev::genesis->hash ())
				.representative (scendere::dev::genesis_key.pub)
				.balance (scendere::dev::constants.genesis_amount - 200)
				.link (key.pub)
				.sign (scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub)
				.work (*system.work.generate (scendere::dev::genesis->hash ()))
				.build_shared ();
	auto vote_fork (std::make_shared<scendere::vote> (scendere::dev::genesis_key.pub, scendere::dev::genesis_key.prv, scendere::vote::timestamp_max, scendere::vote::duration_max, fork));

	ASSERT_EQ (scendere::process_result::progress, node.process_local (send).code);
	node.scheduler.flush ();
	ASSERT_EQ (1, node.active.size ());

	// Vote for conflicting block, but the block does not yet exist in the ledger
	node.active.vote (vote_fork);

	// Block now gets processed
	ASSERT_EQ (scendere::process_result::fork, node.process_local (fork).code);

	// Election must be confirmed
	auto election (node.active.election (fork->qualified_root ()));
	ASSERT_NE (nullptr, election);
	ASSERT_TIMELY (3s, election->confirmed ());
}

TEST (active_transactions, activate_account_chain)
{
	scendere::system system;
	scendere::node_flags flags;
	scendere::node_config config (scendere::get_available_port (), system.logging);
	config.frontiers_confirmation = scendere::frontiers_confirmation_mode::disabled;
	auto & node = *system.add_node (config, flags);

	scendere::keypair key;
	scendere::state_block_builder builder;
	auto send = builder.make_block ()
				.account (scendere::dev::genesis_key.pub)
				.previous (scendere::dev::genesis->hash ())
				.representative (scendere::dev::genesis_key.pub)
				.link (scendere::dev::genesis_key.pub)
				.balance (scendere::dev::constants.genesis_amount - 1)
				.sign (scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub)
				.work (*system.work.generate (scendere::dev::genesis->hash ()))
				.build ();
	auto send2 = builder.make_block ()
				 .account (scendere::dev::genesis_key.pub)
				 .previous (send->hash ())
				 .representative (scendere::dev::genesis_key.pub)
				 .link (key.pub)
				 .balance (scendere::dev::constants.genesis_amount - 2)
				 .sign (scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub)
				 .work (*system.work.generate (send->hash ()))
				 .build ();
	auto send3 = builder.make_block ()
				 .account (scendere::dev::genesis_key.pub)
				 .previous (send2->hash ())
				 .representative (scendere::dev::genesis_key.pub)
				 .link (key.pub)
				 .balance (scendere::dev::constants.genesis_amount - 3)
				 .sign (scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub)
				 .work (*system.work.generate (send2->hash ()))
				 .build ();
	auto open = builder.make_block ()
				.account (key.pub)
				.previous (0)
				.representative (key.pub)
				.link (send2->hash ())
				.balance (1)
				.sign (key.prv, key.pub)
				.work (*system.work.generate (key.pub))
				.build ();
	auto receive = builder.make_block ()
				   .account (key.pub)
				   .previous (open->hash ())
				   .representative (key.pub)
				   .link (send3->hash ())
				   .balance (2)
				   .sign (key.prv, key.pub)
				   .work (*system.work.generate (open->hash ()))
				   .build ();
	ASSERT_EQ (scendere::process_result::progress, node.process (*send).code);
	ASSERT_EQ (scendere::process_result::progress, node.process (*send2).code);
	ASSERT_EQ (scendere::process_result::progress, node.process (*send3).code);
	ASSERT_EQ (scendere::process_result::progress, node.process (*open).code);
	ASSERT_EQ (scendere::process_result::progress, node.process (*receive).code);

	node.scheduler.activate (scendere::dev::genesis_key.pub, node.store.tx_begin_read ());
	node.scheduler.flush ();
	auto election1 = node.active.election (send->qualified_root ());
	ASSERT_EQ (1, node.active.size ());
	ASSERT_EQ (1, election1->blocks ().count (send->hash ()));
	node.scheduler.activate (scendere::dev::genesis_key.pub, node.store.tx_begin_read ());
	auto election2 = node.active.election (send->qualified_root ());
	ASSERT_EQ (election2, election1);
	election1->force_confirm ();
	ASSERT_TIMELY (3s, node.block_confirmed (send->hash ()));
	// On cementing, the next election is started
	ASSERT_TIMELY (3s, node.active.active (send2->qualified_root ()));
	node.scheduler.activate (scendere::dev::genesis_key.pub, node.store.tx_begin_read ());
	auto election3 = node.active.election (send2->qualified_root ());
	ASSERT_NE (nullptr, election3);
	ASSERT_EQ (1, election3->blocks ().count (send2->hash ()));
	election3->force_confirm ();
	ASSERT_TIMELY (3s, node.block_confirmed (send2->hash ()));
	// On cementing, the next election is started
	ASSERT_TIMELY (3s, node.active.active (open->qualified_root ()));
	ASSERT_TIMELY (3s, node.active.active (send3->qualified_root ()));
	node.scheduler.activate (scendere::dev::genesis_key.pub, node.store.tx_begin_read ());
	auto election4 = node.active.election (send3->qualified_root ());
	ASSERT_NE (nullptr, election4);
	ASSERT_EQ (1, election4->blocks ().count (send3->hash ()));
	node.scheduler.activate (key.pub, node.store.tx_begin_read ());
	auto election5 = node.active.election (open->qualified_root ());
	ASSERT_NE (nullptr, election5);
	ASSERT_EQ (1, election5->blocks ().count (open->hash ()));
	election5->force_confirm ();
	ASSERT_TIMELY (3s, node.block_confirmed (open->hash ()));
	// Until send3 is also confirmed, the receive block should not activate
	std::this_thread::sleep_for (200ms);
	node.scheduler.activate (key.pub, node.store.tx_begin_read ());
	election4->force_confirm ();
	ASSERT_TIMELY (3s, node.block_confirmed (send3->hash ()));
	ASSERT_TIMELY (3s, node.active.active (receive->qualified_root ()));
}

TEST (active_transactions, activate_inactive)
{
	scendere::system system;
	scendere::node_flags flags;
	scendere::node_config config (scendere::get_available_port (), system.logging);
	config.frontiers_confirmation = scendere::frontiers_confirmation_mode::disabled;
	auto & node = *system.add_node (config, flags);

	scendere::keypair key;
	scendere::state_block_builder builder;
	auto send = builder.make_block ()
				.account (scendere::dev::genesis_key.pub)
				.previous (scendere::dev::genesis->hash ())
				.representative (scendere::dev::genesis_key.pub)
				.link (key.pub)
				.balance (scendere::dev::constants.genesis_amount - 1)
				.sign (scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub)
				.work (*system.work.generate (scendere::dev::genesis->hash ()))
				.build_shared ();
	auto send2 = builder.make_block ()
				 .account (scendere::dev::genesis_key.pub)
				 .previous (send->hash ())
				 .representative (scendere::dev::genesis_key.pub)
				 .link (scendere::keypair ().pub)
				 .balance (scendere::dev::constants.genesis_amount - 2)
				 .sign (scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub)
				 .work (*system.work.generate (send->hash ()))
				 .build_shared ();
	auto open = builder.make_block ()
				.account (key.pub)
				.previous (0)
				.representative (key.pub)
				.link (send->hash ())
				.balance (1)
				.sign (key.prv, key.pub)
				.work (*system.work.generate (key.pub))
				.build_shared ();

	ASSERT_EQ (scendere::process_result::progress, node.process (*send).code);
	ASSERT_EQ (scendere::process_result::progress, node.process (*send2).code);
	ASSERT_EQ (scendere::process_result::progress, node.process (*open).code);

	node.block_confirm (send2);
	auto election = node.active.election (send2->qualified_root ());
	ASSERT_NE (nullptr, election);
	election->force_confirm ();

	ASSERT_TIMELY (3s, !node.confirmation_height_processor.is_processing_added_block (send2->hash ()));
	ASSERT_TRUE (node.block_confirmed (send2->hash ()));
	ASSERT_TRUE (node.block_confirmed (send->hash ()));

	ASSERT_EQ (1, node.stats.count (scendere::stat::type::confirmation_observer, scendere::stat::detail::inactive_conf_height, scendere::stat::dir::out));
	ASSERT_EQ (1, node.stats.count (scendere::stat::type::confirmation_observer, scendere::stat::detail::active_quorum, scendere::stat::dir::out));
	ASSERT_EQ (0, node.stats.count (scendere::stat::type::confirmation_observer, scendere::stat::detail::active_conf_height, scendere::stat::dir::out));

	// The first block was not active so no activation takes place
	ASSERT_FALSE (node.active.active (open->qualified_root ()) || node.block_confirmed_or_being_confirmed (node.store.tx_begin_read (), open->hash ()));
}

namespace scendere
{
TEST (active_transactions, pessimistic_elections)
{
	scendere::system system;
	scendere::node_flags flags;
	scendere::node_config config (scendere::get_available_port (), system.logging);
	config.frontiers_confirmation = scendere::frontiers_confirmation_mode::disabled;
	auto & node = *system.add_node (config, flags);

	scendere::keypair key;
	scendere::state_block_builder builder;
	auto send = builder.make_block ()
				.account (scendere::dev::genesis_key.pub)
				.previous (scendere::dev::genesis->hash ())
				.representative (scendere::dev::genesis_key.pub)
				.link (scendere::dev::genesis_key.pub)
				.balance (scendere::dev::constants.genesis_amount - 1)
				.sign (scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub)
				.work (*system.work.generate (scendere::dev::genesis->hash ()))
				.build_shared ();

	ASSERT_EQ (scendere::process_result::progress, node.process (*send).code);

	auto send2 = builder.make_block ()
				 .account (scendere::dev::genesis_key.pub)
				 .previous (send->hash ())
				 .representative (scendere::dev::genesis_key.pub)
				 .link (key.pub)
				 .balance (scendere::dev::constants.genesis_amount - 2)
				 .sign (scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub)
				 .work (*system.work.generate (send->hash ()))
				 .build ();

	ASSERT_EQ (scendere::process_result::progress, node.process (*send2).code);

	auto open = builder.make_block ()
				.account (key.pub)
				.previous (0)
				.representative (key.pub)
				.link (send2->hash ())
				.balance (1)
				.sign (key.prv, key.pub)
				.work (*system.work.generate (key.pub))
				.build_shared ();

	ASSERT_EQ (scendere::process_result::progress, node.process (*open).code);

	// This should only cement the first block in genesis account
	uint64_t election_count = 0;
	// Make dummy election with winner.
	{
		scendere::election election1 (
		node, send, [] (auto const &) {}, [] (auto const &) {}, scendere::election_behavior::normal);
		scendere::election election2 (
		node, open, [] (auto const &) {}, [] (auto const &) {}, scendere::election_behavior::normal);
		node.active.add_expired_optimistic_election (election1);
		node.active.add_expired_optimistic_election (election2);
	}
	node.active.confirm_expired_frontiers_pessimistically (node.store.tx_begin_read (), 100, election_count);
	ASSERT_EQ (1, election_count);
	ASSERT_EQ (2, node.active.expired_optimistic_election_infos.size ());
	ASSERT_EQ (2, node.active.expired_optimistic_election_infos.size ());
	auto election_started_it = node.active.expired_optimistic_election_infos.get<scendere::active_transactions::tag_election_started> ().begin ();
	ASSERT_EQ (election_started_it->account, scendere::dev::genesis->account ());
	ASSERT_EQ (election_started_it->election_started, true);
	ASSERT_EQ ((++election_started_it)->election_started, false);

	// No new elections should get started yet
	node.active.confirm_expired_frontiers_pessimistically (node.store.tx_begin_read (), 100, election_count);
	ASSERT_EQ (1, election_count);
	ASSERT_EQ (2, node.active.expired_optimistic_election_infos.size ());
	ASSERT_EQ (node.active.expired_optimistic_election_infos_size, node.active.expired_optimistic_election_infos.size ());

	ASSERT_EQ (1, node.active.size ());
	auto election = node.active.election (send->qualified_root ());
	ASSERT_NE (nullptr, election);
	election->force_confirm ();

	ASSERT_TIMELY (3s, node.block_confirmed (send->hash ()) && !node.confirmation_height_processor.is_processing_added_block (send->hash ()));

	scendere::confirmation_height_info genesis_confirmation_height_info;
	scendere::confirmation_height_info key1_confirmation_height_info;
	{
		auto transaction = node.store.tx_begin_read ();
		node.store.confirmation_height.get (transaction, scendere::dev::genesis->account (), genesis_confirmation_height_info);
		ASSERT_EQ (2, genesis_confirmation_height_info.height);
		node.store.confirmation_height.get (transaction, key.pub, key1_confirmation_height_info);
		ASSERT_EQ (0, key1_confirmation_height_info.height);
	}

	// Activation of cemented frontier successor should get started after the first pessimistic block is confirmed
	ASSERT_TIMELY (10s, node.active.active (send2->qualified_root ()));

	node.active.confirm_expired_frontiers_pessimistically (node.store.tx_begin_read (), 100, election_count);
	ASSERT_EQ (1, election_count);
	ASSERT_EQ (2, node.active.expired_optimistic_election_infos.size ());

	// Confirm it
	election = node.active.election (send2->qualified_root ());
	ASSERT_NE (nullptr, election);
	election->force_confirm ();

	ASSERT_TIMELY (3s, node.block_confirmed (send2->hash ()));

	{
		auto transaction = node.store.tx_begin_read ();
		node.store.confirmation_height.get (transaction, scendere::dev::genesis->account (), genesis_confirmation_height_info);
		ASSERT_EQ (3, genesis_confirmation_height_info.height);
		node.store.confirmation_height.get (transaction, key.pub, key1_confirmation_height_info);
		ASSERT_EQ (0, key1_confirmation_height_info.height);
	}

	// Wait until activation of destination account is done.
	ASSERT_TIMELY (10s, node.active.active (open->qualified_root ()));

	// Election count should not increase, but the elections should be marked as started for that account afterwards
	ASSERT_EQ (election_started_it->election_started, false);
	node.active.confirm_expired_frontiers_pessimistically (node.store.tx_begin_read (), 100, election_count);
	ASSERT_EQ (1, election_count);
	ASSERT_EQ (2, node.active.expired_optimistic_election_infos.size ());
	node.active.confirm_expired_frontiers_pessimistically (node.store.tx_begin_read (), 100, election_count);

	election = node.active.election (open->qualified_root ());
	ASSERT_NE (nullptr, election);
	election->force_confirm ();

	ASSERT_TIMELY (3s, node.block_confirmed (open->hash ()));

	{
		auto transaction = node.store.tx_begin_read ();
		node.store.confirmation_height.get (transaction, scendere::dev::genesis->account (), genesis_confirmation_height_info);
		ASSERT_EQ (3, genesis_confirmation_height_info.height);
		node.store.confirmation_height.get (transaction, key.pub, key1_confirmation_height_info);
		ASSERT_EQ (1, key1_confirmation_height_info.height);
	}

	// Sanity check that calling it again on a fully cemented chain has no adverse effects.
	node.active.confirm_expired_frontiers_pessimistically (node.store.tx_begin_read (), 100, election_count);
	ASSERT_EQ (1, election_count);
	ASSERT_EQ (2, node.active.expired_optimistic_election_infos.size ());
}
}

TEST (active_transactions, list_active)
{
	scendere::system system (1);
	auto & node = *system.nodes[0];

	scendere::keypair key;
	scendere::state_block_builder builder;
	auto send = builder.make_block ()
				.account (scendere::dev::genesis_key.pub)
				.previous (scendere::dev::genesis->hash ())
				.representative (scendere::dev::genesis_key.pub)
				.link (scendere::dev::genesis_key.pub)
				.balance (scendere::dev::constants.genesis_amount - 1)
				.sign (scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub)
				.work (*system.work.generate (scendere::dev::genesis->hash ()))
				.build_shared ();

	ASSERT_EQ (scendere::process_result::progress, node.process (*send).code);

	auto send2 = builder.make_block ()
				 .account (scendere::dev::genesis_key.pub)
				 .previous (send->hash ())
				 .representative (scendere::dev::genesis_key.pub)
				 .link (key.pub)
				 .balance (scendere::dev::constants.genesis_amount - 2)
				 .sign (scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub)
				 .work (*system.work.generate (send->hash ()))
				 .build_shared ();

	ASSERT_EQ (scendere::process_result::progress, node.process (*send2).code);

	auto open = builder.make_block ()
				.account (key.pub)
				.previous (0)
				.representative (key.pub)
				.link (send2->hash ())
				.balance (1)
				.sign (key.prv, key.pub)
				.work (*system.work.generate (key.pub))
				.build_shared ();

	ASSERT_EQ (scendere::process_result::progress, node.process (*open).code);

	scendere::blocks_confirm (node, { send, send2, open });
	ASSERT_EQ (3, node.active.size ());
	ASSERT_EQ (1, node.active.list_active (1).size ());
	ASSERT_EQ (2, node.active.list_active (2).size ());
	ASSERT_EQ (3, node.active.list_active (3).size ());
	ASSERT_EQ (3, node.active.list_active (4).size ());
	ASSERT_EQ (3, node.active.list_active (99999).size ());
	ASSERT_EQ (3, node.active.list_active ().size ());

	auto active = node.active.list_active ();
}

TEST (active_transactions, vacancy)
{
	scendere::system system;
	scendere::node_config config{ scendere::get_available_port (), system.logging };
	config.active_elections_size = 1;
	auto & node = *system.add_node (config);
	scendere::state_block_builder builder;
	auto send = builder.make_block ()
				.account (scendere::dev::genesis_key.pub)
				.previous (scendere::dev::genesis->hash ())
				.representative (scendere::dev::genesis_key.pub)
				.link (scendere::dev::genesis_key.pub)
				.balance (scendere::dev::constants.genesis_amount - scendere::Gxrb_ratio)
				.sign (scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub)
				.work (*system.work.generate (scendere::dev::genesis->hash ()))
				.build_shared ();
	std::atomic<bool> updated = false;
	node.active.vacancy_update = [&updated] () { updated = true; };
	ASSERT_EQ (scendere::process_result::progress, node.process (*send).code);
	ASSERT_EQ (1, node.active.vacancy ());
	ASSERT_EQ (0, node.active.size ());
	node.scheduler.activate (scendere::dev::genesis_key.pub, node.store.tx_begin_read ());
	ASSERT_TIMELY (1s, updated);
	updated = false;
	ASSERT_EQ (0, node.active.vacancy ());
	ASSERT_EQ (1, node.active.size ());
	auto election1 = node.active.election (send->qualified_root ());
	ASSERT_NE (nullptr, election1);
	election1->force_confirm ();
	ASSERT_TIMELY (1s, updated);
	ASSERT_EQ (1, node.active.vacancy ());
	ASSERT_EQ (0, node.active.size ());
}

// Ensure transactions in excess of capacity are removed in fifo order
TEST (active_transactions, fifo)
{
	scendere::system system{};

	scendere::node_config config{ scendere::get_available_port (), system.logging };
	config.active_elections_size = 1;

	auto & node = *system.add_node (config);
	auto latest_hash = scendere::dev::genesis->hash ();
	scendere::keypair key0{};
	scendere::state_block_builder builder{};

	// Construct two pending entries that can be received simultaneously
	auto send1 = builder.make_block ()
				 .previous (latest_hash)
				 .account (scendere::dev::genesis_key.pub)
				 .representative (scendere::dev::genesis_key.pub)
				 .link (key0.pub)
				 .balance (scendere::dev::constants.genesis_amount - 1)
				 .sign (scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub)
				 .work (*system.work.generate (latest_hash))
				 .build_shared ();
	ASSERT_EQ (scendere::process_result::progress, node.process (*send1).code);
	node.process_confirmed (scendere::election_status{ send1 });
	ASSERT_TIMELY (5s, node.block_confirmed (send1->hash ()));

	scendere::keypair key1{};
	latest_hash = send1->hash ();
	auto send2 = builder.make_block ()
				 .previous (latest_hash)
				 .account (scendere::dev::genesis_key.pub)
				 .representative (scendere::dev::genesis_key.pub)
				 .link (key1.pub)
				 .balance (scendere::dev::constants.genesis_amount - 2)
				 .sign (scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub)
				 .work (*system.work.generate (latest_hash))
				 .build_shared ();
	ASSERT_EQ (scendere::process_result::progress, node.process (*send2).code);
	node.process_confirmed (scendere::election_status{ send2 });
	ASSERT_TIMELY (5s, node.block_confirmed (send2->hash ()));

	auto receive1 = builder.make_block ()
					.previous (0)
					.account (key0.pub)
					.representative (scendere::dev::genesis_key.pub)
					.link (send1->hash ())
					.balance (1)
					.sign (key0.prv, key0.pub)
					.work (*system.work.generate (key0.pub))
					.build_shared ();
	ASSERT_EQ (scendere::process_result::progress, node.process (*receive1).code);

	auto receive2 = builder.make_block ()
					.previous (0)
					.account (key1.pub)
					.representative (scendere::dev::genesis_key.pub)
					.link (send2->hash ())
					.balance (1)
					.sign (key1.prv, key1.pub)
					.work (*system.work.generate (key1.pub))
					.build_shared ();
	ASSERT_EQ (scendere::process_result::progress, node.process (*receive2).code);

	// Ensure first transaction becomes active
	node.scheduler.manual (receive1);
	ASSERT_TIMELY (5s, node.active.election (receive1->qualified_root ()) != nullptr);

	// Ensure second transaction becomes active
	node.scheduler.manual (receive2);
	ASSERT_TIMELY (5s, node.active.election (receive2->qualified_root ()) != nullptr);

	// Ensure excess transactions get trimmed
	ASSERT_TIMELY (5s, node.active.size () == 1);

	// Ensure overflow stats have been incremented
	ASSERT_EQ (1, node.stats.count (scendere::stat::type::election, scendere::stat::detail::election_drop_overflow));

	// Ensure the surviving transaction is the least recently inserted
	ASSERT_TIMELY (1s, node.active.election (receive2->qualified_root ()) != nullptr);
}
