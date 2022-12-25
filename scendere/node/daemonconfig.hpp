#pragma once

#include <scendere/lib/errors.hpp>
#include <scendere/node/node_pow_server_config.hpp>
#include <scendere/node/node_rpc_config.hpp>
#include <scendere/node/nodeconfig.hpp>
#include <scendere/node/openclconfig.hpp>

#include <vector>

namespace scendere
{
class tomlconfig;
class daemon_config
{
public:
	daemon_config () = default;
	daemon_config (boost::filesystem::path const & data_path, scendere::network_params & network_params);
	scendere::error deserialize_toml (scendere::tomlconfig &);
	scendere::error serialize_toml (scendere::tomlconfig &);
	bool rpc_enable{ false };
	scendere::node_rpc_config rpc;
	scendere::node_config node;
	bool opencl_enable{ false };
	scendere::opencl_config opencl;
	scendere::node_pow_server_config pow_server;
	boost::filesystem::path data_path;
};

scendere::error read_node_config_toml (boost::filesystem::path const &, scendere::daemon_config & config_a, std::vector<std::string> const & config_overrides = std::vector<std::string> ());
}
