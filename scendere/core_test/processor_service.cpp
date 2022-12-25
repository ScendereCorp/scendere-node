#include <scendere/lib/stats.hpp>
#include <scendere/lib/work.hpp>
#include <scendere/secure/ledger.hpp>
#include <scendere/secure/store.hpp>
#include <scendere/secure/utility.hpp>
#include <scendere/test_common/testutil.hpp>

#include <gtest/gtest.h>

TEST (processor_service, bad_send_signature)
{
	scendere::logger_mt logger;
	auto store = scendere::make_store (logger, scendere::unique_path (), scendere::dev::constants);
	ASSERT_FALSE (store->init_error ());
	scendere::stat stats;
	scendere::ledger ledger (*store, stats, scendere::dev::constants);
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, ledger.cache);
	scendere::work_pool pool{ scendere::dev::network_params.network, std::numeric_limits<unsigned>::max () };
	scendere::account_info info1;
	ASSERT_FALSE (store->account.get (transaction, scendere::dev::genesis_key.pub, info1));
	scendere::keypair key2;
	scendere::send_block send (info1.head, scendere::dev::genesis_key.pub, 50, scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub, *pool.generate (info1.head));
	send.signature.bytes[32] ^= 0x1;
	ASSERT_EQ (scendere::process_result::bad_signature, ledger.process (transaction, send).code);
}

TEST (processor_service, bad_receive_signature)
{
	scendere::logger_mt logger;
	auto store = scendere::make_store (logger, scendere::unique_path (), scendere::dev::constants);
	ASSERT_FALSE (store->init_error ());
	scendere::stat stats;
	scendere::ledger ledger (*store, stats, scendere::dev::constants);
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, ledger.cache);
	scendere::work_pool pool{ scendere::dev::network_params.network, std::numeric_limits<unsigned>::max () };
	scendere::account_info info1;
	ASSERT_FALSE (store->account.get (transaction, scendere::dev::genesis_key.pub, info1));
	scendere::send_block send (info1.head, scendere::dev::genesis_key.pub, 50, scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub, *pool.generate (info1.head));
	scendere::block_hash hash1 (send.hash ());
	ASSERT_EQ (scendere::process_result::progress, ledger.process (transaction, send).code);
	scendere::account_info info2;
	ASSERT_FALSE (store->account.get (transaction, scendere::dev::genesis_key.pub, info2));
	scendere::receive_block receive (hash1, hash1, scendere::dev::genesis_key.prv, scendere::dev::genesis_key.pub, *pool.generate (hash1));
	receive.signature.bytes[32] ^= 0x1;
	ASSERT_EQ (scendere::process_result::bad_signature, ledger.process (transaction, receive).code);
}
