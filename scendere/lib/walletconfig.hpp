#pragma once

#include <scendere/lib/errors.hpp>
#include <scendere/lib/numbers.hpp>

#include <string>

namespace scendere
{
class tomlconfig;

/** Configuration options for the Qt wallet */
class wallet_config final
{
public:
	wallet_config ();
	/** Update this instance by parsing the given wallet and account */
	scendere::error parse (std::string const & wallet_a, std::string const & account_a);
	scendere::error serialize_toml (scendere::tomlconfig & toml_a) const;
	scendere::error deserialize_toml (scendere::tomlconfig & toml_a);
	scendere::wallet_id wallet;
	scendere::account account{};
};
}
