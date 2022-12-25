#include <scendere/node/node.hpp>
#include <scendere/test_common/network.hpp>
#include <scendere/test_common/system.hpp>
#include <scendere/test_common/testutil.hpp>

#include <gtest/gtest.h>

#include <chrono>
#include <future>

using namespace std::chrono_literals;

std::shared_ptr<scendere::transport::channel_tcp> scendere::establish_tcp (scendere::system & system, scendere::node & node, scendere::endpoint const & endpoint)
{
	debug_assert (node.network.endpoint () != endpoint && "Establishing TCP to self is not allowed");

	std::shared_ptr<scendere::transport::channel_tcp> result;
	debug_assert (!node.flags.disable_tcp_realtime);
	node.network.tcp_channels.start_tcp (endpoint);
	auto error = system.poll_until_true (2s, [&result, &node, &endpoint] {
		result = node.network.tcp_channels.find_channel (scendere::transport::map_endpoint_to_tcp (endpoint));
		return result != nullptr;
	});
	return result;
}
