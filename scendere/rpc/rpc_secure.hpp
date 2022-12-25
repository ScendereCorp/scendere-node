#pragma once
#include <scendere/rpc/rpc.hpp>

namespace boost
{
namespace asio
{
	class io_context;
}
}

namespace scendere
{
/**
 * Specialization of scendere::rpc with TLS support
 */
class rpc_secure : public rpc
{
public:
	rpc_secure (boost::asio::io_context & context_a, scendere::rpc_config const & config_a, scendere::rpc_handler_interface & rpc_handler_interface_a);

	/** Starts accepting connections */
	void accept () override;
};
}
