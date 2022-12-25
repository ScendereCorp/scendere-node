#pragma once

#include <scendere/boost/asio/ip/tcp.hpp>
#include <scendere/lib/logger_mt.hpp>
#include <scendere/lib/rpc_handler_interface.hpp>
#include <scendere/lib/rpcconfig.hpp>

namespace boost
{
namespace asio
{
	class io_context;
}
}

namespace scendere
{
class rpc_handler_interface;

class rpc
{
public:
	rpc (boost::asio::io_context & io_ctx_a, scendere::rpc_config config_a, scendere::rpc_handler_interface & rpc_handler_interface_a);
	virtual ~rpc ();
	void start ();
	virtual void accept ();
	void stop ();

	std::uint16_t listening_port ()
	{
		return acceptor.local_endpoint ().port ();
	}

	scendere::rpc_config config;
	boost::asio::ip::tcp::acceptor acceptor;
	scendere::logger_mt logger;
	boost::asio::io_context & io_ctx;
	scendere::rpc_handler_interface & rpc_handler_interface;
	bool stopped{ false };
};

/** Returns the correct RPC implementation based on TLS configuration */
std::unique_ptr<scendere::rpc> get_rpc (boost::asio::io_context & io_ctx_a, scendere::rpc_config const & config_a, scendere::rpc_handler_interface & rpc_handler_interface_a);
}
