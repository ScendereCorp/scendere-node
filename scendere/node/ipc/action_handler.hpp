#pragma once

#include <scendere/ipc_flatbuffers_lib/flatbuffer_producer.hpp>
#include <scendere/ipc_flatbuffers_lib/generated/flatbuffers/scendereapi_generated.h>
#include <scendere/node/ipc/ipc_access_config.hpp>

#include <boost/optional.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <type_traits>
#include <unordered_map>

#include <flatbuffers/flatbuffers.h>
#include <flatbuffers/idl.h>
#include <flatbuffers/minireflect.h>
#include <flatbuffers/registry.h>
#include <flatbuffers/util.h>

namespace scendere
{
class error;
class node;
namespace ipc
{
	class ipc_server;
	class subscriber;

	/**
	 * Implements handlers for the various public IPC messages. When an action handler is completed,
	 * the flatbuffer contains the serialized response object.
	 * @note This is a light-weight class, and an instance can be created for every request.
	 */
	class action_handler final : public flatbuffer_producer, public std::enable_shared_from_this<action_handler>
	{
	public:
		action_handler (scendere::node & node, scendere::ipc::ipc_server & server, std::weak_ptr<scendere::ipc::subscriber> const & subscriber, std::shared_ptr<flatbuffers::FlatBufferBuilder> const & builder);

		void on_account_weight (scendereapi::Envelope const & envelope);
		void on_is_alive (scendereapi::Envelope const & envelope);
		void on_topic_confirmation (scendereapi::Envelope const & envelope);

		/** Request to register a service. The service name is associated with the current session. */
		void on_service_register (scendereapi::Envelope const & envelope);

		/** Request to stop a service by name */
		void on_service_stop (scendereapi::Envelope const & envelope);

		/** Subscribe to the ServiceStop event. The service must first have registered itself on the same session. */
		void on_topic_service_stop (scendereapi::Envelope const & envelope);

		/** Returns a mapping from api message types to handler functions */
		static auto handler_map () -> std::unordered_map<scendereapi::Message, std::function<void (action_handler *, scendereapi::Envelope const &)>, scendere::ipc::enum_hash>;

	private:
		bool has_access (scendereapi::Envelope const & envelope_a, scendere::ipc::access_permission permission_a) const noexcept;
		bool has_access_to_all (scendereapi::Envelope const & envelope_a, std::initializer_list<scendere::ipc::access_permission> permissions_a) const noexcept;
		bool has_access_to_oneof (scendereapi::Envelope const & envelope_a, std::initializer_list<scendere::ipc::access_permission> permissions_a) const noexcept;
		void require (scendereapi::Envelope const & envelope_a, scendere::ipc::access_permission permission_a) const;
		void require_all (scendereapi::Envelope const & envelope_a, std::initializer_list<scendere::ipc::access_permission> permissions_a) const;
		void require_oneof (scendereapi::Envelope const & envelope_a, std::initializer_list<scendere::ipc::access_permission> alternative_permissions_a) const;

		scendere::node & node;
		scendere::ipc::ipc_server & ipc_server;
		std::weak_ptr<scendere::ipc::subscriber> subscriber;
	};
}
}
