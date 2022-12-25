#include <scendere/node/common.hpp>
#include <scendere/node/network.hpp>
#include <scendere/secure/buffer.hpp>

#include <gtest/gtest.h>

#include <boost/algorithm/string.hpp>
#include <boost/variant/get.hpp>

TEST (message, keepalive_serialization)
{
	scendere::keepalive request1{ scendere::dev::network_params.network };
	std::vector<uint8_t> bytes;
	{
		scendere::vectorstream stream (bytes);
		request1.serialize (stream);
	}
	auto error (false);
	scendere::bufferstream stream (bytes.data (), bytes.size ());
	scendere::message_header header (error, stream);
	ASSERT_FALSE (error);
	scendere::keepalive request2 (error, stream, header);
	ASSERT_FALSE (error);
	ASSERT_EQ (request1, request2);
}

TEST (message, keepalive_deserialize)
{
	scendere::keepalive message1{ scendere::dev::network_params.network };
	message1.peers[0] = scendere::endpoint (boost::asio::ip::address_v6::loopback (), 10000);
	std::vector<uint8_t> bytes;
	{
		scendere::vectorstream stream (bytes);
		message1.serialize (stream);
	}
	scendere::bufferstream stream (bytes.data (), bytes.size ());
	auto error (false);
	scendere::message_header header (error, stream);
	ASSERT_FALSE (error);
	ASSERT_EQ (scendere::message_type::keepalive, header.type);
	scendere::keepalive message2 (error, stream, header);
	ASSERT_FALSE (error);
	ASSERT_EQ (message1.peers, message2.peers);
}

TEST (message, publish_serialization)
{
	scendere::publish publish{ scendere::dev::network_params.network, std::make_shared<scendere::send_block> (0, 1, 2, scendere::keypair ().prv, 4, 5) };
	ASSERT_EQ (scendere::block_type::send, publish.header.block_type ());
	std::vector<uint8_t> bytes;
	{
		scendere::vectorstream stream (bytes);
		publish.header.serialize (stream);
	}
	ASSERT_EQ (8, bytes.size ());
	ASSERT_EQ (0x52, bytes[0]);
	ASSERT_EQ (0x41, bytes[1]);
	ASSERT_EQ (scendere::dev::network_params.network.protocol_version, bytes[2]);
	ASSERT_EQ (scendere::dev::network_params.network.protocol_version, bytes[3]);
	ASSERT_EQ (scendere::dev::network_params.network.protocol_version_min, bytes[4]);
	ASSERT_EQ (static_cast<uint8_t> (scendere::message_type::publish), bytes[5]);
	ASSERT_EQ (0x00, bytes[6]); // extensions
	ASSERT_EQ (static_cast<uint8_t> (scendere::block_type::send), bytes[7]);
	scendere::bufferstream stream (bytes.data (), bytes.size ());
	auto error (false);
	scendere::message_header header (error, stream);
	ASSERT_FALSE (error);
	ASSERT_EQ (scendere::dev::network_params.network.protocol_version_min, header.version_min);
	ASSERT_EQ (scendere::dev::network_params.network.protocol_version, header.version_using);
	ASSERT_EQ (scendere::dev::network_params.network.protocol_version, header.version_max);
	ASSERT_EQ (scendere::message_type::publish, header.type);
}

TEST (message, confirm_ack_serialization)
{
	scendere::keypair key1;
	auto vote (std::make_shared<scendere::vote> (key1.pub, key1.prv, 0, 0, std::make_shared<scendere::send_block> (0, 1, 2, key1.prv, 4, 5)));
	scendere::confirm_ack con1{ scendere::dev::network_params.network, vote };
	std::vector<uint8_t> bytes;
	{
		scendere::vectorstream stream1 (bytes);
		con1.serialize (stream1);
	}
	scendere::bufferstream stream2 (bytes.data (), bytes.size ());
	bool error (false);
	scendere::message_header header (error, stream2);
	scendere::confirm_ack con2 (error, stream2, header);
	ASSERT_FALSE (error);
	ASSERT_EQ (con1, con2);
	ASSERT_EQ (header.block_type (), scendere::block_type::send);
}

TEST (message, confirm_ack_hash_serialization)
{
	std::vector<scendere::block_hash> hashes;
	for (auto i (hashes.size ()); i < scendere::network::confirm_ack_hashes_max; i++)
	{
		scendere::keypair key1;
		scendere::block_hash previous;
		scendere::random_pool::generate_block (previous.bytes.data (), previous.bytes.size ());
		scendere::state_block block (key1.pub, previous, key1.pub, 2, 4, key1.prv, key1.pub, 5);
		hashes.push_back (block.hash ());
	}
	scendere::keypair representative1;
	auto vote (std::make_shared<scendere::vote> (representative1.pub, representative1.prv, 0, 0, hashes));
	scendere::confirm_ack con1{ scendere::dev::network_params.network, vote };
	std::vector<uint8_t> bytes;
	{
		scendere::vectorstream stream1 (bytes);
		con1.serialize (stream1);
	}
	scendere::bufferstream stream2 (bytes.data (), bytes.size ());
	bool error (false);
	scendere::message_header header (error, stream2);
	scendere::confirm_ack con2 (error, stream2, header);
	ASSERT_FALSE (error);
	ASSERT_EQ (con1, con2);
	std::vector<scendere::block_hash> vote_blocks;
	for (auto block : con2.vote->blocks)
	{
		vote_blocks.push_back (boost::get<scendere::block_hash> (block));
	}
	ASSERT_EQ (hashes, vote_blocks);
	// Check overflow with max hashes
	ASSERT_EQ (header.count_get (), hashes.size ());
	ASSERT_EQ (header.block_type (), scendere::block_type::not_a_block);
}

TEST (message, confirm_req_serialization)
{
	scendere::keypair key1;
	scendere::keypair key2;
	auto block (std::make_shared<scendere::send_block> (0, key2.pub, 200, scendere::keypair ().prv, 2, 3));
	scendere::confirm_req req{ scendere::dev::network_params.network, block };
	std::vector<uint8_t> bytes;
	{
		scendere::vectorstream stream (bytes);
		req.serialize (stream);
	}
	auto error (false);
	scendere::bufferstream stream2 (bytes.data (), bytes.size ());
	scendere::message_header header (error, stream2);
	scendere::confirm_req req2 (error, stream2, header);
	ASSERT_FALSE (error);
	ASSERT_EQ (req, req2);
	ASSERT_EQ (*req.block, *req2.block);
}

TEST (message, confirm_req_hash_serialization)
{
	scendere::keypair key1;
	scendere::keypair key2;
	scendere::send_block block (1, key2.pub, 200, scendere::keypair ().prv, 2, 3);
	scendere::confirm_req req{ scendere::dev::network_params.network, block.hash (), block.root () };
	std::vector<uint8_t> bytes;
	{
		scendere::vectorstream stream (bytes);
		req.serialize (stream);
	}
	auto error (false);
	scendere::bufferstream stream2 (bytes.data (), bytes.size ());
	scendere::message_header header (error, stream2);
	scendere::confirm_req req2 (error, stream2, header);
	ASSERT_FALSE (error);
	ASSERT_EQ (req, req2);
	ASSERT_EQ (req.roots_hashes, req2.roots_hashes);
	ASSERT_EQ (header.block_type (), scendere::block_type::not_a_block);
	ASSERT_EQ (header.count_get (), req.roots_hashes.size ());
}

TEST (message, confirm_req_hash_batch_serialization)
{
	scendere::keypair key;
	scendere::keypair representative;
	std::vector<std::pair<scendere::block_hash, scendere::root>> roots_hashes;
	scendere::state_block open (key.pub, 0, representative.pub, 2, 4, key.prv, key.pub, 5);
	roots_hashes.push_back (std::make_pair (open.hash (), open.root ()));
	for (auto i (roots_hashes.size ()); i < 7; i++)
	{
		scendere::keypair key1;
		scendere::block_hash previous;
		scendere::random_pool::generate_block (previous.bytes.data (), previous.bytes.size ());
		scendere::state_block block (key1.pub, previous, representative.pub, 2, 4, key1.prv, key1.pub, 5);
		roots_hashes.push_back (std::make_pair (block.hash (), block.root ()));
	}
	roots_hashes.push_back (std::make_pair (open.hash (), open.root ()));
	scendere::confirm_req req{ scendere::dev::network_params.network, roots_hashes };
	std::vector<uint8_t> bytes;
	{
		scendere::vectorstream stream (bytes);
		req.serialize (stream);
	}
	auto error (false);
	scendere::bufferstream stream2 (bytes.data (), bytes.size ());
	scendere::message_header header (error, stream2);
	scendere::confirm_req req2 (error, stream2, header);
	ASSERT_FALSE (error);
	ASSERT_EQ (req, req2);
	ASSERT_EQ (req.roots_hashes, req2.roots_hashes);
	ASSERT_EQ (req.roots_hashes, roots_hashes);
	ASSERT_EQ (req2.roots_hashes, roots_hashes);
	ASSERT_EQ (header.block_type (), scendere::block_type::not_a_block);
	ASSERT_EQ (header.count_get (), req.roots_hashes.size ());
}

// this unit test checks that conversion of message_header to string works as expected
TEST (message, message_header_to_string)
{
	// calculate expected string
	int maxver = scendere::dev::network_params.network.protocol_version;
	int minver = scendere::dev::network_params.network.protocol_version_min;
	std::stringstream ss;
	ss << "NetID: 5241(dev), VerMaxUsingMin: " << maxver << "/" << maxver << "/" << minver << ", MsgType: 2(keepalive), Extensions: 0000";
	auto expected_str = ss.str ();

	// check expected vs real
	scendere::keepalive keepalive_msg{ scendere::dev::network_params.network };
	std::string header_string = keepalive_msg.header.to_string ();
	ASSERT_EQ (expected_str, header_string);
}
