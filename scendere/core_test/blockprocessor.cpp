#include <scendere/lib/blockbuilders.hpp>
#include <scendere/node/node.hpp>
#include <scendere/node/nodeconfig.hpp>
#include <scendere/secure/common.hpp>
#include <scendere/secure/ledger.hpp>
#include <scendere/test_common/system.hpp>
#include <scendere/test_common/testutil.hpp>

#include <gtest/gtest.h>

using namespace std::chrono_literals;

TEST (block_processor, broadcast_block_on_arrival)
{
	scendere::system system;
	scendere::node_config config1{ scendere::get_available_port (), system.logging };
	// Deactivates elections on both nodes.
	config1.active_elections_size = 0;
	scendere::node_config config2{ scendere::get_available_port (), system.logging };
	config2.active_elections_size = 0;
	scendere::node_flags flags;
	// Disables bootstrap listener to make sure the block won't be shared by this channel.
	flags.disable_bootstrap_listener = true;
	auto node1 = system.add_node (config1, flags);
	auto node2 = system.add_node (config2, flags);
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
	// Adds a block to the first node. process_active() -> (calls) block_processor.add() -> add() ->
	// awakes process_block() -> process_batch() -> process_one() -> process_live()
	node1->process_active (send1);
	// Checks whether the block was broadcast.
	ASSERT_TIMELY (5s, node2->ledger.block_or_pruned_exists (send1->hash ()));
}