#pragma once

#include <scendere/lib/config.hpp>
#include <scendere/lib/errors.hpp>

#include <memory>

namespace scendere
{
class tomlconfig;
class tls_config;
namespace websocket
{
	/** websocket configuration */
	class config final
	{
	public:
		config (scendere::network_constants & network_constants);
		scendere::error deserialize_toml (scendere::tomlconfig & toml_a);
		scendere::error serialize_toml (scendere::tomlconfig & toml) const;
		scendere::network_constants & network_constants;
		bool enabled{ false };
		uint16_t port;
		std::string address;
		/** Optional TLS config */
		std::shared_ptr<scendere::tls_config> tls_config;
	};
}
}
