#include <scendere/boost/asio/bind_executor.hpp>
#include <scendere/lib/rpc_handler_interface.hpp>
#include <scendere/lib/tlsconfig.hpp>
#include <scendere/rpc/rpc.hpp>
#include <scendere/rpc/rpc_connection.hpp>

#include <boost/format.hpp>

#include <iostream>

#ifdef SCENDERE_SECURE_RPC
#include <scendere/rpc/rpc_secure.hpp>
#endif

scendere::rpc::rpc (boost::asio::io_context & io_ctx_a, scendere::rpc_config config_a, scendere::rpc_handler_interface & rpc_handler_interface_a) :
	config (std::move (config_a)),
	acceptor (io_ctx_a),
	logger (std::chrono::milliseconds (0)),
	io_ctx (io_ctx_a),
	rpc_handler_interface (rpc_handler_interface_a)
{
	rpc_handler_interface.rpc_instance (*this);
}

scendere::rpc::~rpc ()
{
	if (!stopped)
	{
		stop ();
	}
}

void scendere::rpc::start ()
{
	auto endpoint (boost::asio::ip::tcp::endpoint (boost::asio::ip::make_address_v6 (config.address), config.port));
	bool const is_loopback = (endpoint.address ().is_loopback () || (endpoint.address ().to_v6 ().is_v4_mapped () && boost::asio::ip::make_address_v4 (boost::asio::ip::v4_mapped, endpoint.address ().to_v6 ()).is_loopback ()));
	if (!is_loopback && config.enable_control)
	{
		auto warning = boost::str (boost::format ("WARNING: control-level RPCs are enabled on non-local address %1%, potentially allowing wallet access outside local computer") % endpoint.address ().to_string ());
		std::cout << warning << std::endl;
		logger.always_log (warning);
	}
	acceptor.open (endpoint.protocol ());
	acceptor.set_option (boost::asio::ip::tcp::acceptor::reuse_address (true));

	boost::system::error_code ec;
	acceptor.bind (endpoint, ec);
	if (ec)
	{
		logger.always_log (boost::str (boost::format ("Error while binding for RPC on port %1%: %2%") % endpoint.port () % ec.message ()));
		throw std::runtime_error (ec.message ());
	}
	acceptor.listen ();
	accept ();
}

void scendere::rpc::accept ()
{
	auto connection (std::make_shared<scendere::rpc_connection> (config, io_ctx, logger, rpc_handler_interface));
	acceptor.async_accept (connection->socket, boost::asio::bind_executor (connection->strand, [this, connection] (boost::system::error_code const & ec) {
		if (ec != boost::asio::error::operation_aborted && acceptor.is_open ())
		{
			accept ();
		}
		if (!ec)
		{
			connection->parse_connection ();
		}
		else
		{
			logger.always_log (boost::str (boost::format ("Error accepting RPC connections: %1% (%2%)") % ec.message () % ec.value ()));
		}
	}));
}

void scendere::rpc::stop ()
{
	stopped = true;
	acceptor.close ();
}

std::unique_ptr<scendere::rpc> scendere::get_rpc (boost::asio::io_context & io_ctx_a, scendere::rpc_config const & config_a, scendere::rpc_handler_interface & rpc_handler_interface_a)
{
	std::unique_ptr<rpc> impl;

	if (config_a.tls_config && config_a.tls_config->enable_https)
	{
#ifdef SCENDERE_SECURE_RPC
		impl = std::make_unique<rpc_secure> (io_ctx_a, config_a, rpc_handler_interface_a);
#endif
	}
	else
	{
		impl = std::make_unique<rpc> (io_ctx_a, config_a, rpc_handler_interface_a);
	}

	return impl;
}
