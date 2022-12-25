#pragma once

#include <scendere/lib/errors.hpp>

#include <chrono>

namespace scendere
{
class jsonconfig;
class tomlconfig;
class txn_tracking_config final
{
public:
	/** If true, enable tracking for transaction read/writes held open longer than the min time variables */
	bool enable{ false };
	std::chrono::milliseconds min_read_txn_time{ 5000 };
	std::chrono::milliseconds min_write_txn_time{ 500 };
	bool ignore_writes_below_block_processor_max_time{ true };
};

/** Configuration options for diagnostics information */
class diagnostics_config final
{
public:
	scendere::error serialize_toml (scendere::tomlconfig &) const;
	scendere::error deserialize_toml (scendere::tomlconfig &);

	txn_tracking_config txn_tracking;
};
}
