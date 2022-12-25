#include <scendere/lib/blockbuilders.hpp>
#include <scendere/lib/logger_mt.hpp>
#include <scendere/node/unchecked_map.hpp>
#include <scendere/secure/store.hpp>
#include <scendere/secure/utility.hpp>
#include <scendere/test_common/system.hpp>
#include <scendere/test_common/testutil.hpp>

#include <gtest/gtest.h>

#include <memory>

using namespace std::chrono_literals;

namespace
{
class context
{
public:
	context () :
		store{ scendere::make_store (logger, scendere::unique_path (), scendere::dev::constants) },
		unchecked{ *store, false }
	{
	}
	scendere::logger_mt logger;
	std::unique_ptr<scendere::store> store;
	scendere::unchecked_map unchecked;
};
std::shared_ptr<scendere::block> block ()
{
	scendere::block_builder builder;
	return builder.state ()
	.account (scendere::dev::genesis_key.pub)
	.previous (scendere::dev::genesis->hash ())
	.representative (scendere::dev::genesis_key.pub)
	.balance (scendere::dev::constants.genesis_amount - 1)
	.link (scendere::dev::genesis_key.pub)
	.sign (scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub)
	.work (0)
	.build_shared ();
}
}

TEST (unchecked_map, construction)
{
	context context;
}

TEST (unchecked_map, put_one)
{
	context context;
	scendere::unchecked_info info{ block (), scendere::dev::genesis_key.pub };
	context.unchecked.put (info.block->previous (), info);
}

TEST (block_store, one_bootstrap)
{
	scendere::system system{};
	scendere::logger_mt logger{};
	auto store = scendere::make_store (logger, scendere::unique_path (), scendere::dev::constants);
	scendere::unchecked_map unchecked{ *store, false };
	ASSERT_TRUE (!store->init_error ());
	auto block1 = std::make_shared<scendere::send_block> (0, 1, 2, scendere::keypair ().prv, 4, 5);
	unchecked.put (block1->hash (), scendere::unchecked_info{ block1 });
	auto check_block_is_listed = [&] (scendere::transaction const & transaction_a, scendere::block_hash const & block_hash_a) {
		return unchecked.get (transaction_a, block_hash_a).size () > 0;
	};
	// Waits for the block1 to get saved in the database
	ASSERT_TIMELY (10s, check_block_is_listed (store->tx_begin_read (), block1->hash ()));
	auto transaction = store->tx_begin_read ();
	auto [begin, end] = unchecked.full_range (transaction);
	ASSERT_NE (end, begin);
	auto hash1 = begin->first.key ();
	ASSERT_EQ (block1->hash (), hash1);
	auto blocks = unchecked.get (transaction, hash1);
	ASSERT_EQ (1, blocks.size ());
	auto block2 = blocks[0].block;
	ASSERT_EQ (*block1, *block2);
	++begin;
	ASSERT_EQ (end, begin);
}
