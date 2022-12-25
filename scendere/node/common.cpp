#include <scendere/lib/blocks.hpp>
#include <scendere/lib/memory.hpp>
#include <scendere/lib/work.hpp>
#include <scendere/node/active_transactions.hpp>
#include <scendere/node/common.hpp>
#include <scendere/node/election.hpp>
#include <scendere/node/network.hpp>
#include <scendere/node/wallet.hpp>
#include <scendere/secure/buffer.hpp>

#include <boost/endian/conversion.hpp>
#include <boost/format.hpp>
#include <boost/pool/pool_alloc.hpp>
#include <boost/variant/get.hpp>

#include <numeric>
#include <sstream>

std::bitset<16> constexpr scendere::message_header::block_type_mask;
std::bitset<16> constexpr scendere::message_header::count_mask;
std::bitset<16> constexpr scendere::message_header::telemetry_size_mask;

std::chrono::seconds constexpr scendere::telemetry_cache_cutoffs::dev;
std::chrono::seconds constexpr scendere::telemetry_cache_cutoffs::beta;
std::chrono::seconds constexpr scendere::telemetry_cache_cutoffs::live;

uint64_t scendere::ip_address_hash_raw (boost::asio::ip::address const & ip_a, uint16_t port)
{
	debug_assert (ip_a.is_v6 ());
	uint64_t result;
	scendere::uint128_union address;
	address.bytes = ip_a.to_v6 ().to_bytes ();
	blake2b_state state;
	blake2b_init (&state, sizeof (result));
	blake2b_update (&state, scendere::hardened_constants::get ().random_128.bytes.data (), scendere::hardened_constants::get ().random_128.bytes.size ());
	if (port != 0)
	{
		blake2b_update (&state, &port, sizeof (port));
	}
	blake2b_update (&state, address.bytes.data (), address.bytes.size ());
	blake2b_final (&state, &result, sizeof (result));
	return result;
}

scendere::message_header::message_header (scendere::network_constants const & constants, scendere::message_type type_a) :
	network{ constants.current_network },
	version_max{ constants.protocol_version },
	version_using{ constants.protocol_version },
	version_min{ constants.protocol_version_min },
	type (type_a)
{
}

scendere::message_header::message_header (bool & error_a, scendere::stream & stream_a)
{
	if (!error_a)
	{
		error_a = deserialize (stream_a);
	}
}

void scendere::message_header::serialize (scendere::stream & stream_a) const
{
	scendere::write (stream_a, boost::endian::native_to_big (static_cast<uint16_t> (network)));
	scendere::write (stream_a, version_max);
	scendere::write (stream_a, version_using);
	scendere::write (stream_a, version_min);
	scendere::write (stream_a, type);
	scendere::write (stream_a, static_cast<uint16_t> (extensions.to_ullong ()));
}

bool scendere::message_header::deserialize (scendere::stream & stream_a)
{
	auto error (false);
	try
	{
		uint16_t network_bytes;
		scendere::read (stream_a, network_bytes);
		network = static_cast<scendere::networks> (boost::endian::big_to_native (network_bytes));
		scendere::read (stream_a, version_max);
		scendere::read (stream_a, version_using);
		scendere::read (stream_a, version_min);
		scendere::read (stream_a, type);
		uint16_t extensions_l;
		scendere::read (stream_a, extensions_l);
		extensions = extensions_l;
	}
	catch (std::runtime_error const &)
	{
		error = true;
	}

	return error;
}

std::string scendere::message_type_to_string (scendere::message_type message_type_l)
{
	switch (message_type_l)
	{
		case scendere::message_type::invalid:
			return "invalid";
		case scendere::message_type::not_a_type:
			return "not_a_type";
		case scendere::message_type::keepalive:
			return "keepalive";
		case scendere::message_type::publish:
			return "publish";
		case scendere::message_type::confirm_req:
			return "confirm_req";
		case scendere::message_type::confirm_ack:
			return "confirm_ack";
		case scendere::message_type::bulk_pull:
			return "bulk_pull";
		case scendere::message_type::bulk_push:
			return "bulk_push";
		case scendere::message_type::frontier_req:
			return "frontier_req";
		case scendere::message_type::node_id_handshake:
			return "node_id_handshake";
		case scendere::message_type::bulk_pull_account:
			return "bulk_pull_account";
		case scendere::message_type::telemetry_req:
			return "telemetry_req";
		case scendere::message_type::telemetry_ack:
			return "telemetry_ack";
			// default case intentionally omitted to cause warnings for unhandled enums
	}

	return "n/a";
}

std::string scendere::message_header::to_string ()
{
	// Cast to uint16_t to get integer value since uint8_t is treated as an unsigned char in string formatting.
	uint16_t type_l = static_cast<uint16_t> (type);
	uint16_t version_max_l = static_cast<uint16_t> (version_max);
	uint16_t version_using_l = static_cast<uint16_t> (version_using);
	uint16_t version_min_l = static_cast<uint16_t> (version_min);
	std::string type_text = scendere::message_type_to_string (type);

	std::stringstream stream;

	stream << boost::format ("NetID: %1%(%2%), ") % scendere::to_string_hex (static_cast<uint16_t> (network)) % scendere::network::to_string (network);
	stream << boost::format ("VerMaxUsingMin: %1%/%2%/%3%, ") % version_max_l % version_using_l % version_min_l;
	stream << boost::format ("MsgType: %1%(%2%), ") % type_l % type_text;
	stream << boost::format ("Extensions: %1%") % scendere::to_string_hex (static_cast<uint16_t> (extensions.to_ulong ()));

	return stream.str ();
}

scendere::message::message (scendere::network_constants const & constants, scendere::message_type type_a) :
	header (constants, type_a)
{
}

scendere::message::message (scendere::message_header const & header_a) :
	header (header_a)
{
}

std::shared_ptr<std::vector<uint8_t>> scendere::message::to_bytes () const
{
	auto bytes = std::make_shared<std::vector<uint8_t>> ();
	scendere::vectorstream stream (*bytes);
	serialize (stream);
	return bytes;
}

scendere::shared_const_buffer scendere::message::to_shared_const_buffer () const
{
	return shared_const_buffer (to_bytes ());
}

scendere::block_type scendere::message_header::block_type () const
{
	return static_cast<scendere::block_type> (((extensions & block_type_mask) >> 8).to_ullong ());
}

void scendere::message_header::block_type_set (scendere::block_type type_a)
{
	extensions &= ~block_type_mask;
	extensions |= std::bitset<16> (static_cast<unsigned long long> (type_a) << 8);
}

uint8_t scendere::message_header::count_get () const
{
	return static_cast<uint8_t> (((extensions & count_mask) >> 12).to_ullong ());
}

void scendere::message_header::count_set (uint8_t count_a)
{
	debug_assert (count_a < 16);
	extensions &= ~count_mask;
	extensions |= std::bitset<16> (static_cast<unsigned long long> (count_a) << 12);
}

void scendere::message_header::flag_set (uint8_t flag_a)
{
	// Flags from 8 are block_type & count
	debug_assert (flag_a < 8);
	extensions.set (flag_a, true);
}

bool scendere::message_header::bulk_pull_is_count_present () const
{
	auto result (false);
	if (type == scendere::message_type::bulk_pull)
	{
		if (extensions.test (bulk_pull_count_present_flag))
		{
			result = true;
		}
	}
	return result;
}

bool scendere::message_header::frontier_req_is_only_confirmed_present () const
{
	auto result (false);
	if (type == scendere::message_type::frontier_req)
	{
		if (extensions.test (frontier_req_only_confirmed))
		{
			result = true;
		}
	}
	return result;
}

bool scendere::message_header::node_id_handshake_is_query () const
{
	auto result (false);
	if (type == scendere::message_type::node_id_handshake)
	{
		if (extensions.test (node_id_handshake_query_flag))
		{
			result = true;
		}
	}
	return result;
}

bool scendere::message_header::node_id_handshake_is_response () const
{
	auto result (false);
	if (type == scendere::message_type::node_id_handshake)
	{
		if (extensions.test (node_id_handshake_response_flag))
		{
			result = true;
		}
	}
	return result;
}

std::size_t scendere::message_header::payload_length_bytes () const
{
	switch (type)
	{
		case scendere::message_type::bulk_pull:
		{
			return scendere::bulk_pull::size + (bulk_pull_is_count_present () ? scendere::bulk_pull::extended_parameters_size : 0);
		}
		case scendere::message_type::bulk_push:
		case scendere::message_type::telemetry_req:
		{
			// These don't have a payload
			return 0;
		}
		case scendere::message_type::frontier_req:
		{
			return scendere::frontier_req::size;
		}
		case scendere::message_type::bulk_pull_account:
		{
			return scendere::bulk_pull_account::size;
		}
		case scendere::message_type::keepalive:
		{
			return scendere::keepalive::size;
		}
		case scendere::message_type::publish:
		{
			return scendere::block::size (block_type ());
		}
		case scendere::message_type::confirm_ack:
		{
			return scendere::confirm_ack::size (block_type (), count_get ());
		}
		case scendere::message_type::confirm_req:
		{
			return scendere::confirm_req::size (block_type (), count_get ());
		}
		case scendere::message_type::node_id_handshake:
		{
			return scendere::node_id_handshake::size (*this);
		}
		case scendere::message_type::telemetry_ack:
		{
			return scendere::telemetry_ack::size (*this);
		}
		default:
		{
			debug_assert (false);
			return 0;
		}
	}
}

// MTU - IP header - UDP header
std::size_t const scendere::message_parser::max_safe_udp_message_size = 508;

std::string scendere::message_parser::status_string ()
{
	switch (status)
	{
		case scendere::message_parser::parse_status::success:
		{
			return "success";
		}
		case scendere::message_parser::parse_status::insufficient_work:
		{
			return "insufficient_work";
		}
		case scendere::message_parser::parse_status::invalid_header:
		{
			return "invalid_header";
		}
		case scendere::message_parser::parse_status::invalid_message_type:
		{
			return "invalid_message_type";
		}
		case scendere::message_parser::parse_status::invalid_keepalive_message:
		{
			return "invalid_keepalive_message";
		}
		case scendere::message_parser::parse_status::invalid_publish_message:
		{
			return "invalid_publish_message";
		}
		case scendere::message_parser::parse_status::invalid_confirm_req_message:
		{
			return "invalid_confirm_req_message";
		}
		case scendere::message_parser::parse_status::invalid_confirm_ack_message:
		{
			return "invalid_confirm_ack_message";
		}
		case scendere::message_parser::parse_status::invalid_node_id_handshake_message:
		{
			return "invalid_node_id_handshake_message";
		}
		case scendere::message_parser::parse_status::invalid_telemetry_req_message:
		{
			return "invalid_telemetry_req_message";
		}
		case scendere::message_parser::parse_status::invalid_telemetry_ack_message:
		{
			return "invalid_telemetry_ack_message";
		}
		case scendere::message_parser::parse_status::outdated_version:
		{
			return "outdated_version";
		}
		case scendere::message_parser::parse_status::duplicate_publish_message:
		{
			return "duplicate_publish_message";
		}
	}

	debug_assert (false);

	return "[unknown parse_status]";
}

scendere::message_parser::message_parser (scendere::network_filter & publish_filter_a, scendere::block_uniquer & block_uniquer_a, scendere::vote_uniquer & vote_uniquer_a, scendere::message_visitor & visitor_a, scendere::work_pool & pool_a, scendere::network_constants const & network) :
	publish_filter (publish_filter_a),
	block_uniquer (block_uniquer_a),
	vote_uniquer (vote_uniquer_a),
	visitor (visitor_a),
	pool (pool_a),
	status (parse_status::success),
	network{ network }
{
}

void scendere::message_parser::deserialize_buffer (uint8_t const * buffer_a, std::size_t size_a)
{
	status = parse_status::success;
	auto error (false);
	if (size_a <= max_safe_udp_message_size)
	{
		// Guaranteed to be deliverable
		scendere::bufferstream stream (buffer_a, size_a);
		scendere::message_header header (error, stream);
		if (!error)
		{
			if (header.version_using < network.protocol_version_min)
			{
				status = parse_status::outdated_version;
			}
			else
			{
				switch (header.type)
				{
					case scendere::message_type::keepalive:
					{
						deserialize_keepalive (stream, header);
						break;
					}
					case scendere::message_type::publish:
					{
						scendere::uint128_t digest;
						if (!publish_filter.apply (buffer_a + header.size, size_a - header.size, &digest))
						{
							deserialize_publish (stream, header, digest);
						}
						else
						{
							status = parse_status::duplicate_publish_message;
						}
						break;
					}
					case scendere::message_type::confirm_req:
					{
						deserialize_confirm_req (stream, header);
						break;
					}
					case scendere::message_type::confirm_ack:
					{
						deserialize_confirm_ack (stream, header);
						break;
					}
					case scendere::message_type::node_id_handshake:
					{
						deserialize_node_id_handshake (stream, header);
						break;
					}
					case scendere::message_type::telemetry_req:
					{
						deserialize_telemetry_req (stream, header);
						break;
					}
					case scendere::message_type::telemetry_ack:
					{
						deserialize_telemetry_ack (stream, header);
						break;
					}
					default:
					{
						status = parse_status::invalid_message_type;
						break;
					}
				}
			}
		}
		else
		{
			status = parse_status::invalid_header;
		}
	}
}

void scendere::message_parser::deserialize_keepalive (scendere::stream & stream_a, scendere::message_header const & header_a)
{
	auto error (false);
	scendere::keepalive incoming (error, stream_a, header_a);
	if (!error && at_end (stream_a))
	{
		visitor.keepalive (incoming);
	}
	else
	{
		status = parse_status::invalid_keepalive_message;
	}
}

void scendere::message_parser::deserialize_publish (scendere::stream & stream_a, scendere::message_header const & header_a, scendere::uint128_t const & digest_a)
{
	auto error (false);
	scendere::publish incoming (error, stream_a, header_a, digest_a, &block_uniquer);
	if (!error && at_end (stream_a))
	{
		if (!network.work.validate_entry (*incoming.block))
		{
			visitor.publish (incoming);
		}
		else
		{
			status = parse_status::insufficient_work;
		}
	}
	else
	{
		status = parse_status::invalid_publish_message;
	}
}

void scendere::message_parser::deserialize_confirm_req (scendere::stream & stream_a, scendere::message_header const & header_a)
{
	auto error (false);
	scendere::confirm_req incoming (error, stream_a, header_a, &block_uniquer);
	if (!error && at_end (stream_a))
	{
		if (incoming.block == nullptr || !network.work.validate_entry (*incoming.block))
		{
			visitor.confirm_req (incoming);
		}
		else
		{
			status = parse_status::insufficient_work;
		}
	}
	else
	{
		status = parse_status::invalid_confirm_req_message;
	}
}

void scendere::message_parser::deserialize_confirm_ack (scendere::stream & stream_a, scendere::message_header const & header_a)
{
	auto error (false);
	scendere::confirm_ack incoming (error, stream_a, header_a, &vote_uniquer);
	if (!error && at_end (stream_a))
	{
		for (auto & vote_block : incoming.vote->blocks)
		{
			if (!vote_block.which ())
			{
				auto const & block (boost::get<std::shared_ptr<scendere::block>> (vote_block));
				if (network.work.validate_entry (*block))
				{
					status = parse_status::insufficient_work;
					break;
				}
			}
		}
		if (status == parse_status::success)
		{
			visitor.confirm_ack (incoming);
		}
	}
	else
	{
		status = parse_status::invalid_confirm_ack_message;
	}
}

void scendere::message_parser::deserialize_node_id_handshake (scendere::stream & stream_a, scendere::message_header const & header_a)
{
	bool error_l (false);
	scendere::node_id_handshake incoming (error_l, stream_a, header_a);
	if (!error_l && at_end (stream_a))
	{
		visitor.node_id_handshake (incoming);
	}
	else
	{
		status = parse_status::invalid_node_id_handshake_message;
	}
}

void scendere::message_parser::deserialize_telemetry_req (scendere::stream & stream_a, scendere::message_header const & header_a)
{
	scendere::telemetry_req incoming (header_a);
	if (at_end (stream_a))
	{
		visitor.telemetry_req (incoming);
	}
	else
	{
		status = parse_status::invalid_telemetry_req_message;
	}
}

void scendere::message_parser::deserialize_telemetry_ack (scendere::stream & stream_a, scendere::message_header const & header_a)
{
	bool error_l (false);
	scendere::telemetry_ack incoming (error_l, stream_a, header_a);
	// Intentionally not checking if at the end of stream, because these messages support backwards/forwards compatibility
	if (!error_l)
	{
		visitor.telemetry_ack (incoming);
	}
	else
	{
		status = parse_status::invalid_telemetry_ack_message;
	}
}

bool scendere::message_parser::at_end (scendere::stream & stream_a)
{
	uint8_t junk;
	auto end (scendere::try_read (stream_a, junk));
	return end;
}

scendere::keepalive::keepalive (scendere::network_constants const & constants) :
	message (constants, scendere::message_type::keepalive)
{
	scendere::endpoint endpoint (boost::asio::ip::address_v6{}, 0);
	for (auto i (peers.begin ()), n (peers.end ()); i != n; ++i)
	{
		*i = endpoint;
	}
}

scendere::keepalive::keepalive (bool & error_a, scendere::stream & stream_a, scendere::message_header const & header_a) :
	message (header_a)
{
	if (!error_a)
	{
		error_a = deserialize (stream_a);
	}
}

void scendere::keepalive::visit (scendere::message_visitor & visitor_a) const
{
	visitor_a.keepalive (*this);
}

void scendere::keepalive::serialize (scendere::stream & stream_a) const
{
	header.serialize (stream_a);
	for (auto i (peers.begin ()), j (peers.end ()); i != j; ++i)
	{
		debug_assert (i->address ().is_v6 ());
		auto bytes (i->address ().to_v6 ().to_bytes ());
		write (stream_a, bytes);
		write (stream_a, i->port ());
	}
}

bool scendere::keepalive::deserialize (scendere::stream & stream_a)
{
	debug_assert (header.type == scendere::message_type::keepalive);
	auto error (false);
	for (auto i (peers.begin ()), j (peers.end ()); i != j && !error; ++i)
	{
		std::array<uint8_t, 16> address;
		uint16_t port;
		if (!try_read (stream_a, address) && !try_read (stream_a, port))
		{
			*i = scendere::endpoint (boost::asio::ip::address_v6 (address), port);
		}
		else
		{
			error = true;
		}
	}
	return error;
}

bool scendere::keepalive::operator== (scendere::keepalive const & other_a) const
{
	return peers == other_a.peers;
}

scendere::publish::publish (bool & error_a, scendere::stream & stream_a, scendere::message_header const & header_a, scendere::uint128_t const & digest_a, scendere::block_uniquer * uniquer_a) :
	message (header_a),
	digest (digest_a)
{
	if (!error_a)
	{
		error_a = deserialize (stream_a, uniquer_a);
	}
}

scendere::publish::publish (scendere::network_constants const & constants, std::shared_ptr<scendere::block> const & block_a) :
	message (constants, scendere::message_type::publish),
	block (block_a)
{
	header.block_type_set (block->type ());
}

void scendere::publish::serialize (scendere::stream & stream_a) const
{
	debug_assert (block != nullptr);
	header.serialize (stream_a);
	block->serialize (stream_a);
}

bool scendere::publish::deserialize (scendere::stream & stream_a, scendere::block_uniquer * uniquer_a)
{
	debug_assert (header.type == scendere::message_type::publish);
	block = scendere::deserialize_block (stream_a, header.block_type (), uniquer_a);
	auto result (block == nullptr);
	return result;
}

void scendere::publish::visit (scendere::message_visitor & visitor_a) const
{
	visitor_a.publish (*this);
}

bool scendere::publish::operator== (scendere::publish const & other_a) const
{
	return *block == *other_a.block;
}

scendere::confirm_req::confirm_req (bool & error_a, scendere::stream & stream_a, scendere::message_header const & header_a, scendere::block_uniquer * uniquer_a) :
	message (header_a)
{
	if (!error_a)
	{
		error_a = deserialize (stream_a, uniquer_a);
	}
}

scendere::confirm_req::confirm_req (scendere::network_constants const & constants, std::shared_ptr<scendere::block> const & block_a) :
	message (constants, scendere::message_type::confirm_req),
	block (block_a)
{
	header.block_type_set (block->type ());
}

scendere::confirm_req::confirm_req (scendere::network_constants const & constants, std::vector<std::pair<scendere::block_hash, scendere::root>> const & roots_hashes_a) :
	message (constants, scendere::message_type::confirm_req),
	roots_hashes (roots_hashes_a)
{
	// not_a_block (1) block type for hashes + roots request
	header.block_type_set (scendere::block_type::not_a_block);
	debug_assert (roots_hashes.size () < 16);
	header.count_set (static_cast<uint8_t> (roots_hashes.size ()));
}

scendere::confirm_req::confirm_req (scendere::network_constants const & constants, scendere::block_hash const & hash_a, scendere::root const & root_a) :
	message (constants, scendere::message_type::confirm_req),
	roots_hashes (std::vector<std::pair<scendere::block_hash, scendere::root>> (1, std::make_pair (hash_a, root_a)))
{
	debug_assert (!roots_hashes.empty ());
	// not_a_block (1) block type for hashes + roots request
	header.block_type_set (scendere::block_type::not_a_block);
	debug_assert (roots_hashes.size () < 16);
	header.count_set (static_cast<uint8_t> (roots_hashes.size ()));
}

void scendere::confirm_req::visit (scendere::message_visitor & visitor_a) const
{
	visitor_a.confirm_req (*this);
}

void scendere::confirm_req::serialize (scendere::stream & stream_a) const
{
	header.serialize (stream_a);
	if (header.block_type () == scendere::block_type::not_a_block)
	{
		debug_assert (!roots_hashes.empty ());
		// Write hashes & roots
		for (auto & root_hash : roots_hashes)
		{
			write (stream_a, root_hash.first);
			write (stream_a, root_hash.second);
		}
	}
	else
	{
		debug_assert (block != nullptr);
		block->serialize (stream_a);
	}
}

bool scendere::confirm_req::deserialize (scendere::stream & stream_a, scendere::block_uniquer * uniquer_a)
{
	bool result (false);
	debug_assert (header.type == scendere::message_type::confirm_req);
	try
	{
		if (header.block_type () == scendere::block_type::not_a_block)
		{
			uint8_t count (header.count_get ());
			for (auto i (0); i != count && !result; ++i)
			{
				scendere::block_hash block_hash (0);
				scendere::block_hash root (0);
				read (stream_a, block_hash);
				read (stream_a, root);
				if (!block_hash.is_zero () || !root.is_zero ())
				{
					roots_hashes.emplace_back (block_hash, root);
				}
			}

			result = roots_hashes.empty () || (roots_hashes.size () != count);
		}
		else
		{
			block = scendere::deserialize_block (stream_a, header.block_type (), uniquer_a);
			result = block == nullptr;
		}
	}
	catch (std::runtime_error const &)
	{
		result = true;
	}

	return result;
}

bool scendere::confirm_req::operator== (scendere::confirm_req const & other_a) const
{
	bool equal (false);
	if (block != nullptr && other_a.block != nullptr)
	{
		equal = *block == *other_a.block;
	}
	else if (!roots_hashes.empty () && !other_a.roots_hashes.empty ())
	{
		equal = roots_hashes == other_a.roots_hashes;
	}
	return equal;
}

std::string scendere::confirm_req::roots_string () const
{
	std::string result;
	for (auto & root_hash : roots_hashes)
	{
		result += root_hash.first.to_string ();
		result += ":";
		result += root_hash.second.to_string ();
		result += ", ";
	}
	return result;
}

std::size_t scendere::confirm_req::size (scendere::block_type type_a, std::size_t count)
{
	std::size_t result (0);
	if (type_a != scendere::block_type::invalid && type_a != scendere::block_type::not_a_block)
	{
		result = scendere::block::size (type_a);
	}
	else if (type_a == scendere::block_type::not_a_block)
	{
		result = count * (sizeof (scendere::uint256_union) + sizeof (scendere::block_hash));
	}
	return result;
}

scendere::confirm_ack::confirm_ack (bool & error_a, scendere::stream & stream_a, scendere::message_header const & header_a, scendere::vote_uniquer * uniquer_a) :
	message (header_a),
	vote (scendere::make_shared<scendere::vote> (error_a, stream_a, header.block_type ()))
{
	if (!error_a && uniquer_a)
	{
		vote = uniquer_a->unique (vote);
	}
}

scendere::confirm_ack::confirm_ack (scendere::network_constants const & constants, std::shared_ptr<scendere::vote> const & vote_a) :
	message (constants, scendere::message_type::confirm_ack),
	vote (vote_a)
{
	debug_assert (!vote_a->blocks.empty ());
	auto & first_vote_block (vote_a->blocks[0]);
	if (first_vote_block.which ())
	{
		header.block_type_set (scendere::block_type::not_a_block);
		debug_assert (vote_a->blocks.size () < 16);
		header.count_set (static_cast<uint8_t> (vote_a->blocks.size ()));
	}
	else
	{
		header.block_type_set (boost::get<std::shared_ptr<scendere::block>> (first_vote_block)->type ());
	}
}

void scendere::confirm_ack::serialize (scendere::stream & stream_a) const
{
	debug_assert (header.block_type () == scendere::block_type::not_a_block || header.block_type () == scendere::block_type::send || header.block_type () == scendere::block_type::receive || header.block_type () == scendere::block_type::open || header.block_type () == scendere::block_type::change || header.block_type () == scendere::block_type::state);
	header.serialize (stream_a);
	vote->serialize (stream_a, header.block_type ());
}

bool scendere::confirm_ack::operator== (scendere::confirm_ack const & other_a) const
{
	auto result (*vote == *other_a.vote);
	return result;
}

void scendere::confirm_ack::visit (scendere::message_visitor & visitor_a) const
{
	visitor_a.confirm_ack (*this);
}

std::size_t scendere::confirm_ack::size (scendere::block_type type_a, std::size_t count)
{
	std::size_t result (sizeof (scendere::account) + sizeof (scendere::signature) + sizeof (uint64_t));
	if (type_a != scendere::block_type::invalid && type_a != scendere::block_type::not_a_block)
	{
		result += scendere::block::size (type_a);
	}
	else if (type_a == scendere::block_type::not_a_block)
	{
		result += count * sizeof (scendere::block_hash);
	}
	return result;
}

scendere::frontier_req::frontier_req (scendere::network_constants const & constants) :
	message (constants, scendere::message_type::frontier_req)
{
}

scendere::frontier_req::frontier_req (bool & error_a, scendere::stream & stream_a, scendere::message_header const & header_a) :
	message (header_a)
{
	if (!error_a)
	{
		error_a = deserialize (stream_a);
	}
}

void scendere::frontier_req::serialize (scendere::stream & stream_a) const
{
	header.serialize (stream_a);
	write (stream_a, start.bytes);
	write (stream_a, age);
	write (stream_a, count);
}

bool scendere::frontier_req::deserialize (scendere::stream & stream_a)
{
	debug_assert (header.type == scendere::message_type::frontier_req);
	auto error (false);
	try
	{
		scendere::read (stream_a, start.bytes);
		scendere::read (stream_a, age);
		scendere::read (stream_a, count);
	}
	catch (std::runtime_error const &)
	{
		error = true;
	}

	return error;
}

void scendere::frontier_req::visit (scendere::message_visitor & visitor_a) const
{
	visitor_a.frontier_req (*this);
}

bool scendere::frontier_req::operator== (scendere::frontier_req const & other_a) const
{
	return start == other_a.start && age == other_a.age && count == other_a.count;
}

scendere::bulk_pull::bulk_pull (scendere::network_constants const & constants) :
	message (constants, scendere::message_type::bulk_pull)
{
}

scendere::bulk_pull::bulk_pull (bool & error_a, scendere::stream & stream_a, scendere::message_header const & header_a) :
	message (header_a)
{
	if (!error_a)
	{
		error_a = deserialize (stream_a);
	}
}

void scendere::bulk_pull::visit (scendere::message_visitor & visitor_a) const
{
	visitor_a.bulk_pull (*this);
}

void scendere::bulk_pull::serialize (scendere::stream & stream_a) const
{
	/*
	 * Ensure the "count_present" flag is set if there
	 * is a limit specifed.  Additionally, do not allow
	 * the "count_present" flag with a value of 0, since
	 * that is a sentinel which we use to mean "all blocks"
	 * and that is the behavior of not having the flag set
	 * so it is wasteful to do this.
	 */
	debug_assert ((count == 0 && !is_count_present ()) || (count != 0 && is_count_present ()));

	header.serialize (stream_a);
	write (stream_a, start);
	write (stream_a, end);

	if (is_count_present ())
	{
		std::array<uint8_t, extended_parameters_size> count_buffer{ { 0 } };
		decltype (count) count_little_endian;
		static_assert (sizeof (count_little_endian) < (count_buffer.size () - 1), "count must fit within buffer");

		count_little_endian = boost::endian::native_to_little (count);
		memcpy (count_buffer.data () + 1, &count_little_endian, sizeof (count_little_endian));

		write (stream_a, count_buffer);
	}
}

bool scendere::bulk_pull::deserialize (scendere::stream & stream_a)
{
	debug_assert (header.type == scendere::message_type::bulk_pull);
	auto error (false);
	try
	{
		scendere::read (stream_a, start);
		scendere::read (stream_a, end);

		if (is_count_present ())
		{
			std::array<uint8_t, extended_parameters_size> extended_parameters_buffers;
			static_assert (sizeof (count) < (extended_parameters_buffers.size () - 1), "count must fit within buffer");

			scendere::read (stream_a, extended_parameters_buffers);
			if (extended_parameters_buffers.front () != 0)
			{
				error = true;
			}
			else
			{
				memcpy (&count, extended_parameters_buffers.data () + 1, sizeof (count));
				boost::endian::little_to_native_inplace (count);
			}
		}
		else
		{
			count = 0;
		}
	}
	catch (std::runtime_error const &)
	{
		error = true;
	}

	return error;
}

bool scendere::bulk_pull::is_count_present () const
{
	return header.extensions.test (count_present_flag);
}

void scendere::bulk_pull::set_count_present (bool value_a)
{
	header.extensions.set (count_present_flag, value_a);
}

scendere::bulk_pull_account::bulk_pull_account (scendere::network_constants const & constants) :
	message (constants, scendere::message_type::bulk_pull_account)
{
}

scendere::bulk_pull_account::bulk_pull_account (bool & error_a, scendere::stream & stream_a, scendere::message_header const & header_a) :
	message (header_a)
{
	if (!error_a)
	{
		error_a = deserialize (stream_a);
	}
}

void scendere::bulk_pull_account::visit (scendere::message_visitor & visitor_a) const
{
	visitor_a.bulk_pull_account (*this);
}

void scendere::bulk_pull_account::serialize (scendere::stream & stream_a) const
{
	header.serialize (stream_a);
	write (stream_a, account);
	write (stream_a, minimum_amount);
	write (stream_a, flags);
}

bool scendere::bulk_pull_account::deserialize (scendere::stream & stream_a)
{
	debug_assert (header.type == scendere::message_type::bulk_pull_account);
	auto error (false);
	try
	{
		scendere::read (stream_a, account);
		scendere::read (stream_a, minimum_amount);
		scendere::read (stream_a, flags);
	}
	catch (std::runtime_error const &)
	{
		error = true;
	}

	return error;
}

scendere::bulk_push::bulk_push (scendere::network_constants const & constants) :
	message (constants, scendere::message_type::bulk_push)
{
}

scendere::bulk_push::bulk_push (scendere::message_header const & header_a) :
	message (header_a)
{
}

bool scendere::bulk_push::deserialize (scendere::stream & stream_a)
{
	debug_assert (header.type == scendere::message_type::bulk_push);
	return false;
}

void scendere::bulk_push::serialize (scendere::stream & stream_a) const
{
	header.serialize (stream_a);
}

void scendere::bulk_push::visit (scendere::message_visitor & visitor_a) const
{
	visitor_a.bulk_push (*this);
}

scendere::telemetry_req::telemetry_req (scendere::network_constants const & constants) :
	message (constants, scendere::message_type::telemetry_req)
{
}

scendere::telemetry_req::telemetry_req (scendere::message_header const & header_a) :
	message (header_a)
{
}

bool scendere::telemetry_req::deserialize (scendere::stream & stream_a)
{
	debug_assert (header.type == scendere::message_type::telemetry_req);
	return false;
}

void scendere::telemetry_req::serialize (scendere::stream & stream_a) const
{
	header.serialize (stream_a);
}

void scendere::telemetry_req::visit (scendere::message_visitor & visitor_a) const
{
	visitor_a.telemetry_req (*this);
}

scendere::telemetry_ack::telemetry_ack (scendere::network_constants const & constants) :
	message (constants, scendere::message_type::telemetry_ack)
{
}

scendere::telemetry_ack::telemetry_ack (bool & error_a, scendere::stream & stream_a, scendere::message_header const & message_header) :
	message (message_header)
{
	if (!error_a)
	{
		error_a = deserialize (stream_a);
	}
}

scendere::telemetry_ack::telemetry_ack (scendere::network_constants const & constants, scendere::telemetry_data const & telemetry_data_a) :
	message (constants, scendere::message_type::telemetry_ack),
	data (telemetry_data_a)
{
	debug_assert (telemetry_data::size + telemetry_data_a.unknown_data.size () <= message_header::telemetry_size_mask.to_ulong ()); // Maximum size the mask allows
	header.extensions &= ~message_header::telemetry_size_mask;
	header.extensions |= std::bitset<16> (static_cast<unsigned long long> (telemetry_data::size) + telemetry_data_a.unknown_data.size ());
}

void scendere::telemetry_ack::serialize (scendere::stream & stream_a) const
{
	header.serialize (stream_a);
	if (!is_empty_payload ())
	{
		data.serialize (stream_a);
	}
}

bool scendere::telemetry_ack::deserialize (scendere::stream & stream_a)
{
	auto error (false);
	debug_assert (header.type == scendere::message_type::telemetry_ack);
	try
	{
		if (!is_empty_payload ())
		{
			data.deserialize (stream_a, scendere::narrow_cast<uint16_t> (header.extensions.to_ulong ()));
		}
	}
	catch (std::runtime_error const &)
	{
		error = true;
	}

	return error;
}

void scendere::telemetry_ack::visit (scendere::message_visitor & visitor_a) const
{
	visitor_a.telemetry_ack (*this);
}

uint16_t scendere::telemetry_ack::size () const
{
	return size (header);
}

uint16_t scendere::telemetry_ack::size (scendere::message_header const & message_header_a)
{
	return static_cast<uint16_t> ((message_header_a.extensions & message_header::telemetry_size_mask).to_ullong ());
}

bool scendere::telemetry_ack::is_empty_payload () const
{
	return size () == 0;
}

void scendere::telemetry_data::deserialize (scendere::stream & stream_a, uint16_t payload_length_a)
{
	read (stream_a, signature);
	read (stream_a, node_id);
	read (stream_a, block_count);
	boost::endian::big_to_native_inplace (block_count);
	read (stream_a, cemented_count);
	boost::endian::big_to_native_inplace (cemented_count);
	read (stream_a, unchecked_count);
	boost::endian::big_to_native_inplace (unchecked_count);
	read (stream_a, account_count);
	boost::endian::big_to_native_inplace (account_count);
	read (stream_a, bandwidth_cap);
	boost::endian::big_to_native_inplace (bandwidth_cap);
	read (stream_a, peer_count);
	boost::endian::big_to_native_inplace (peer_count);
	read (stream_a, protocol_version);
	read (stream_a, uptime);
	boost::endian::big_to_native_inplace (uptime);
	read (stream_a, genesis_block.bytes);
	read (stream_a, major_version);
	read (stream_a, minor_version);
	read (stream_a, patch_version);
	read (stream_a, pre_release_version);
	read (stream_a, maker);

	uint64_t timestamp_l;
	read (stream_a, timestamp_l);
	boost::endian::big_to_native_inplace (timestamp_l);
	timestamp = std::chrono::system_clock::time_point (std::chrono::milliseconds (timestamp_l));
	read (stream_a, active_difficulty);
	boost::endian::big_to_native_inplace (active_difficulty);
	if (payload_length_a > latest_size)
	{
		read (stream_a, unknown_data, payload_length_a - latest_size);
	}
}

void scendere::telemetry_data::serialize_without_signature (scendere::stream & stream_a) const
{
	// All values should be serialized in big endian
	write (stream_a, node_id);
	write (stream_a, boost::endian::native_to_big (block_count));
	write (stream_a, boost::endian::native_to_big (cemented_count));
	write (stream_a, boost::endian::native_to_big (unchecked_count));
	write (stream_a, boost::endian::native_to_big (account_count));
	write (stream_a, boost::endian::native_to_big (bandwidth_cap));
	write (stream_a, boost::endian::native_to_big (peer_count));
	write (stream_a, protocol_version);
	write (stream_a, boost::endian::native_to_big (uptime));
	write (stream_a, genesis_block.bytes);
	write (stream_a, major_version);
	write (stream_a, minor_version);
	write (stream_a, patch_version);
	write (stream_a, pre_release_version);
	write (stream_a, maker);
	write (stream_a, boost::endian::native_to_big (std::chrono::duration_cast<std::chrono::milliseconds> (timestamp.time_since_epoch ()).count ()));
	write (stream_a, boost::endian::native_to_big (active_difficulty));
	write (stream_a, unknown_data);
}

void scendere::telemetry_data::serialize (scendere::stream & stream_a) const
{
	write (stream_a, signature);
	serialize_without_signature (stream_a);
}

scendere::error scendere::telemetry_data::serialize_json (scendere::jsonconfig & json, bool ignore_identification_metrics_a) const
{
	json.put ("block_count", block_count);
	json.put ("cemented_count", cemented_count);
	json.put ("unchecked_count", unchecked_count);
	json.put ("account_count", account_count);
	json.put ("bandwidth_cap", bandwidth_cap);
	json.put ("peer_count", peer_count);
	json.put ("protocol_version", protocol_version);
	json.put ("uptime", uptime);
	json.put ("genesis_block", genesis_block.to_string ());
	json.put ("major_version", major_version);
	json.put ("minor_version", minor_version);
	json.put ("patch_version", patch_version);
	json.put ("pre_release_version", pre_release_version);
	json.put ("maker", maker);
	json.put ("timestamp", std::chrono::duration_cast<std::chrono::milliseconds> (timestamp.time_since_epoch ()).count ());
	json.put ("active_difficulty", scendere::to_string_hex (active_difficulty));
	// Keep these last for UI purposes
	if (!ignore_identification_metrics_a)
	{
		json.put ("node_id", node_id.to_node_id ());
		json.put ("signature", signature.to_string ());
	}
	return json.get_error ();
}

scendere::error scendere::telemetry_data::deserialize_json (scendere::jsonconfig & json, bool ignore_identification_metrics_a)
{
	if (!ignore_identification_metrics_a)
	{
		std::string signature_l;
		json.get ("signature", signature_l);
		if (!json.get_error ())
		{
			if (signature.decode_hex (signature_l))
			{
				json.get_error ().set ("Could not deserialize signature");
			}
		}

		std::string node_id_l;
		json.get ("node_id", node_id_l);
		if (!json.get_error ())
		{
			if (node_id.decode_node_id (node_id_l))
			{
				json.get_error ().set ("Could not deserialize node id");
			}
		}
	}

	json.get ("block_count", block_count);
	json.get ("cemented_count", cemented_count);
	json.get ("unchecked_count", unchecked_count);
	json.get ("account_count", account_count);
	json.get ("bandwidth_cap", bandwidth_cap);
	json.get ("peer_count", peer_count);
	json.get ("protocol_version", protocol_version);
	json.get ("uptime", uptime);
	std::string genesis_block_l;
	json.get ("genesis_block", genesis_block_l);
	if (!json.get_error ())
	{
		if (genesis_block.decode_hex (genesis_block_l))
		{
			json.get_error ().set ("Could not deserialize genesis block");
		}
	}
	json.get ("major_version", major_version);
	json.get ("minor_version", minor_version);
	json.get ("patch_version", patch_version);
	json.get ("pre_release_version", pre_release_version);
	json.get ("maker", maker);
	auto timestamp_l = json.get<uint64_t> ("timestamp");
	timestamp = std::chrono::system_clock::time_point (std::chrono::milliseconds (timestamp_l));
	auto current_active_difficulty_text = json.get<std::string> ("active_difficulty");
	auto ec = scendere::from_string_hex (current_active_difficulty_text, active_difficulty);
	debug_assert (!ec);
	return json.get_error ();
}

std::string scendere::telemetry_data::to_string () const
{
	scendere::jsonconfig jc;
	serialize_json (jc, true);
	std::stringstream ss;
	jc.write (ss);
	return ss.str ();
}

bool scendere::telemetry_data::operator== (scendere::telemetry_data const & data_a) const
{
	return (signature == data_a.signature && node_id == data_a.node_id && block_count == data_a.block_count && cemented_count == data_a.cemented_count && unchecked_count == data_a.unchecked_count && account_count == data_a.account_count && bandwidth_cap == data_a.bandwidth_cap && uptime == data_a.uptime && peer_count == data_a.peer_count && protocol_version == data_a.protocol_version && genesis_block == data_a.genesis_block && major_version == data_a.major_version && minor_version == data_a.minor_version && patch_version == data_a.patch_version && pre_release_version == data_a.pre_release_version && maker == data_a.maker && timestamp == data_a.timestamp && active_difficulty == data_a.active_difficulty && unknown_data == data_a.unknown_data);
}

bool scendere::telemetry_data::operator!= (scendere::telemetry_data const & data_a) const
{
	return !(*this == data_a);
}

void scendere::telemetry_data::sign (scendere::keypair const & node_id_a)
{
	debug_assert (node_id == node_id_a.pub);
	std::vector<uint8_t> bytes;
	{
		scendere::vectorstream stream (bytes);
		serialize_without_signature (stream);
	}

	signature = scendere::sign_message (node_id_a.prv, node_id_a.pub, bytes.data (), bytes.size ());
}

bool scendere::telemetry_data::validate_signature () const
{
	std::vector<uint8_t> bytes;
	{
		scendere::vectorstream stream (bytes);
		serialize_without_signature (stream);
	}

	return scendere::validate_message (node_id, bytes.data (), bytes.size (), signature);
}

scendere::node_id_handshake::node_id_handshake (bool & error_a, scendere::stream & stream_a, scendere::message_header const & header_a) :
	message (header_a),
	query (boost::none),
	response (boost::none)
{
	error_a = deserialize (stream_a);
}

scendere::node_id_handshake::node_id_handshake (scendere::network_constants const & constants, boost::optional<scendere::uint256_union> query, boost::optional<std::pair<scendere::account, scendere::signature>> response) :
	message (constants, scendere::message_type::node_id_handshake),
	query (query),
	response (response)
{
	if (query)
	{
		header.flag_set (scendere::message_header::node_id_handshake_query_flag);
	}
	if (response)
	{
		header.flag_set (scendere::message_header::node_id_handshake_response_flag);
	}
}

void scendere::node_id_handshake::serialize (scendere::stream & stream_a) const
{
	header.serialize (stream_a);
	if (query)
	{
		write (stream_a, *query);
	}
	if (response)
	{
		write (stream_a, response->first);
		write (stream_a, response->second);
	}
}

bool scendere::node_id_handshake::deserialize (scendere::stream & stream_a)
{
	debug_assert (header.type == scendere::message_type::node_id_handshake);
	auto error (false);
	try
	{
		if (header.node_id_handshake_is_query ())
		{
			scendere::uint256_union query_hash;
			read (stream_a, query_hash);
			query = query_hash;
		}

		if (header.node_id_handshake_is_response ())
		{
			scendere::account response_account;
			read (stream_a, response_account);
			scendere::signature response_signature;
			read (stream_a, response_signature);
			response = std::make_pair (response_account, response_signature);
		}
	}
	catch (std::runtime_error const &)
	{
		error = true;
	}

	return error;
}

bool scendere::node_id_handshake::operator== (scendere::node_id_handshake const & other_a) const
{
	auto result (*query == *other_a.query && *response == *other_a.response);
	return result;
}

void scendere::node_id_handshake::visit (scendere::message_visitor & visitor_a) const
{
	visitor_a.node_id_handshake (*this);
}

std::size_t scendere::node_id_handshake::size () const
{
	return size (header);
}

std::size_t scendere::node_id_handshake::size (scendere::message_header const & header_a)
{
	std::size_t result (0);
	if (header_a.node_id_handshake_is_query ())
	{
		result = sizeof (scendere::uint256_union);
	}
	if (header_a.node_id_handshake_is_response ())
	{
		result += sizeof (scendere::account) + sizeof (scendere::signature);
	}
	return result;
}

scendere::message_visitor::~message_visitor ()
{
}

bool scendere::parse_port (std::string const & string_a, uint16_t & port_a)
{
	bool result = false;
	try
	{
		port_a = boost::lexical_cast<uint16_t> (string_a);
	}
	catch (...)
	{
		result = true;
	}
	return result;
}

// Can handle both ipv4 & ipv6 addresses (with and without square brackets)
bool scendere::parse_address (std::string const & address_text_a, boost::asio::ip::address & address_a)
{
	auto address_text = address_text_a;
	if (!address_text.empty () && address_text.front () == '[' && address_text.back () == ']')
	{
		// Chop the square brackets off as make_address doesn't always like them
		address_text = address_text.substr (1, address_text.size () - 2);
	}

	boost::system::error_code address_ec;
	address_a = boost::asio::ip::make_address (address_text, address_ec);
	return !!address_ec;
}

bool scendere::parse_address_port (std::string const & string, boost::asio::ip::address & address_a, uint16_t & port_a)
{
	auto result (false);
	auto port_position (string.rfind (':'));
	if (port_position != std::string::npos && port_position > 0)
	{
		std::string port_string (string.substr (port_position + 1));
		try
		{
			uint16_t port;
			result = parse_port (port_string, port);
			if (!result)
			{
				boost::system::error_code ec;
				auto address (boost::asio::ip::make_address_v6 (string.substr (0, port_position), ec));
				if (!ec)
				{
					address_a = address;
					port_a = port;
				}
				else
				{
					result = true;
				}
			}
			else
			{
				result = true;
			}
		}
		catch (...)
		{
			result = true;
		}
	}
	else
	{
		result = true;
	}
	return result;
}

bool scendere::parse_endpoint (std::string const & string, scendere::endpoint & endpoint_a)
{
	boost::asio::ip::address address;
	uint16_t port;
	auto result (parse_address_port (string, address, port));
	if (!result)
	{
		endpoint_a = scendere::endpoint (address, port);
	}
	return result;
}

bool scendere::parse_tcp_endpoint (std::string const & string, scendere::tcp_endpoint & endpoint_a)
{
	boost::asio::ip::address address;
	uint16_t port;
	auto result (parse_address_port (string, address, port));
	if (!result)
	{
		endpoint_a = scendere::tcp_endpoint (address, port);
	}
	return result;
}

std::chrono::seconds scendere::telemetry_cache_cutoffs::network_to_time (network_constants const & network_constants)
{
	return std::chrono::seconds{ (network_constants.is_live_network () || network_constants.is_test_network ()) ? live : network_constants.is_beta_network () ? beta
																																							  : dev };
}

scendere::node_singleton_memory_pool_purge_guard::node_singleton_memory_pool_purge_guard () :
	cleanup_guard ({ scendere::block_memory_pool_purge, scendere::purge_shared_ptr_singleton_pool_memory<scendere::vote>, scendere::purge_shared_ptr_singleton_pool_memory<scendere::election>, scendere::purge_singleton_inactive_votes_cache_pool_memory })
{
}
