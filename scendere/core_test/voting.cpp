#include <scendere/node/common.hpp>
#include <scendere/node/voting.hpp>
#include <scendere/test_common/system.hpp>
#include <scendere/test_common/testutil.hpp>

#include <gtest/gtest.h>

using namespace std::chrono_literals;

namespace scendere
{
TEST (local_vote_history, basic)
{
	scendere::local_vote_history history{ scendere::dev::network_params.voting };
	ASSERT_FALSE (history.exists (1));
	ASSERT_FALSE (history.exists (2));
	ASSERT_TRUE (history.votes (1).empty ());
	ASSERT_TRUE (history.votes (2).empty ());
	auto vote1a (std::make_shared<scendere::vote> ());
	ASSERT_EQ (0, history.size ());
	history.add (1, 2, vote1a);
	ASSERT_EQ (1, history.size ());
	ASSERT_TRUE (history.exists (1));
	ASSERT_FALSE (history.exists (2));
	auto votes1a (history.votes (1));
	ASSERT_FALSE (votes1a.empty ());
	ASSERT_EQ (1, history.votes (1, 2).size ());
	ASSERT_TRUE (history.votes (1, 1).empty ());
	ASSERT_TRUE (history.votes (1, 3).empty ());
	ASSERT_TRUE (history.votes (2).empty ());
	ASSERT_EQ (1, votes1a.size ());
	ASSERT_EQ (vote1a, votes1a[0]);
	auto vote1b (std::make_shared<scendere::vote> ());
	history.add (1, 2, vote1b);
	auto votes1b (history.votes (1));
	ASSERT_EQ (1, votes1b.size ());
	ASSERT_EQ (vote1b, votes1b[0]);
	ASSERT_NE (vote1a, votes1b[0]);
	auto vote2 (std::make_shared<scendere::vote> ());
	vote2->account.dwords[0]++;
	ASSERT_EQ (1, history.size ());
	history.add (1, 2, vote2);
	ASSERT_EQ (2, history.size ());
	auto votes2 (history.votes (1));
	ASSERT_EQ (2, votes2.size ());
	ASSERT_TRUE (vote1b == votes2[0] || vote1b == votes2[1]);
	ASSERT_TRUE (vote2 == votes2[0] || vote2 == votes2[1]);
	auto vote3 (std::make_shared<scendere::vote> ());
	vote3->account.dwords[1]++;
	history.add (1, 3, vote3);
	ASSERT_EQ (1, history.size ());
	auto votes3 (history.votes (1));
	ASSERT_EQ (1, votes3.size ());
	ASSERT_TRUE (vote3 == votes3[0]);
}
}

TEST (vote_generator, cache)
{
	scendere::system system (1);
	auto & node (*system.nodes[0]);
	auto epoch1 = system.upgrade_genesis_epoch (node, scendere::epoch::epoch_1);
	system.wallet (0)->insert_adhoc (scendere::dev::genesis_key.prv);
	node.active.generator.add (epoch1->root (), epoch1->hash ());
	ASSERT_TIMELY (1s, !node.history.votes (epoch1->root (), epoch1->hash ()).empty ());
	auto votes (node.history.votes (epoch1->root (), epoch1->hash ()));
	ASSERT_FALSE (votes.empty ());
	ASSERT_TRUE (std::any_of (votes[0]->begin (), votes[0]->end (), [hash = epoch1->hash ()] (scendere::block_hash const & hash_a) { return hash_a == hash; }));
}

TEST (vote_generator, multiple_representatives)
{
	scendere::system system (1);
	auto & node (*system.nodes[0]);
	scendere::keypair key1, key2, key3;
	auto & wallet (*system.wallet (0));
	wallet.insert_adhoc (scendere::dev::genesis_key.prv);
	wallet.insert_adhoc (key1.prv);
	wallet.insert_adhoc (key2.prv);
	wallet.insert_adhoc (key3.prv);
	auto const amount = 100 * scendere::Gxrb_ratio;
	wallet.send_sync (scendere::dev::genesis_key.pub, key1.pub, amount);
	wallet.send_sync (scendere::dev::genesis_key.pub, key2.pub, amount);
	wallet.send_sync (scendere::dev::genesis_key.pub, key3.pub, amount);
	ASSERT_TIMELY (3s, node.balance (key1.pub) == amount && node.balance (key2.pub) == amount && node.balance (key3.pub) == amount);
	wallet.change_sync (key1.pub, key1.pub);
	wallet.change_sync (key2.pub, key2.pub);
	wallet.change_sync (key3.pub, key3.pub);
	ASSERT_TRUE (node.weight (key1.pub) == amount && node.weight (key2.pub) == amount && node.weight (key3.pub) == amount);
	node.wallets.compute_reps ();
	ASSERT_EQ (4, node.wallets.reps ().voting);
	auto hash = wallet.send_sync (scendere::dev::genesis_key.pub, scendere::dev::genesis_key.pub, 1);
	auto send = node.block (hash);
	ASSERT_NE (nullptr, send);
	ASSERT_TIMELY (5s, node.history.votes (send->root (), send->hash ()).size () == 4);
	auto votes (node.history.votes (send->root (), send->hash ()));
	for (auto const & account : { key1.pub, key2.pub, key3.pub, scendere::dev::genesis_key.pub })
	{
		auto existing (std::find_if (votes.begin (), votes.end (), [&account] (std::shared_ptr<scendere::vote> const & vote_a) -> bool {
			return vote_a->account == account;
		}));
		ASSERT_NE (votes.end (), existing);
	}
}

TEST (vote_generator, session)
{
	scendere::system system (1);
	auto node (system.nodes[0]);
	system.wallet (0)->insert_adhoc (scendere::dev::genesis_key.prv);
	scendere::vote_generator_session generator_session (node->active.generator);
	boost::thread thread ([node, &generator_session] () {
		scendere::thread_role::set (scendere::thread_role::name::request_loop);
		generator_session.add (scendere::dev::genesis->account (), scendere::dev::genesis->hash ());
		ASSERT_EQ (0, node->stats.count (scendere::stat::type::vote, scendere::stat::detail::vote_indeterminate));
		generator_session.flush ();
	});
	thread.join ();
	ASSERT_TIMELY (2s, 1 == node->stats.count (scendere::stat::type::vote, scendere::stat::detail::vote_indeterminate));
}

TEST (vote_spacing, basic)
{
	scendere::vote_spacing spacing{ std::chrono::milliseconds{ 100 } };
	scendere::root root1{ 1 };
	scendere::root root2{ 2 };
	scendere::block_hash hash3{ 3 };
	scendere::block_hash hash4{ 4 };
	scendere::block_hash hash5{ 5 };
	ASSERT_EQ (0, spacing.size ());
	ASSERT_TRUE (spacing.votable (root1, hash3));
	spacing.flag (root1, hash3);
	ASSERT_EQ (1, spacing.size ());
	ASSERT_TRUE (spacing.votable (root1, hash3));
	ASSERT_FALSE (spacing.votable (root1, hash4));
	spacing.flag (root2, hash5);
	ASSERT_EQ (2, spacing.size ());
}

TEST (vote_spacing, prune)
{
	auto length = std::chrono::milliseconds{ 100 };
	scendere::vote_spacing spacing{ length };
	scendere::root root1{ 1 };
	scendere::root root2{ 2 };
	scendere::block_hash hash3{ 3 };
	scendere::block_hash hash4{ 4 };
	spacing.flag (root1, hash3);
	ASSERT_EQ (1, spacing.size ());
	std::this_thread::sleep_for (length);
	spacing.flag (root2, hash4);
	ASSERT_EQ (1, spacing.size ());
}

TEST (vote_spacing, vote_generator)
{
	scendere::node_config config;
	config.frontiers_confirmation = scendere::frontiers_confirmation_mode::disabled;
	scendere::system system;
	scendere::node_flags node_flags;
	node_flags.disable_search_pending = true;
	auto & node = *system.add_node (config, node_flags);
	auto & wallet = *system.wallet (0);
	wallet.insert_adhoc (scendere::dev::genesis_key.prv);
	scendere::state_block_builder builder;
	auto send1 = builder.make_block ()
				 .account (scendere::dev::genesis_key.pub)
				 .previous (scendere::dev::genesis->hash ())
				 .representative (scendere::dev::genesis_key.pub)
				 .balance (scendere::dev::constants.genesis_amount - scendere::Gxrb_ratio)
				 .link (scendere::dev::genesis_key.pub)
				 .sign (scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub)
				 .work (*system.work.generate (scendere::dev::genesis->hash ()))
				 .build_shared ();
	auto send2 = builder.make_block ()
				 .account (scendere::dev::genesis_key.pub)
				 .previous (scendere::dev::genesis->hash ())
				 .representative (scendere::dev::genesis_key.pub)
				 .balance (scendere::dev::constants.genesis_amount - scendere::Gxrb_ratio - 1)
				 .link (scendere::dev::genesis_key.pub)
				 .sign (scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub)
				 .work (*system.work.generate (scendere::dev::genesis->hash ()))
				 .build_shared ();
	ASSERT_EQ (scendere::process_result::progress, node.ledger.process (node.store.tx_begin_write (), *send1).code);
	ASSERT_EQ (0, node.stats.count (scendere::stat::type::vote_generator, scendere::stat::detail::generator_broadcasts));
	node.active.generator.add (scendere::dev::genesis->hash (), send1->hash ());
	ASSERT_TIMELY (3s, node.stats.count (scendere::stat::type::vote_generator, scendere::stat::detail::generator_broadcasts) == 1);
	ASSERT_FALSE (node.ledger.rollback (node.store.tx_begin_write (), send1->hash ()));
	ASSERT_EQ (scendere::process_result::progress, node.ledger.process (node.store.tx_begin_write (), *send2).code);
	node.active.generator.add (scendere::dev::genesis->hash (), send2->hash ());
	ASSERT_TIMELY (3s, node.stats.count (scendere::stat::type::vote_generator, scendere::stat::detail::generator_spacing) == 1);
	ASSERT_EQ (1, node.stats.count (scendere::stat::type::vote_generator, scendere::stat::detail::generator_broadcasts));
	std::this_thread::sleep_for (config.network_params.voting.delay);
	node.active.generator.add (scendere::dev::genesis->hash (), send2->hash ());
	ASSERT_TIMELY (3s, node.stats.count (scendere::stat::type::vote_generator, scendere::stat::detail::generator_broadcasts) == 2);
}

TEST (vote_spacing, rapid)
{
	scendere::node_config config;
	config.frontiers_confirmation = scendere::frontiers_confirmation_mode::disabled;
	scendere::system system;
	scendere::node_flags node_flags;
	node_flags.disable_search_pending = true;
	auto & node = *system.add_node (config, node_flags);
	auto & wallet = *system.wallet (0);
	wallet.insert_adhoc (scendere::dev::genesis_key.prv);
	scendere::state_block_builder builder;
	auto send1 = builder.make_block ()
				 .account (scendere::dev::genesis_key.pub)
				 .previous (scendere::dev::genesis->hash ())
				 .representative (scendere::dev::genesis_key.pub)
				 .balance (scendere::dev::constants.genesis_amount - scendere::Gxrb_ratio)
				 .link (scendere::dev::genesis_key.pub)
				 .sign (scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub)
				 .work (*system.work.generate (scendere::dev::genesis->hash ()))
				 .build_shared ();
	auto send2 = builder.make_block ()
				 .account (scendere::dev::genesis_key.pub)
				 .previous (scendere::dev::genesis->hash ())
				 .representative (scendere::dev::genesis_key.pub)
				 .balance (scendere::dev::constants.genesis_amount - scendere::Gxrb_ratio - 1)
				 .link (scendere::dev::genesis_key.pub)
				 .sign (scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub)
				 .work (*system.work.generate (scendere::dev::genesis->hash ()))
				 .build_shared ();
	ASSERT_EQ (scendere::process_result::progress, node.ledger.process (node.store.tx_begin_write (), *send1).code);
	node.active.generator.add (scendere::dev::genesis->hash (), send1->hash ());
	ASSERT_TIMELY (3s, node.stats.count (scendere::stat::type::vote_generator, scendere::stat::detail::generator_broadcasts) == 1);
	ASSERT_FALSE (node.ledger.rollback (node.store.tx_begin_write (), send1->hash ()));
	ASSERT_EQ (scendere::process_result::progress, node.ledger.process (node.store.tx_begin_write (), *send2).code);
	node.active.generator.add (scendere::dev::genesis->hash (), send2->hash ());
	ASSERT_TIMELY (3s, node.stats.count (scendere::stat::type::vote_generator, scendere::stat::detail::generator_spacing) == 1);
	ASSERT_TIMELY (3s, 1 == node.stats.count (scendere::stat::type::vote_generator, scendere::stat::detail::generator_broadcasts));
	std::this_thread::sleep_for (config.network_params.voting.delay);
	node.active.generator.add (scendere::dev::genesis->hash (), send2->hash ());
	ASSERT_TIMELY (3s, node.stats.count (scendere::stat::type::vote_generator, scendere::stat::detail::generator_broadcasts) == 2);
}
