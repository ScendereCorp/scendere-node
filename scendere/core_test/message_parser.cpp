#include <scendere/test_common/system.hpp>
#include <scendere/test_common/testutil.hpp>

#include <gtest/gtest.h>

namespace
{
class dev_visitor : public scendere::message_visitor
{
public:
	void keepalive (scendere::keepalive const &) override
	{
		++keepalive_count;
	}
	void publish (scendere::publish const &) override
	{
		++publish_count;
	}
	void confirm_req (scendere::confirm_req const &) override
	{
		++confirm_req_count;
	}
	void confirm_ack (scendere::confirm_ack const &) override
	{
		++confirm_ack_count;
	}
	void bulk_pull (scendere::bulk_pull const &) override
	{
		ASSERT_FALSE (true);
	}
	void bulk_pull_account (scendere::bulk_pull_account const &) override
	{
		ASSERT_FALSE (true);
	}
	void bulk_push (scendere::bulk_push const &) override
	{
		ASSERT_FALSE (true);
	}
	void frontier_req (scendere::frontier_req const &) override
	{
		ASSERT_FALSE (true);
	}
	void node_id_handshake (scendere::node_id_handshake const &) override
	{
		ASSERT_FALSE (true);
	}
	void telemetry_req (scendere::telemetry_req const &) override
	{
		ASSERT_FALSE (true);
	}
	void telemetry_ack (scendere::telemetry_ack const &) override
	{
		ASSERT_FALSE (true);
	}

	uint64_t keepalive_count{ 0 };
	uint64_t publish_count{ 0 };
	uint64_t confirm_req_count{ 0 };
	uint64_t confirm_ack_count{ 0 };
};
}

TEST (message_parser, exact_confirm_ack_size)
{
	scendere::system system (1);
	dev_visitor visitor;
	scendere::network_filter filter (1);
	scendere::block_uniquer block_uniquer;
	scendere::vote_uniquer vote_uniquer (block_uniquer);
	scendere::message_parser parser (filter, block_uniquer, vote_uniquer, visitor, system.work, scendere::dev::network_params.network);
	auto block (std::make_shared<scendere::send_block> (1, 1, 2, scendere::keypair ().prv, 4, *system.work.generate (scendere::root (1))));
	auto vote (std::make_shared<scendere::vote> (0, scendere::keypair ().prv, 0, 0, std::move (block)));
	scendere::confirm_ack message{ scendere::dev::network_params.network, vote };
	std::vector<uint8_t> bytes;
	{
		scendere::vectorstream stream (bytes);
		message.serialize (stream);
	}
	ASSERT_EQ (0, visitor.confirm_ack_count);
	ASSERT_EQ (parser.status, scendere::message_parser::parse_status::success);
	auto error (false);
	scendere::bufferstream stream1 (bytes.data (), bytes.size ());
	scendere::message_header header1 (error, stream1);
	ASSERT_FALSE (error);
	parser.deserialize_confirm_ack (stream1, header1);
	ASSERT_EQ (1, visitor.confirm_ack_count);
	ASSERT_EQ (parser.status, scendere::message_parser::parse_status::success);
	bytes.push_back (0);
	scendere::bufferstream stream2 (bytes.data (), bytes.size ());
	scendere::message_header header2 (error, stream2);
	ASSERT_FALSE (error);
	parser.deserialize_confirm_ack (stream2, header2);
	ASSERT_EQ (1, visitor.confirm_ack_count);
	ASSERT_NE (parser.status, scendere::message_parser::parse_status::success);
}

TEST (message_parser, exact_confirm_req_size)
{
	scendere::system system (1);
	dev_visitor visitor;
	scendere::network_filter filter (1);
	scendere::block_uniquer block_uniquer;
	scendere::vote_uniquer vote_uniquer (block_uniquer);
	scendere::message_parser parser (filter, block_uniquer, vote_uniquer, visitor, system.work, scendere::dev::network_params.network);
	auto block (std::make_shared<scendere::send_block> (1, 1, 2, scendere::keypair ().prv, 4, *system.work.generate (scendere::root (1))));
	scendere::confirm_req message{ scendere::dev::network_params.network, block };
	std::vector<uint8_t> bytes;
	{
		scendere::vectorstream stream (bytes);
		message.serialize (stream);
	}
	ASSERT_EQ (0, visitor.confirm_req_count);
	ASSERT_EQ (parser.status, scendere::message_parser::parse_status::success);
	auto error (false);
	scendere::bufferstream stream1 (bytes.data (), bytes.size ());
	scendere::message_header header1 (error, stream1);
	ASSERT_FALSE (error);
	parser.deserialize_confirm_req (stream1, header1);
	ASSERT_EQ (1, visitor.confirm_req_count);
	ASSERT_EQ (parser.status, scendere::message_parser::parse_status::success);
	bytes.push_back (0);
	scendere::bufferstream stream2 (bytes.data (), bytes.size ());
	scendere::message_header header2 (error, stream2);
	ASSERT_FALSE (error);
	parser.deserialize_confirm_req (stream2, header2);
	ASSERT_EQ (1, visitor.confirm_req_count);
	ASSERT_NE (parser.status, scendere::message_parser::parse_status::success);
}

TEST (message_parser, exact_confirm_req_hash_size)
{
	scendere::system system (1);
	dev_visitor visitor;
	scendere::network_filter filter (1);
	scendere::block_uniquer block_uniquer;
	scendere::vote_uniquer vote_uniquer (block_uniquer);
	scendere::message_parser parser (filter, block_uniquer, vote_uniquer, visitor, system.work, scendere::dev::network_params.network);
	scendere::send_block block (1, 1, 2, scendere::keypair ().prv, 4, *system.work.generate (scendere::root (1)));
	scendere::confirm_req message{ scendere::dev::network_params.network, block.hash (), block.root () };
	std::vector<uint8_t> bytes;
	{
		scendere::vectorstream stream (bytes);
		message.serialize (stream);
	}
	ASSERT_EQ (0, visitor.confirm_req_count);
	ASSERT_EQ (parser.status, scendere::message_parser::parse_status::success);
	auto error (false);
	scendere::bufferstream stream1 (bytes.data (), bytes.size ());
	scendere::message_header header1 (error, stream1);
	ASSERT_FALSE (error);
	parser.deserialize_confirm_req (stream1, header1);
	ASSERT_EQ (1, visitor.confirm_req_count);
	ASSERT_EQ (parser.status, scendere::message_parser::parse_status::success);
	bytes.push_back (0);
	scendere::bufferstream stream2 (bytes.data (), bytes.size ());
	scendere::message_header header2 (error, stream2);
	ASSERT_FALSE (error);
	parser.deserialize_confirm_req (stream2, header2);
	ASSERT_EQ (1, visitor.confirm_req_count);
	ASSERT_NE (parser.status, scendere::message_parser::parse_status::success);
}

TEST (message_parser, exact_publish_size)
{
	scendere::system system (1);
	dev_visitor visitor;
	scendere::network_filter filter (1);
	scendere::block_uniquer block_uniquer;
	scendere::vote_uniquer vote_uniquer (block_uniquer);
	scendere::message_parser parser (filter, block_uniquer, vote_uniquer, visitor, system.work, scendere::dev::network_params.network);
	auto block (std::make_shared<scendere::send_block> (1, 1, 2, scendere::keypair ().prv, 4, *system.work.generate (scendere::root (1))));
	scendere::publish message{ scendere::dev::network_params.network, block };
	std::vector<uint8_t> bytes;
	{
		scendere::vectorstream stream (bytes);
		message.serialize (stream);
	}
	ASSERT_EQ (0, visitor.publish_count);
	ASSERT_EQ (parser.status, scendere::message_parser::parse_status::success);
	auto error (false);
	scendere::bufferstream stream1 (bytes.data (), bytes.size ());
	scendere::message_header header1 (error, stream1);
	ASSERT_FALSE (error);
	parser.deserialize_publish (stream1, header1);
	ASSERT_EQ (1, visitor.publish_count);
	ASSERT_EQ (parser.status, scendere::message_parser::parse_status::success);
	bytes.push_back (0);
	scendere::bufferstream stream2 (bytes.data (), bytes.size ());
	scendere::message_header header2 (error, stream2);
	ASSERT_FALSE (error);
	parser.deserialize_publish (stream2, header2);
	ASSERT_EQ (1, visitor.publish_count);
	ASSERT_NE (parser.status, scendere::message_parser::parse_status::success);
}

TEST (message_parser, exact_keepalive_size)
{
	scendere::system system (1);
	dev_visitor visitor;
	scendere::network_filter filter (1);
	scendere::block_uniquer block_uniquer;
	scendere::vote_uniquer vote_uniquer (block_uniquer);
	scendere::message_parser parser (filter, block_uniquer, vote_uniquer, visitor, system.work, scendere::dev::network_params.network);
	scendere::keepalive message{ scendere::dev::network_params.network };
	std::vector<uint8_t> bytes;
	{
		scendere::vectorstream stream (bytes);
		message.serialize (stream);
	}
	ASSERT_EQ (0, visitor.keepalive_count);
	ASSERT_EQ (parser.status, scendere::message_parser::parse_status::success);
	auto error (false);
	scendere::bufferstream stream1 (bytes.data (), bytes.size ());
	scendere::message_header header1 (error, stream1);
	ASSERT_FALSE (error);
	parser.deserialize_keepalive (stream1, header1);
	ASSERT_EQ (1, visitor.keepalive_count);
	ASSERT_EQ (parser.status, scendere::message_parser::parse_status::success);
	bytes.push_back (0);
	scendere::bufferstream stream2 (bytes.data (), bytes.size ());
	scendere::message_header header2 (error, stream2);
	ASSERT_FALSE (error);
	parser.deserialize_keepalive (stream2, header2);
	ASSERT_EQ (1, visitor.keepalive_count);
	ASSERT_NE (parser.status, scendere::message_parser::parse_status::success);
}
