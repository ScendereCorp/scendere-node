#include <scendere/node/common.hpp>
#include <scendere/node/testing.hpp>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iostream>

namespace scendere
{
void force_scendere_dev_network ();
}
namespace
{
std::shared_ptr<scendere::system> system0;
std::shared_ptr<scendere::node> node0;

class fuzz_visitor : public scendere::message_visitor
{
public:
	virtual void keepalive (scendere::keepalive const &) override
	{
	}
	virtual void publish (scendere::publish const &) override
	{
	}
	virtual void confirm_req (scendere::confirm_req const &) override
	{
	}
	virtual void confirm_ack (scendere::confirm_ack const &) override
	{
	}
	virtual void bulk_pull (scendere::bulk_pull const &) override
	{
	}
	virtual void bulk_pull_account (scendere::bulk_pull_account const &) override
	{
	}
	virtual void bulk_push (scendere::bulk_push const &) override
	{
	}
	virtual void frontier_req (scendere::frontier_req const &) override
	{
	}
	virtual void node_id_handshake (scendere::node_id_handshake const &) override
	{
	}
	virtual void telemetry_req (scendere::telemetry_req const &) override
	{
	}
	virtual void telemetry_ack (scendere::telemetry_ack const &) override
	{
	}
};
}

/** Fuzz live message parsing. This covers parsing and block/vote uniquing. */
void fuzz_message_parser (uint8_t const * Data, size_t Size)
{
	static bool initialized = false;
	if (!initialized)
	{
		scendere::force_scendere_dev_network ();
		initialized = true;
		system0 = std::make_shared<scendere::system> (1);
		node0 = system0->nodes[0];
	}

	fuzz_visitor visitor;
	scendere::message_parser parser (node0->network.publish_filter, node0->block_uniquer, node0->vote_uniquer, visitor, node0->work);
	parser.deserialize_buffer (Data, Size);
}

/** Fuzzer entry point */
extern "C" int LLVMFuzzerTestOneInput (uint8_t const * Data, size_t Size)
{
	fuzz_message_parser (Data, Size);
	return 0;
}
