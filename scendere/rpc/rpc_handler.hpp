#pragma once

#include <boost/property_tree/ptree.hpp>

#include <functional>
#include <string>

namespace scendere
{
class rpc_config;
class rpc_handler_interface;
class logger_mt;
class rpc_handler_request_params;

class rpc_handler : public std::enable_shared_from_this<scendere::rpc_handler>
{
public:
	rpc_handler (scendere::rpc_config const & rpc_config, std::string const & body_a, std::string const & request_id_a, std::function<void (std::string const &)> const & response_a, scendere::rpc_handler_interface & rpc_handler_interface_a, scendere::logger_mt & logger);
	void process_request (scendere::rpc_handler_request_params const & request_params);

private:
	std::string body;
	std::string request_id;
	boost::property_tree::ptree request;
	std::function<void (std::string const &)> response;
	scendere::rpc_config const & rpc_config;
	scendere::rpc_handler_interface & rpc_handler_interface;
	scendere::logger_mt & logger;
};
}
