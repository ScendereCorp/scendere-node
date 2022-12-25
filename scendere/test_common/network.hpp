#pragma once

#include <scendere/node/common.hpp>

namespace scendere
{
class node;
class system;

namespace transport
{
	class channel;
	class channel_tcp;
}

/** Waits until a TCP connection is established and returns the TCP channel on success*/
std::shared_ptr<scendere::transport::channel_tcp> establish_tcp (scendere::system &, scendere::node &, scendere::endpoint const &);
}
