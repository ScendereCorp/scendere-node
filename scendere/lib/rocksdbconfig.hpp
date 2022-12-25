#pragma once

#include <scendere/lib/errors.hpp>

#include <thread>

namespace scendere
{
class tomlconfig;

/** Configuration options for RocksDB */
class rocksdb_config final
{
public:
	rocksdb_config () :
		enable{ using_rocksdb_in_tests () }
	{
	}
	scendere::error serialize_toml (scendere::tomlconfig & toml_a) const;
	scendere::error deserialize_toml (scendere::tomlconfig & toml_a);

	/** To use RocksDB in tests make sure the environment variable TEST_USE_ROCKSDB=1 is set */
	static bool using_rocksdb_in_tests ();

	bool enable{ false };
	uint8_t memory_multiplier{ 2 };
	unsigned io_threads{ std::thread::hardware_concurrency () };
};
}
