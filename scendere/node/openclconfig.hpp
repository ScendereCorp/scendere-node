#pragma once

#include <scendere/lib/errors.hpp>

namespace scendere
{
class tomlconfig;
class opencl_config
{
public:
	opencl_config () = default;
	opencl_config (unsigned, unsigned, unsigned);
	scendere::error serialize_toml (scendere::tomlconfig &) const;
	scendere::error deserialize_toml (scendere::tomlconfig &);
	unsigned platform{ 0 };
	unsigned device{ 0 };
	unsigned threads{ 1024 * 1024 };
};
}
