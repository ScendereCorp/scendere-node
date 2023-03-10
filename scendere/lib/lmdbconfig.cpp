#include <scendere/lib/lmdbconfig.hpp>
#include <scendere/lib/tomlconfig.hpp>
#include <scendere/secure/common.hpp>

#include <iostream>

scendere::error scendere::lmdb_config::serialize_toml (scendere::tomlconfig & toml) const
{
	std::string sync_string;
	switch (sync)
	{
		case scendere::lmdb_config::sync_strategy::always:
			sync_string = "always";
			break;
		case scendere::lmdb_config::sync_strategy::nosync_safe:
			sync_string = "nosync_safe";
			break;
		case scendere::lmdb_config::sync_strategy::nosync_unsafe:
			sync_string = "nosync_unsafe";
			break;
		case scendere::lmdb_config::sync_strategy::nosync_unsafe_large_memory:
			sync_string = "nosync_unsafe_large_memory";
			break;
	}

	toml.put ("sync", sync_string, "Sync strategy for flushing commits to the ledger database. This does not affect the wallet database.\ntype:string,{always, nosync_safe, nosync_unsafe, nosync_unsafe_large_memory}");
	toml.put ("max_databases", max_databases, "Maximum open lmdb databases. Increase default if more than 100 wallets is required.\nNote: external management is recommended when a large amounts of wallets are required (see https://docs.scendere.org/integration-guides/key-management/).\ntype:uin32");
	toml.put ("map_size", map_size, "Maximum ledger database map size in bytes.\ntype:uint64");
	return toml.get_error ();
}

scendere::error scendere::lmdb_config::deserialize_toml (scendere::tomlconfig & toml)
{
	auto default_max_databases = max_databases;
	toml.get_optional<uint32_t> ("max_databases", max_databases);
	toml.get_optional<size_t> ("map_size", map_size);

	if (!toml.get_error ())
	{
		std::string sync_string = "always";
		toml.get_optional<std::string> ("sync", sync_string);
		if (sync_string == "always")
		{
			sync = scendere::lmdb_config::sync_strategy::always;
		}
		else if (sync_string == "nosync_safe")
		{
			sync = scendere::lmdb_config::sync_strategy::nosync_safe;
		}
		else if (sync_string == "nosync_unsafe")
		{
			sync = scendere::lmdb_config::sync_strategy::nosync_unsafe;
		}
		else if (sync_string == "nosync_unsafe_large_memory")
		{
			sync = scendere::lmdb_config::sync_strategy::nosync_unsafe_large_memory;
		}
		else
		{
			toml.get_error ().set (sync_string + " is not a valid sync option");
		}
	}

	return toml.get_error ();
}
