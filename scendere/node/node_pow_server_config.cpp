#include <scendere/lib/tomlconfig.hpp>
#include <scendere/node/node_pow_server_config.hpp>

scendere::error scendere::node_pow_server_config::serialize_toml (scendere::tomlconfig & toml) const
{
	toml.put ("enable", enable, "Value is currently not in use. Enable or disable starting Scendere PoW Server as a child process.\ntype:bool");
	toml.put ("scendere_pow_server_path", pow_server_path, "Value is currently not in use. Path to the scendere_pow_server executable.\ntype:string,path");
	return toml.get_error ();
}

scendere::error scendere::node_pow_server_config::deserialize_toml (scendere::tomlconfig & toml)
{
	toml.get_optional<bool> ("enable", enable);
	toml.get_optional<std::string> ("scendere_pow_server_path", pow_server_path);

	return toml.get_error ();
}
