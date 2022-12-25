#include <scendere/ipc_flatbuffers_lib/generated/flatbuffers/scendereapi_generated.h>
#include <scendere/lib/errors.hpp>
#include <scendere/lib/numbers.hpp>
#include <scendere/node/ipc/action_handler.hpp>
#include <scendere/node/ipc/ipc_server.hpp>
#include <scendere/node/node.hpp>

#include <iostream>

namespace
{
scendere::account parse_account (std::string const & account, bool & out_is_deprecated_format)
{
	scendere::account result{};
	if (account.empty ())
	{
		throw scendere::error (scendere::error_common::bad_account_number);
	}

	if (result.decode_account (account))
	{
		throw scendere::error (scendere::error_common::bad_account_number);
	}
	else if (account[3] == '-' || account[4] == '-')
	{
		out_is_deprecated_format = true;
	}

	return result;
}
/** Returns the message as a Flatbuffers ObjectAPI type, managed by a unique_ptr */
template <typename T>
auto get_message (scendereapi::Envelope const & envelope)
{
	auto raw (envelope.message_as<T> ()->UnPack ());
	return std::unique_ptr<typename T::NativeTableType> (raw);
}
}

/**
 * Mapping from message type to handler function.
 * @note This must be updated whenever a new message type is added to the Flatbuffers IDL.
 */
auto scendere::ipc::action_handler::handler_map () -> std::unordered_map<scendereapi::Message, std::function<void (scendere::ipc::action_handler *, scendereapi::Envelope const &)>, scendere::ipc::enum_hash>
{
	static std::unordered_map<scendereapi::Message, std::function<void (scendere::ipc::action_handler *, scendereapi::Envelope const &)>, scendere::ipc::enum_hash> handlers;
	if (handlers.empty ())
	{
		handlers.emplace (scendereapi::Message::Message_IsAlive, &scendere::ipc::action_handler::on_is_alive);
		handlers.emplace (scendereapi::Message::Message_TopicConfirmation, &scendere::ipc::action_handler::on_topic_confirmation);
		handlers.emplace (scendereapi::Message::Message_AccountWeight, &scendere::ipc::action_handler::on_account_weight);
		handlers.emplace (scendereapi::Message::Message_ServiceRegister, &scendere::ipc::action_handler::on_service_register);
		handlers.emplace (scendereapi::Message::Message_ServiceStop, &scendere::ipc::action_handler::on_service_stop);
		handlers.emplace (scendereapi::Message::Message_TopicServiceStop, &scendere::ipc::action_handler::on_topic_service_stop);
	}
	return handlers;
}

scendere::ipc::action_handler::action_handler (scendere::node & node_a, scendere::ipc::ipc_server & server_a, std::weak_ptr<scendere::ipc::subscriber> const & subscriber_a, std::shared_ptr<flatbuffers::FlatBufferBuilder> const & builder_a) :
	flatbuffer_producer (builder_a),
	node (node_a),
	ipc_server (server_a),
	subscriber (subscriber_a)
{
}

void scendere::ipc::action_handler::on_topic_confirmation (scendereapi::Envelope const & envelope_a)
{
	auto confirmationTopic (get_message<scendereapi::TopicConfirmation> (envelope_a));
	ipc_server.get_broker ()->subscribe (subscriber, std::move (confirmationTopic));
	scendereapi::EventAckT ack;
	create_response (ack);
}

void scendere::ipc::action_handler::on_service_register (scendereapi::Envelope const & envelope_a)
{
	require_oneof (envelope_a, { scendere::ipc::access_permission::api_service_register, scendere::ipc::access_permission::service });
	auto query (get_message<scendereapi::ServiceRegister> (envelope_a));
	ipc_server.get_broker ()->service_register (query->service_name, this->subscriber);
	scendereapi::SuccessT success;
	create_response (success);
}

void scendere::ipc::action_handler::on_service_stop (scendereapi::Envelope const & envelope_a)
{
	require_oneof (envelope_a, { scendere::ipc::access_permission::api_service_stop, scendere::ipc::access_permission::service });
	auto query (get_message<scendereapi::ServiceStop> (envelope_a));
	if (query->service_name == "node")
	{
		ipc_server.node.stop ();
	}
	else
	{
		ipc_server.get_broker ()->service_stop (query->service_name);
	}
	scendereapi::SuccessT success;
	create_response (success);
}

void scendere::ipc::action_handler::on_topic_service_stop (scendereapi::Envelope const & envelope_a)
{
	auto topic (get_message<scendereapi::TopicServiceStop> (envelope_a));
	ipc_server.get_broker ()->subscribe (subscriber, std::move (topic));
	scendereapi::EventAckT ack;
	create_response (ack);
}

void scendere::ipc::action_handler::on_account_weight (scendereapi::Envelope const & envelope_a)
{
	require_oneof (envelope_a, { scendere::ipc::access_permission::api_account_weight, scendere::ipc::access_permission::account_query });
	bool is_deprecated_format{ false };
	auto query (get_message<scendereapi::AccountWeight> (envelope_a));
	auto balance (node.weight (parse_account (query->account, is_deprecated_format)));

	scendereapi::AccountWeightResponseT response;
	response.voting_weight = balance.str ();
	create_response (response);
}

void scendere::ipc::action_handler::on_is_alive (scendereapi::Envelope const & envelope)
{
	scendereapi::IsAliveT alive;
	create_response (alive);
}

bool scendere::ipc::action_handler::has_access (scendereapi::Envelope const & envelope_a, scendere::ipc::access_permission permission_a) const noexcept
{
	// If credentials are missing in the envelope, the default user is used
	std::string credentials;
	if (envelope_a.credentials () != nullptr)
	{
		credentials = envelope_a.credentials ()->str ();
	}

	return ipc_server.get_access ().has_access (credentials, permission_a);
}

bool scendere::ipc::action_handler::has_access_to_all (scendereapi::Envelope const & envelope_a, std::initializer_list<scendere::ipc::access_permission> permissions_a) const noexcept
{
	// If credentials are missing in the envelope, the default user is used
	std::string credentials;
	if (envelope_a.credentials () != nullptr)
	{
		credentials = envelope_a.credentials ()->str ();
	}

	return ipc_server.get_access ().has_access_to_all (credentials, permissions_a);
}

bool scendere::ipc::action_handler::has_access_to_oneof (scendereapi::Envelope const & envelope_a, std::initializer_list<scendere::ipc::access_permission> permissions_a) const noexcept
{
	// If credentials are missing in the envelope, the default user is used
	std::string credentials;
	if (envelope_a.credentials () != nullptr)
	{
		credentials = envelope_a.credentials ()->str ();
	}

	return ipc_server.get_access ().has_access_to_oneof (credentials, permissions_a);
}

void scendere::ipc::action_handler::require (scendereapi::Envelope const & envelope_a, scendere::ipc::access_permission permission_a) const
{
	if (!has_access (envelope_a, permission_a))
	{
		throw scendere::error (scendere::error_common::access_denied);
	}
}

void scendere::ipc::action_handler::require_all (scendereapi::Envelope const & envelope_a, std::initializer_list<scendere::ipc::access_permission> permissions_a) const
{
	if (!has_access_to_all (envelope_a, permissions_a))
	{
		throw scendere::error (scendere::error_common::access_denied);
	}
}

void scendere::ipc::action_handler::require_oneof (scendereapi::Envelope const & envelope_a, std::initializer_list<scendere::ipc::access_permission> permissions_a) const
{
	if (!has_access_to_oneof (envelope_a, permissions_a))
	{
		throw scendere::error (scendere::error_common::access_denied);
	}
}
