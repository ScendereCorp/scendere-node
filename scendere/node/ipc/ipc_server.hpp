#pragma once

#include <scendere/ipc_flatbuffers_lib/generated/flatbuffers/scendereapi_generated.h>
#include <scendere/lib/errors.hpp>
#include <scendere/lib/ipc.hpp>
#include <scendere/node/ipc/ipc_access_config.hpp>
#include <scendere/node/ipc/ipc_broker.hpp>
#include <scendere/node/node_rpc_config.hpp>

#include <atomic>
#include <memory>
#include <mutex>

namespace flatbuffers
{
class Parser;
}
namespace scendere
{
class node;
class error;
namespace ipc
{
	class access;
	/** The IPC server accepts connections on one or more configured transports */
	class ipc_server final
	{
	public:
		ipc_server (scendere::node & node, scendere::node_rpc_config const & node_rpc_config);
		~ipc_server ();
		void stop ();

		std::optional<std::uint16_t> listening_tcp_port () const;

		scendere::node & node;
		scendere::node_rpc_config const & node_rpc_config;

		/** Unique counter/id shared across sessions */
		std::atomic<uint64_t> id_dispenser{ 1 };
		std::shared_ptr<scendere::ipc::broker> get_broker ();
		scendere::ipc::access & get_access ();
		scendere::error reload_access_config ();

	private:
		void setup_callbacks ();
		std::shared_ptr<scendere::ipc::broker> broker;
		scendere::ipc::access access;
		std::unique_ptr<dsock_file_remover> file_remover;
		std::vector<std::shared_ptr<scendere::ipc::transport>> transports;
	};
}
}
