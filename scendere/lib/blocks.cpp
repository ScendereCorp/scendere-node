#include <scendere/crypto_lib/random_pool.hpp>
#include <scendere/lib/blocks.hpp>
#include <scendere/lib/memory.hpp>
#include <scendere/lib/numbers.hpp>
#include <scendere/lib/threading.hpp>
#include <scendere/secure/common.hpp>

#include <crypto/cryptopp/words.h>

#include <boost/endian/conversion.hpp>
#include <boost/property_tree/json_parser.hpp>

#include <bitset>

/** Compare blocks, first by type, then content. This is an optimization over dynamic_cast, which is very slow on some platforms. */
namespace
{
template <typename T>
bool blocks_equal (T const & first, scendere::block const & second)
{
	static_assert (std::is_base_of<scendere::block, T>::value, "Input parameter is not a block type");
	return (first.type () == second.type ()) && (static_cast<T const &> (second)) == first;
}

template <typename block>
std::shared_ptr<block> deserialize_block (scendere::stream & stream_a)
{
	auto error (false);
	auto result = scendere::make_shared<block> (error, stream_a);
	if (error)
	{
		result = nullptr;
	}

	return result;
}
}

void scendere::block_memory_pool_purge ()
{
	scendere::purge_shared_ptr_singleton_pool_memory<scendere::open_block> ();
	scendere::purge_shared_ptr_singleton_pool_memory<scendere::state_block> ();
	scendere::purge_shared_ptr_singleton_pool_memory<scendere::send_block> ();
	scendere::purge_shared_ptr_singleton_pool_memory<scendere::change_block> ();
}

std::string scendere::block::to_json () const
{
	std::string result;
	serialize_json (result);
	return result;
}

size_t scendere::block::size (scendere::block_type type_a)
{
	size_t result (0);
	switch (type_a)
	{
		case scendere::block_type::invalid:
		case scendere::block_type::not_a_block:
			debug_assert (false);
			break;
		case scendere::block_type::send:
			result = scendere::send_block::size;
			break;
		case scendere::block_type::receive:
			result = scendere::receive_block::size;
			break;
		case scendere::block_type::change:
			result = scendere::change_block::size;
			break;
		case scendere::block_type::open:
			result = scendere::open_block::size;
			break;
		case scendere::block_type::state:
			result = scendere::state_block::size;
			break;
	}
	return result;
}

scendere::work_version scendere::block::work_version () const
{
	return scendere::work_version::work_1;
}

scendere::block_hash scendere::block::generate_hash () const
{
	scendere::block_hash result;
	blake2b_state hash_l;
	auto status (blake2b_init (&hash_l, sizeof (result.bytes)));
	debug_assert (status == 0);
	hash (hash_l);
	status = blake2b_final (&hash_l, result.bytes.data (), sizeof (result.bytes));
	debug_assert (status == 0);
	return result;
}

void scendere::block::refresh ()
{
	if (!cached_hash.is_zero ())
	{
		cached_hash = generate_hash ();
	}
}

scendere::block_hash const & scendere::block::hash () const
{
	if (!cached_hash.is_zero ())
	{
		// Once a block is created, it should not be modified (unless using refresh ())
		// This would invalidate the cache; check it hasn't changed.
		debug_assert (cached_hash == generate_hash ());
	}
	else
	{
		cached_hash = generate_hash ();
	}

	return cached_hash;
}

scendere::block_hash scendere::block::full_hash () const
{
	scendere::block_hash result;
	blake2b_state state;
	blake2b_init (&state, sizeof (result.bytes));
	blake2b_update (&state, hash ().bytes.data (), sizeof (hash ()));
	auto signature (block_signature ());
	blake2b_update (&state, signature.bytes.data (), sizeof (signature));
	auto work (block_work ());
	blake2b_update (&state, &work, sizeof (work));
	blake2b_final (&state, result.bytes.data (), sizeof (result.bytes));
	return result;
}

scendere::block_sideband const & scendere::block::sideband () const
{
	debug_assert (sideband_m.is_initialized ());
	return *sideband_m;
}

void scendere::block::sideband_set (scendere::block_sideband const & sideband_a)
{
	sideband_m = sideband_a;
}

bool scendere::block::has_sideband () const
{
	return sideband_m.is_initialized ();
}

scendere::account const & scendere::block::representative () const
{
	static scendere::account representative{};
	return representative;
}

scendere::block_hash const & scendere::block::source () const
{
	static scendere::block_hash source{ 0 };
	return source;
}

scendere::account const & scendere::block::destination () const
{
	static scendere::account destination{};
	return destination;
}

scendere::link const & scendere::block::link () const
{
	static scendere::link link{ 0 };
	return link;
}

scendere::account const & scendere::block::account () const
{
	static scendere::account account{};
	return account;
}

scendere::qualified_root scendere::block::qualified_root () const
{
	return scendere::qualified_root (root (), previous ());
}

scendere::amount const & scendere::block::balance () const
{
	static scendere::amount amount{ 0 };
	return amount;
}

void scendere::send_block::visit (scendere::block_visitor & visitor_a) const
{
	visitor_a.send_block (*this);
}

void scendere::send_block::visit (scendere::mutable_block_visitor & visitor_a)
{
	visitor_a.send_block (*this);
}

void scendere::send_block::hash (blake2b_state & hash_a) const
{
	hashables.hash (hash_a);
}

uint64_t scendere::send_block::block_work () const
{
	return work;
}

void scendere::send_block::block_work_set (uint64_t work_a)
{
	work = work_a;
}

scendere::send_hashables::send_hashables (scendere::block_hash const & previous_a, scendere::account const & destination_a, scendere::amount const & balance_a) :
	previous (previous_a),
	destination (destination_a),
	balance (balance_a)
{
}

scendere::send_hashables::send_hashables (bool & error_a, scendere::stream & stream_a)
{
	try
	{
		scendere::read (stream_a, previous.bytes);
		scendere::read (stream_a, destination.bytes);
		scendere::read (stream_a, balance.bytes);
	}
	catch (std::runtime_error const &)
	{
		error_a = true;
	}
}

scendere::send_hashables::send_hashables (bool & error_a, boost::property_tree::ptree const & tree_a)
{
	try
	{
		auto previous_l (tree_a.get<std::string> ("previous"));
		auto destination_l (tree_a.get<std::string> ("destination"));
		auto balance_l (tree_a.get<std::string> ("balance"));
		error_a = previous.decode_hex (previous_l);
		if (!error_a)
		{
			error_a = destination.decode_account (destination_l);
			if (!error_a)
			{
				error_a = balance.decode_hex (balance_l);
			}
		}
	}
	catch (std::runtime_error const &)
	{
		error_a = true;
	}
}

void scendere::send_hashables::hash (blake2b_state & hash_a) const
{
	auto status (blake2b_update (&hash_a, previous.bytes.data (), sizeof (previous.bytes)));
	debug_assert (status == 0);
	status = blake2b_update (&hash_a, destination.bytes.data (), sizeof (destination.bytes));
	debug_assert (status == 0);
	status = blake2b_update (&hash_a, balance.bytes.data (), sizeof (balance.bytes));
	debug_assert (status == 0);
}

void scendere::send_block::serialize (scendere::stream & stream_a) const
{
	write (stream_a, hashables.previous.bytes);
	write (stream_a, hashables.destination.bytes);
	write (stream_a, hashables.balance.bytes);
	write (stream_a, signature.bytes);
	write (stream_a, work);
}

bool scendere::send_block::deserialize (scendere::stream & stream_a)
{
	auto error (false);
	try
	{
		read (stream_a, hashables.previous.bytes);
		read (stream_a, hashables.destination.bytes);
		read (stream_a, hashables.balance.bytes);
		read (stream_a, signature.bytes);
		read (stream_a, work);
	}
	catch (std::exception const &)
	{
		error = true;
	}

	return error;
}

void scendere::send_block::serialize_json (std::string & string_a, bool single_line) const
{
	boost::property_tree::ptree tree;
	serialize_json (tree);
	std::stringstream ostream;
	boost::property_tree::write_json (ostream, tree, !single_line);
	string_a = ostream.str ();
}

void scendere::send_block::serialize_json (boost::property_tree::ptree & tree) const
{
	tree.put ("type", "send");
	std::string previous;
	hashables.previous.encode_hex (previous);
	tree.put ("previous", previous);
	tree.put ("destination", hashables.destination.to_account ());
	std::string balance;
	hashables.balance.encode_hex (balance);
	tree.put ("balance", balance);
	std::string signature_l;
	signature.encode_hex (signature_l);
	tree.put ("work", scendere::to_string_hex (work));
	tree.put ("signature", signature_l);
}

bool scendere::send_block::deserialize_json (boost::property_tree::ptree const & tree_a)
{
	auto error (false);
	try
	{
		debug_assert (tree_a.get<std::string> ("type") == "send");
		auto previous_l (tree_a.get<std::string> ("previous"));
		auto destination_l (tree_a.get<std::string> ("destination"));
		auto balance_l (tree_a.get<std::string> ("balance"));
		auto work_l (tree_a.get<std::string> ("work"));
		auto signature_l (tree_a.get<std::string> ("signature"));
		error = hashables.previous.decode_hex (previous_l);
		if (!error)
		{
			error = hashables.destination.decode_account (destination_l);
			if (!error)
			{
				error = hashables.balance.decode_hex (balance_l);
				if (!error)
				{
					error = scendere::from_string_hex (work_l, work);
					if (!error)
					{
						error = signature.decode_hex (signature_l);
					}
				}
			}
		}
	}
	catch (std::runtime_error const &)
	{
		error = true;
	}
	return error;
}

scendere::send_block::send_block (scendere::block_hash const & previous_a, scendere::account const & destination_a, scendere::amount const & balance_a, scendere::raw_key const & prv_a, scendere::public_key const & pub_a, uint64_t work_a) :
	hashables (previous_a, destination_a, balance_a),
	signature (scendere::sign_message (prv_a, pub_a, hash ())),
	work (work_a)
{
	debug_assert (destination_a != nullptr);
	debug_assert (pub_a != nullptr);
}

scendere::send_block::send_block (bool & error_a, scendere::stream & stream_a) :
	hashables (error_a, stream_a)
{
	if (!error_a)
	{
		try
		{
			scendere::read (stream_a, signature.bytes);
			scendere::read (stream_a, work);
		}
		catch (std::runtime_error const &)
		{
			error_a = true;
		}
	}
}

scendere::send_block::send_block (bool & error_a, boost::property_tree::ptree const & tree_a) :
	hashables (error_a, tree_a)
{
	if (!error_a)
	{
		try
		{
			auto signature_l (tree_a.get<std::string> ("signature"));
			auto work_l (tree_a.get<std::string> ("work"));
			error_a = signature.decode_hex (signature_l);
			if (!error_a)
			{
				error_a = scendere::from_string_hex (work_l, work);
			}
		}
		catch (std::runtime_error const &)
		{
			error_a = true;
		}
	}
}

bool scendere::send_block::operator== (scendere::block const & other_a) const
{
	return blocks_equal (*this, other_a);
}

bool scendere::send_block::valid_predecessor (scendere::block const & block_a) const
{
	bool result;
	switch (block_a.type ())
	{
		case scendere::block_type::send:
		case scendere::block_type::receive:
		case scendere::block_type::open:
		case scendere::block_type::change:
			result = true;
			break;
		default:
			result = false;
			break;
	}
	return result;
}

scendere::block_type scendere::send_block::type () const
{
	return scendere::block_type::send;
}

bool scendere::send_block::operator== (scendere::send_block const & other_a) const
{
	auto result (hashables.destination == other_a.hashables.destination && hashables.previous == other_a.hashables.previous && hashables.balance == other_a.hashables.balance && work == other_a.work && signature == other_a.signature);
	return result;
}

scendere::block_hash const & scendere::send_block::previous () const
{
	return hashables.previous;
}

scendere::account const & scendere::send_block::destination () const
{
	return hashables.destination;
}

scendere::root const & scendere::send_block::root () const
{
	return hashables.previous;
}

scendere::amount const & scendere::send_block::balance () const
{
	return hashables.balance;
}

scendere::signature const & scendere::send_block::block_signature () const
{
	return signature;
}

void scendere::send_block::signature_set (scendere::signature const & signature_a)
{
	signature = signature_a;
}

scendere::open_hashables::open_hashables (scendere::block_hash const & source_a, scendere::account const & representative_a, scendere::account const & account_a) :
	source (source_a),
	representative (representative_a),
	account (account_a)
{
}

scendere::open_hashables::open_hashables (bool & error_a, scendere::stream & stream_a)
{
	try
	{
		scendere::read (stream_a, source.bytes);
		scendere::read (stream_a, representative.bytes);
		scendere::read (stream_a, account.bytes);
	}
	catch (std::runtime_error const &)
	{
		error_a = true;
	}
}

scendere::open_hashables::open_hashables (bool & error_a, boost::property_tree::ptree const & tree_a)
{
	try
	{
		auto source_l (tree_a.get<std::string> ("source"));
		auto representative_l (tree_a.get<std::string> ("representative"));
		auto account_l (tree_a.get<std::string> ("account"));
		error_a = source.decode_hex (source_l);
		if (!error_a)
		{
			error_a = representative.decode_account (representative_l);
			if (!error_a)
			{
				error_a = account.decode_account (account_l);
			}
		}
	}
	catch (std::runtime_error const &)
	{
		error_a = true;
	}
}

void scendere::open_hashables::hash (blake2b_state & hash_a) const
{
	blake2b_update (&hash_a, source.bytes.data (), sizeof (source.bytes));
	blake2b_update (&hash_a, representative.bytes.data (), sizeof (representative.bytes));
	blake2b_update (&hash_a, account.bytes.data (), sizeof (account.bytes));
}

scendere::open_block::open_block (scendere::block_hash const & source_a, scendere::account const & representative_a, scendere::account const & account_a, scendere::raw_key const & prv_a, scendere::public_key const & pub_a, uint64_t work_a) :
	hashables (source_a, representative_a, account_a),
	signature (scendere::sign_message (prv_a, pub_a, hash ())),
	work (work_a)
{
	debug_assert (representative_a != nullptr);
	debug_assert (account_a != nullptr);
	debug_assert (pub_a != nullptr);
}

scendere::open_block::open_block (scendere::block_hash const & source_a, scendere::account const & representative_a, scendere::account const & account_a, std::nullptr_t) :
	hashables (source_a, representative_a, account_a),
	work (0)
{
	debug_assert (representative_a != nullptr);
	debug_assert (account_a != nullptr);

	signature.clear ();
}

scendere::open_block::open_block (bool & error_a, scendere::stream & stream_a) :
	hashables (error_a, stream_a)
{
	if (!error_a)
	{
		try
		{
			scendere::read (stream_a, signature);
			scendere::read (stream_a, work);
		}
		catch (std::runtime_error const &)
		{
			error_a = true;
		}
	}
}

scendere::open_block::open_block (bool & error_a, boost::property_tree::ptree const & tree_a) :
	hashables (error_a, tree_a)
{
	if (!error_a)
	{
		try
		{
			auto work_l (tree_a.get<std::string> ("work"));
			auto signature_l (tree_a.get<std::string> ("signature"));
			error_a = scendere::from_string_hex (work_l, work);
			if (!error_a)
			{
				error_a = signature.decode_hex (signature_l);
			}
		}
		catch (std::runtime_error const &)
		{
			error_a = true;
		}
	}
}

void scendere::open_block::hash (blake2b_state & hash_a) const
{
	hashables.hash (hash_a);
}

uint64_t scendere::open_block::block_work () const
{
	return work;
}

void scendere::open_block::block_work_set (uint64_t work_a)
{
	work = work_a;
}

scendere::block_hash const & scendere::open_block::previous () const
{
	static scendere::block_hash result{ 0 };
	return result;
}

scendere::account const & scendere::open_block::account () const
{
	return hashables.account;
}

void scendere::open_block::serialize (scendere::stream & stream_a) const
{
	write (stream_a, hashables.source);
	write (stream_a, hashables.representative);
	write (stream_a, hashables.account);
	write (stream_a, signature);
	write (stream_a, work);
}

bool scendere::open_block::deserialize (scendere::stream & stream_a)
{
	auto error (false);
	try
	{
		read (stream_a, hashables.source);
		read (stream_a, hashables.representative);
		read (stream_a, hashables.account);
		read (stream_a, signature);
		read (stream_a, work);
	}
	catch (std::runtime_error const &)
	{
		error = true;
	}

	return error;
}

void scendere::open_block::serialize_json (std::string & string_a, bool single_line) const
{
	boost::property_tree::ptree tree;
	serialize_json (tree);
	std::stringstream ostream;
	boost::property_tree::write_json (ostream, tree, !single_line);
	string_a = ostream.str ();
}

void scendere::open_block::serialize_json (boost::property_tree::ptree & tree) const
{
	tree.put ("type", "open");
	tree.put ("source", hashables.source.to_string ());
	tree.put ("representative", representative ().to_account ());
	tree.put ("account", hashables.account.to_account ());
	std::string signature_l;
	signature.encode_hex (signature_l);
	tree.put ("work", scendere::to_string_hex (work));
	tree.put ("signature", signature_l);
}

bool scendere::open_block::deserialize_json (boost::property_tree::ptree const & tree_a)
{
	auto error (false);
	try
	{
		debug_assert (tree_a.get<std::string> ("type") == "open");
		auto source_l (tree_a.get<std::string> ("source"));
		auto representative_l (tree_a.get<std::string> ("representative"));
		auto account_l (tree_a.get<std::string> ("account"));
		auto work_l (tree_a.get<std::string> ("work"));
		auto signature_l (tree_a.get<std::string> ("signature"));
		error = hashables.source.decode_hex (source_l);
		if (!error)
		{
			error = hashables.representative.decode_hex (representative_l);
			if (!error)
			{
				error = hashables.account.decode_hex (account_l);
				if (!error)
				{
					error = scendere::from_string_hex (work_l, work);
					if (!error)
					{
						error = signature.decode_hex (signature_l);
					}
				}
			}
		}
	}
	catch (std::runtime_error const &)
	{
		error = true;
	}
	return error;
}

void scendere::open_block::visit (scendere::block_visitor & visitor_a) const
{
	visitor_a.open_block (*this);
}

void scendere::open_block::visit (scendere::mutable_block_visitor & visitor_a)
{
	visitor_a.open_block (*this);
}

scendere::block_type scendere::open_block::type () const
{
	return scendere::block_type::open;
}

bool scendere::open_block::operator== (scendere::block const & other_a) const
{
	return blocks_equal (*this, other_a);
}

bool scendere::open_block::operator== (scendere::open_block const & other_a) const
{
	return hashables.source == other_a.hashables.source && hashables.representative == other_a.hashables.representative && hashables.account == other_a.hashables.account && work == other_a.work && signature == other_a.signature;
}

bool scendere::open_block::valid_predecessor (scendere::block const & block_a) const
{
	return false;
}

scendere::block_hash const & scendere::open_block::source () const
{
	return hashables.source;
}

scendere::root const & scendere::open_block::root () const
{
	return hashables.account;
}

scendere::account const & scendere::open_block::representative () const
{
	return hashables.representative;
}

scendere::signature const & scendere::open_block::block_signature () const
{
	return signature;
}

void scendere::open_block::signature_set (scendere::signature const & signature_a)
{
	signature = signature_a;
}

scendere::change_hashables::change_hashables (scendere::block_hash const & previous_a, scendere::account const & representative_a) :
	previous (previous_a),
	representative (representative_a)
{
}

scendere::change_hashables::change_hashables (bool & error_a, scendere::stream & stream_a)
{
	try
	{
		scendere::read (stream_a, previous);
		scendere::read (stream_a, representative);
	}
	catch (std::runtime_error const &)
	{
		error_a = true;
	}
}

scendere::change_hashables::change_hashables (bool & error_a, boost::property_tree::ptree const & tree_a)
{
	try
	{
		auto previous_l (tree_a.get<std::string> ("previous"));
		auto representative_l (tree_a.get<std::string> ("representative"));
		error_a = previous.decode_hex (previous_l);
		if (!error_a)
		{
			error_a = representative.decode_account (representative_l);
		}
	}
	catch (std::runtime_error const &)
	{
		error_a = true;
	}
}

void scendere::change_hashables::hash (blake2b_state & hash_a) const
{
	blake2b_update (&hash_a, previous.bytes.data (), sizeof (previous.bytes));
	blake2b_update (&hash_a, representative.bytes.data (), sizeof (representative.bytes));
}

scendere::change_block::change_block (scendere::block_hash const & previous_a, scendere::account const & representative_a, scendere::raw_key const & prv_a, scendere::public_key const & pub_a, uint64_t work_a) :
	hashables (previous_a, representative_a),
	signature (scendere::sign_message (prv_a, pub_a, hash ())),
	work (work_a)
{
	debug_assert (representative_a != nullptr);
	debug_assert (pub_a != nullptr);
}

scendere::change_block::change_block (bool & error_a, scendere::stream & stream_a) :
	hashables (error_a, stream_a)
{
	if (!error_a)
	{
		try
		{
			scendere::read (stream_a, signature);
			scendere::read (stream_a, work);
		}
		catch (std::runtime_error const &)
		{
			error_a = true;
		}
	}
}

scendere::change_block::change_block (bool & error_a, boost::property_tree::ptree const & tree_a) :
	hashables (error_a, tree_a)
{
	if (!error_a)
	{
		try
		{
			auto work_l (tree_a.get<std::string> ("work"));
			auto signature_l (tree_a.get<std::string> ("signature"));
			error_a = scendere::from_string_hex (work_l, work);
			if (!error_a)
			{
				error_a = signature.decode_hex (signature_l);
			}
		}
		catch (std::runtime_error const &)
		{
			error_a = true;
		}
	}
}

void scendere::change_block::hash (blake2b_state & hash_a) const
{
	hashables.hash (hash_a);
}

uint64_t scendere::change_block::block_work () const
{
	return work;
}

void scendere::change_block::block_work_set (uint64_t work_a)
{
	work = work_a;
}

scendere::block_hash const & scendere::change_block::previous () const
{
	return hashables.previous;
}

void scendere::change_block::serialize (scendere::stream & stream_a) const
{
	write (stream_a, hashables.previous);
	write (stream_a, hashables.representative);
	write (stream_a, signature);
	write (stream_a, work);
}

bool scendere::change_block::deserialize (scendere::stream & stream_a)
{
	auto error (false);
	try
	{
		read (stream_a, hashables.previous);
		read (stream_a, hashables.representative);
		read (stream_a, signature);
		read (stream_a, work);
	}
	catch (std::runtime_error const &)
	{
		error = true;
	}

	return error;
}

void scendere::change_block::serialize_json (std::string & string_a, bool single_line) const
{
	boost::property_tree::ptree tree;
	serialize_json (tree);
	std::stringstream ostream;
	boost::property_tree::write_json (ostream, tree, !single_line);
	string_a = ostream.str ();
}

void scendere::change_block::serialize_json (boost::property_tree::ptree & tree) const
{
	tree.put ("type", "change");
	tree.put ("previous", hashables.previous.to_string ());
	tree.put ("representative", representative ().to_account ());
	tree.put ("work", scendere::to_string_hex (work));
	std::string signature_l;
	signature.encode_hex (signature_l);
	tree.put ("signature", signature_l);
}

bool scendere::change_block::deserialize_json (boost::property_tree::ptree const & tree_a)
{
	auto error (false);
	try
	{
		debug_assert (tree_a.get<std::string> ("type") == "change");
		auto previous_l (tree_a.get<std::string> ("previous"));
		auto representative_l (tree_a.get<std::string> ("representative"));
		auto work_l (tree_a.get<std::string> ("work"));
		auto signature_l (tree_a.get<std::string> ("signature"));
		error = hashables.previous.decode_hex (previous_l);
		if (!error)
		{
			error = hashables.representative.decode_hex (representative_l);
			if (!error)
			{
				error = scendere::from_string_hex (work_l, work);
				if (!error)
				{
					error = signature.decode_hex (signature_l);
				}
			}
		}
	}
	catch (std::runtime_error const &)
	{
		error = true;
	}
	return error;
}

void scendere::change_block::visit (scendere::block_visitor & visitor_a) const
{
	visitor_a.change_block (*this);
}

void scendere::change_block::visit (scendere::mutable_block_visitor & visitor_a)
{
	visitor_a.change_block (*this);
}

scendere::block_type scendere::change_block::type () const
{
	return scendere::block_type::change;
}

bool scendere::change_block::operator== (scendere::block const & other_a) const
{
	return blocks_equal (*this, other_a);
}

bool scendere::change_block::operator== (scendere::change_block const & other_a) const
{
	return hashables.previous == other_a.hashables.previous && hashables.representative == other_a.hashables.representative && work == other_a.work && signature == other_a.signature;
}

bool scendere::change_block::valid_predecessor (scendere::block const & block_a) const
{
	bool result;
	switch (block_a.type ())
	{
		case scendere::block_type::send:
		case scendere::block_type::receive:
		case scendere::block_type::open:
		case scendere::block_type::change:
			result = true;
			break;
		default:
			result = false;
			break;
	}
	return result;
}

scendere::root const & scendere::change_block::root () const
{
	return hashables.previous;
}

scendere::account const & scendere::change_block::representative () const
{
	return hashables.representative;
}

scendere::signature const & scendere::change_block::block_signature () const
{
	return signature;
}

void scendere::change_block::signature_set (scendere::signature const & signature_a)
{
	signature = signature_a;
}

scendere::state_hashables::state_hashables (scendere::account const & account_a, scendere::block_hash const & previous_a, scendere::account const & representative_a, scendere::amount const & balance_a, scendere::link const & link_a) :
	account (account_a),
	previous (previous_a),
	representative (representative_a),
	balance (balance_a),
	link (link_a)
{
}

scendere::state_hashables::state_hashables (bool & error_a, scendere::stream & stream_a)
{
	try
	{
		scendere::read (stream_a, account);
		scendere::read (stream_a, previous);
		scendere::read (stream_a, representative);
		scendere::read (stream_a, balance);
		scendere::read (stream_a, link);
	}
	catch (std::runtime_error const &)
	{
		error_a = true;
	}
}

scendere::state_hashables::state_hashables (bool & error_a, boost::property_tree::ptree const & tree_a)
{
	try
	{
		auto account_l (tree_a.get<std::string> ("account"));
		auto previous_l (tree_a.get<std::string> ("previous"));
		auto representative_l (tree_a.get<std::string> ("representative"));
		auto balance_l (tree_a.get<std::string> ("balance"));
		auto link_l (tree_a.get<std::string> ("link"));
		error_a = account.decode_account (account_l);
		if (!error_a)
		{
			error_a = previous.decode_hex (previous_l);
			if (!error_a)
			{
				error_a = representative.decode_account (representative_l);
				if (!error_a)
				{
					error_a = balance.decode_dec (balance_l);
					if (!error_a)
					{
						error_a = link.decode_account (link_l) && link.decode_hex (link_l);
					}
				}
			}
		}
	}
	catch (std::runtime_error const &)
	{
		error_a = true;
	}
}

void scendere::state_hashables::hash (blake2b_state & hash_a) const
{
	blake2b_update (&hash_a, account.bytes.data (), sizeof (account.bytes));
	blake2b_update (&hash_a, previous.bytes.data (), sizeof (previous.bytes));
	blake2b_update (&hash_a, representative.bytes.data (), sizeof (representative.bytes));
	blake2b_update (&hash_a, balance.bytes.data (), sizeof (balance.bytes));
	blake2b_update (&hash_a, link.bytes.data (), sizeof (link.bytes));
}

scendere::state_block::state_block (scendere::account const & account_a, scendere::block_hash const & previous_a, scendere::account const & representative_a, scendere::amount const & balance_a, scendere::link const & link_a, scendere::raw_key const & prv_a, scendere::public_key const & pub_a, uint64_t work_a) :
	hashables (account_a, previous_a, representative_a, balance_a, link_a),
	signature (scendere::sign_message (prv_a, pub_a, hash ())),
	work (work_a)
{
	debug_assert (account_a != nullptr);
	debug_assert (representative_a != nullptr);
	debug_assert (link_a.as_account () != nullptr);
	debug_assert (pub_a != nullptr);
}

scendere::state_block::state_block (bool & error_a, scendere::stream & stream_a) :
	hashables (error_a, stream_a)
{
	if (!error_a)
	{
		try
		{
			scendere::read (stream_a, signature);
			scendere::read (stream_a, work);
			boost::endian::big_to_native_inplace (work);
		}
		catch (std::runtime_error const &)
		{
			error_a = true;
		}
	}
}

scendere::state_block::state_block (bool & error_a, boost::property_tree::ptree const & tree_a) :
	hashables (error_a, tree_a)
{
	if (!error_a)
	{
		try
		{
			auto type_l (tree_a.get<std::string> ("type"));
			auto signature_l (tree_a.get<std::string> ("signature"));
			auto work_l (tree_a.get<std::string> ("work"));
			error_a = type_l != "state";
			if (!error_a)
			{
				error_a = scendere::from_string_hex (work_l, work);
				if (!error_a)
				{
					error_a = signature.decode_hex (signature_l);
				}
			}
		}
		catch (std::runtime_error const &)
		{
			error_a = true;
		}
	}
}

void scendere::state_block::hash (blake2b_state & hash_a) const
{
	scendere::uint256_union preamble (static_cast<uint64_t> (scendere::block_type::state));
	blake2b_update (&hash_a, preamble.bytes.data (), preamble.bytes.size ());
	hashables.hash (hash_a);
}

uint64_t scendere::state_block::block_work () const
{
	return work;
}

void scendere::state_block::block_work_set (uint64_t work_a)
{
	work = work_a;
}

scendere::block_hash const & scendere::state_block::previous () const
{
	return hashables.previous;
}

scendere::account const & scendere::state_block::account () const
{
	return hashables.account;
}

void scendere::state_block::serialize (scendere::stream & stream_a) const
{
	write (stream_a, hashables.account);
	write (stream_a, hashables.previous);
	write (stream_a, hashables.representative);
	write (stream_a, hashables.balance);
	write (stream_a, hashables.link);
	write (stream_a, signature);
	write (stream_a, boost::endian::native_to_big (work));
}

bool scendere::state_block::deserialize (scendere::stream & stream_a)
{
	auto error (false);
	try
	{
		read (stream_a, hashables.account);
		read (stream_a, hashables.previous);
		read (stream_a, hashables.representative);
		read (stream_a, hashables.balance);
		read (stream_a, hashables.link);
		read (stream_a, signature);
		read (stream_a, work);
		boost::endian::big_to_native_inplace (work);
	}
	catch (std::runtime_error const &)
	{
		error = true;
	}

	return error;
}

void scendere::state_block::serialize_json (std::string & string_a, bool single_line) const
{
	boost::property_tree::ptree tree;
	serialize_json (tree);
	std::stringstream ostream;
	boost::property_tree::write_json (ostream, tree, !single_line);
	string_a = ostream.str ();
}

void scendere::state_block::serialize_json (boost::property_tree::ptree & tree) const
{
	tree.put ("type", "state");
	tree.put ("account", hashables.account.to_account ());
	tree.put ("previous", hashables.previous.to_string ());
	tree.put ("representative", representative ().to_account ());
	tree.put ("balance", hashables.balance.to_string_dec ());
	tree.put ("link", hashables.link.to_string ());
	tree.put ("link_as_account", hashables.link.to_account ());
	std::string signature_l;
	signature.encode_hex (signature_l);
	tree.put ("signature", signature_l);
	tree.put ("work", scendere::to_string_hex (work));
}

bool scendere::state_block::deserialize_json (boost::property_tree::ptree const & tree_a)
{
	auto error (false);
	try
	{
		debug_assert (tree_a.get<std::string> ("type") == "state");
		auto account_l (tree_a.get<std::string> ("account"));
		auto previous_l (tree_a.get<std::string> ("previous"));
		auto representative_l (tree_a.get<std::string> ("representative"));
		auto balance_l (tree_a.get<std::string> ("balance"));
		auto link_l (tree_a.get<std::string> ("link"));
		auto work_l (tree_a.get<std::string> ("work"));
		auto signature_l (tree_a.get<std::string> ("signature"));
		error = hashables.account.decode_account (account_l);
		if (!error)
		{
			error = hashables.previous.decode_hex (previous_l);
			if (!error)
			{
				error = hashables.representative.decode_account (representative_l);
				if (!error)
				{
					error = hashables.balance.decode_dec (balance_l);
					if (!error)
					{
						error = hashables.link.decode_account (link_l) && hashables.link.decode_hex (link_l);
						if (!error)
						{
							error = scendere::from_string_hex (work_l, work);
							if (!error)
							{
								error = signature.decode_hex (signature_l);
							}
						}
					}
				}
			}
		}
	}
	catch (std::runtime_error const &)
	{
		error = true;
	}
	return error;
}

void scendere::state_block::visit (scendere::block_visitor & visitor_a) const
{
	visitor_a.state_block (*this);
}

void scendere::state_block::visit (scendere::mutable_block_visitor & visitor_a)
{
	visitor_a.state_block (*this);
}

scendere::block_type scendere::state_block::type () const
{
	return scendere::block_type::state;
}

bool scendere::state_block::operator== (scendere::block const & other_a) const
{
	return blocks_equal (*this, other_a);
}

bool scendere::state_block::operator== (scendere::state_block const & other_a) const
{
	return hashables.account == other_a.hashables.account && hashables.previous == other_a.hashables.previous && hashables.representative == other_a.hashables.representative && hashables.balance == other_a.hashables.balance && hashables.link == other_a.hashables.link && signature == other_a.signature && work == other_a.work;
}

bool scendere::state_block::valid_predecessor (scendere::block const & block_a) const
{
	return true;
}

scendere::root const & scendere::state_block::root () const
{
	if (!hashables.previous.is_zero ())
	{
		return hashables.previous;
	}
	else
	{
		return hashables.account;
	}
}

scendere::link const & scendere::state_block::link () const
{
	return hashables.link;
}

scendere::account const & scendere::state_block::representative () const
{
	return hashables.representative;
}

scendere::amount const & scendere::state_block::balance () const
{
	return hashables.balance;
}

scendere::signature const & scendere::state_block::block_signature () const
{
	return signature;
}

void scendere::state_block::signature_set (scendere::signature const & signature_a)
{
	signature = signature_a;
}

std::shared_ptr<scendere::block> scendere::deserialize_block_json (boost::property_tree::ptree const & tree_a, scendere::block_uniquer * uniquer_a)
{
	std::shared_ptr<scendere::block> result;
	try
	{
		auto type (tree_a.get<std::string> ("type"));
		bool error (false);
		std::unique_ptr<scendere::block> obj;
		if (type == "receive")
		{
			obj = std::make_unique<scendere::receive_block> (error, tree_a);
		}
		else if (type == "send")
		{
			obj = std::make_unique<scendere::send_block> (error, tree_a);
		}
		else if (type == "open")
		{
			obj = std::make_unique<scendere::open_block> (error, tree_a);
		}
		else if (type == "change")
		{
			obj = std::make_unique<scendere::change_block> (error, tree_a);
		}
		else if (type == "state")
		{
			obj = std::make_unique<scendere::state_block> (error, tree_a);
		}

		if (!error)
		{
			result = std::move (obj);
		}
	}
	catch (std::runtime_error const &)
	{
	}
	if (uniquer_a != nullptr)
	{
		result = uniquer_a->unique (result);
	}
	return result;
}

std::shared_ptr<scendere::block> scendere::deserialize_block (scendere::stream & stream_a)
{
	scendere::block_type type;
	auto error (try_read (stream_a, type));
	std::shared_ptr<scendere::block> result;
	if (!error)
	{
		result = scendere::deserialize_block (stream_a, type);
	}
	return result;
}

std::shared_ptr<scendere::block> scendere::deserialize_block (scendere::stream & stream_a, scendere::block_type type_a, scendere::block_uniquer * uniquer_a)
{
	std::shared_ptr<scendere::block> result;
	switch (type_a)
	{
		case scendere::block_type::receive:
		{
			result = ::deserialize_block<scendere::receive_block> (stream_a);
			break;
		}
		case scendere::block_type::send:
		{
			result = ::deserialize_block<scendere::send_block> (stream_a);
			break;
		}
		case scendere::block_type::open:
		{
			result = ::deserialize_block<scendere::open_block> (stream_a);
			break;
		}
		case scendere::block_type::change:
		{
			result = ::deserialize_block<scendere::change_block> (stream_a);
			break;
		}
		case scendere::block_type::state:
		{
			result = ::deserialize_block<scendere::state_block> (stream_a);
			break;
		}
		default:
#ifndef SCENDERE_FUZZER_TEST
			debug_assert (false);
#endif
			break;
	}
	if (uniquer_a != nullptr)
	{
		result = uniquer_a->unique (result);
	}
	return result;
}

void scendere::receive_block::visit (scendere::block_visitor & visitor_a) const
{
	visitor_a.receive_block (*this);
}

void scendere::receive_block::visit (scendere::mutable_block_visitor & visitor_a)
{
	visitor_a.receive_block (*this);
}

bool scendere::receive_block::operator== (scendere::receive_block const & other_a) const
{
	auto result (hashables.previous == other_a.hashables.previous && hashables.source == other_a.hashables.source && work == other_a.work && signature == other_a.signature);
	return result;
}

void scendere::receive_block::serialize (scendere::stream & stream_a) const
{
	write (stream_a, hashables.previous.bytes);
	write (stream_a, hashables.source.bytes);
	write (stream_a, signature.bytes);
	write (stream_a, work);
}

bool scendere::receive_block::deserialize (scendere::stream & stream_a)
{
	auto error (false);
	try
	{
		read (stream_a, hashables.previous.bytes);
		read (stream_a, hashables.source.bytes);
		read (stream_a, signature.bytes);
		read (stream_a, work);
	}
	catch (std::runtime_error const &)
	{
		error = true;
	}

	return error;
}

void scendere::receive_block::serialize_json (std::string & string_a, bool single_line) const
{
	boost::property_tree::ptree tree;
	serialize_json (tree);
	std::stringstream ostream;
	boost::property_tree::write_json (ostream, tree, !single_line);
	string_a = ostream.str ();
}

void scendere::receive_block::serialize_json (boost::property_tree::ptree & tree) const
{
	tree.put ("type", "receive");
	std::string previous;
	hashables.previous.encode_hex (previous);
	tree.put ("previous", previous);
	std::string source;
	hashables.source.encode_hex (source);
	tree.put ("source", source);
	std::string signature_l;
	signature.encode_hex (signature_l);
	tree.put ("work", scendere::to_string_hex (work));
	tree.put ("signature", signature_l);
}

bool scendere::receive_block::deserialize_json (boost::property_tree::ptree const & tree_a)
{
	auto error (false);
	try
	{
		debug_assert (tree_a.get<std::string> ("type") == "receive");
		auto previous_l (tree_a.get<std::string> ("previous"));
		auto source_l (tree_a.get<std::string> ("source"));
		auto work_l (tree_a.get<std::string> ("work"));
		auto signature_l (tree_a.get<std::string> ("signature"));
		error = hashables.previous.decode_hex (previous_l);
		if (!error)
		{
			error = hashables.source.decode_hex (source_l);
			if (!error)
			{
				error = scendere::from_string_hex (work_l, work);
				if (!error)
				{
					error = signature.decode_hex (signature_l);
				}
			}
		}
	}
	catch (std::runtime_error const &)
	{
		error = true;
	}
	return error;
}

scendere::receive_block::receive_block (scendere::block_hash const & previous_a, scendere::block_hash const & source_a, scendere::raw_key const & prv_a, scendere::public_key const & pub_a, uint64_t work_a) :
	hashables (previous_a, source_a),
	signature (scendere::sign_message (prv_a, pub_a, hash ())),
	work (work_a)
{
	debug_assert (pub_a != nullptr);
}

scendere::receive_block::receive_block (bool & error_a, scendere::stream & stream_a) :
	hashables (error_a, stream_a)
{
	if (!error_a)
	{
		try
		{
			scendere::read (stream_a, signature);
			scendere::read (stream_a, work);
		}
		catch (std::runtime_error const &)
		{
			error_a = true;
		}
	}
}

scendere::receive_block::receive_block (bool & error_a, boost::property_tree::ptree const & tree_a) :
	hashables (error_a, tree_a)
{
	if (!error_a)
	{
		try
		{
			auto signature_l (tree_a.get<std::string> ("signature"));
			auto work_l (tree_a.get<std::string> ("work"));
			error_a = signature.decode_hex (signature_l);
			if (!error_a)
			{
				error_a = scendere::from_string_hex (work_l, work);
			}
		}
		catch (std::runtime_error const &)
		{
			error_a = true;
		}
	}
}

void scendere::receive_block::hash (blake2b_state & hash_a) const
{
	hashables.hash (hash_a);
}

uint64_t scendere::receive_block::block_work () const
{
	return work;
}

void scendere::receive_block::block_work_set (uint64_t work_a)
{
	work = work_a;
}

bool scendere::receive_block::operator== (scendere::block const & other_a) const
{
	return blocks_equal (*this, other_a);
}

bool scendere::receive_block::valid_predecessor (scendere::block const & block_a) const
{
	bool result;
	switch (block_a.type ())
	{
		case scendere::block_type::send:
		case scendere::block_type::receive:
		case scendere::block_type::open:
		case scendere::block_type::change:
			result = true;
			break;
		default:
			result = false;
			break;
	}
	return result;
}

scendere::block_hash const & scendere::receive_block::previous () const
{
	return hashables.previous;
}

scendere::block_hash const & scendere::receive_block::source () const
{
	return hashables.source;
}

scendere::root const & scendere::receive_block::root () const
{
	return hashables.previous;
}

scendere::signature const & scendere::receive_block::block_signature () const
{
	return signature;
}

void scendere::receive_block::signature_set (scendere::signature const & signature_a)
{
	signature = signature_a;
}

scendere::block_type scendere::receive_block::type () const
{
	return scendere::block_type::receive;
}

scendere::receive_hashables::receive_hashables (scendere::block_hash const & previous_a, scendere::block_hash const & source_a) :
	previous (previous_a),
	source (source_a)
{
}

scendere::receive_hashables::receive_hashables (bool & error_a, scendere::stream & stream_a)
{
	try
	{
		scendere::read (stream_a, previous.bytes);
		scendere::read (stream_a, source.bytes);
	}
	catch (std::runtime_error const &)
	{
		error_a = true;
	}
}

scendere::receive_hashables::receive_hashables (bool & error_a, boost::property_tree::ptree const & tree_a)
{
	try
	{
		auto previous_l (tree_a.get<std::string> ("previous"));
		auto source_l (tree_a.get<std::string> ("source"));
		error_a = previous.decode_hex (previous_l);
		if (!error_a)
		{
			error_a = source.decode_hex (source_l);
		}
	}
	catch (std::runtime_error const &)
	{
		error_a = true;
	}
}

void scendere::receive_hashables::hash (blake2b_state & hash_a) const
{
	blake2b_update (&hash_a, previous.bytes.data (), sizeof (previous.bytes));
	blake2b_update (&hash_a, source.bytes.data (), sizeof (source.bytes));
}

scendere::block_details::block_details (scendere::epoch const epoch_a, bool const is_send_a, bool const is_receive_a, bool const is_epoch_a) :
	epoch (epoch_a), is_send (is_send_a), is_receive (is_receive_a), is_epoch (is_epoch_a)
{
}

bool scendere::block_details::operator== (scendere::block_details const & other_a) const
{
	return epoch == other_a.epoch && is_send == other_a.is_send && is_receive == other_a.is_receive && is_epoch == other_a.is_epoch;
}

uint8_t scendere::block_details::packed () const
{
	std::bitset<8> result (static_cast<uint8_t> (epoch));
	result.set (7, is_send);
	result.set (6, is_receive);
	result.set (5, is_epoch);
	return static_cast<uint8_t> (result.to_ulong ());
}

void scendere::block_details::unpack (uint8_t details_a)
{
	constexpr std::bitset<8> epoch_mask{ 0b00011111 };
	auto as_bitset = static_cast<std::bitset<8>> (details_a);
	is_send = as_bitset.test (7);
	is_receive = as_bitset.test (6);
	is_epoch = as_bitset.test (5);
	epoch = static_cast<scendere::epoch> ((as_bitset & epoch_mask).to_ulong ());
}

void scendere::block_details::serialize (scendere::stream & stream_a) const
{
	scendere::write (stream_a, packed ());
}

bool scendere::block_details::deserialize (scendere::stream & stream_a)
{
	bool result (false);
	try
	{
		uint8_t packed{ 0 };
		scendere::read (stream_a, packed);
		unpack (packed);
	}
	catch (std::runtime_error &)
	{
		result = true;
	}

	return result;
}

std::string scendere::state_subtype (scendere::block_details const details_a)
{
	debug_assert (details_a.is_epoch + details_a.is_receive + details_a.is_send <= 1);
	if (details_a.is_send)
	{
		return "send";
	}
	else if (details_a.is_receive)
	{
		return "receive";
	}
	else if (details_a.is_epoch)
	{
		return "epoch";
	}
	else
	{
		return "change";
	}
}

scendere::block_sideband::block_sideband (scendere::account const & account_a, scendere::block_hash const & successor_a, scendere::amount const & balance_a, uint64_t const height_a, uint64_t const timestamp_a, scendere::block_details const & details_a, scendere::epoch const source_epoch_a) :
	successor (successor_a),
	account (account_a),
	balance (balance_a),
	height (height_a),
	timestamp (timestamp_a),
	details (details_a),
	source_epoch (source_epoch_a)
{
}

scendere::block_sideband::block_sideband (scendere::account const & account_a, scendere::block_hash const & successor_a, scendere::amount const & balance_a, uint64_t const height_a, uint64_t const timestamp_a, scendere::epoch const epoch_a, bool const is_send, bool const is_receive, bool const is_epoch, scendere::epoch const source_epoch_a) :
	successor (successor_a),
	account (account_a),
	balance (balance_a),
	height (height_a),
	timestamp (timestamp_a),
	details (epoch_a, is_send, is_receive, is_epoch),
	source_epoch (source_epoch_a)
{
}

size_t scendere::block_sideband::size (scendere::block_type type_a)
{
	size_t result (0);
	result += sizeof (successor);
	if (type_a != scendere::block_type::state && type_a != scendere::block_type::open)
	{
		result += sizeof (account);
	}
	if (type_a != scendere::block_type::open)
	{
		result += sizeof (height);
	}
	if (type_a == scendere::block_type::receive || type_a == scendere::block_type::change || type_a == scendere::block_type::open)
	{
		result += sizeof (balance);
	}
	result += sizeof (timestamp);
	if (type_a == scendere::block_type::state)
	{
		static_assert (sizeof (scendere::epoch) == scendere::block_details::size (), "block_details is larger than the epoch enum");
		result += scendere::block_details::size () + sizeof (scendere::epoch);
	}
	return result;
}

void scendere::block_sideband::serialize (scendere::stream & stream_a, scendere::block_type type_a) const
{
	scendere::write (stream_a, successor.bytes);
	if (type_a != scendere::block_type::state && type_a != scendere::block_type::open)
	{
		scendere::write (stream_a, account.bytes);
	}
	if (type_a != scendere::block_type::open)
	{
		scendere::write (stream_a, boost::endian::native_to_big (height));
	}
	if (type_a == scendere::block_type::receive || type_a == scendere::block_type::change || type_a == scendere::block_type::open)
	{
		scendere::write (stream_a, balance.bytes);
	}
	scendere::write (stream_a, boost::endian::native_to_big (timestamp));
	if (type_a == scendere::block_type::state)
	{
		details.serialize (stream_a);
		scendere::write (stream_a, static_cast<uint8_t> (source_epoch));
	}
}

bool scendere::block_sideband::deserialize (scendere::stream & stream_a, scendere::block_type type_a)
{
	bool result (false);
	try
	{
		scendere::read (stream_a, successor.bytes);
		if (type_a != scendere::block_type::state && type_a != scendere::block_type::open)
		{
			scendere::read (stream_a, account.bytes);
		}
		if (type_a != scendere::block_type::open)
		{
			scendere::read (stream_a, height);
			boost::endian::big_to_native_inplace (height);
		}
		else
		{
			height = 1;
		}
		if (type_a == scendere::block_type::receive || type_a == scendere::block_type::change || type_a == scendere::block_type::open)
		{
			scendere::read (stream_a, balance.bytes);
		}
		scendere::read (stream_a, timestamp);
		boost::endian::big_to_native_inplace (timestamp);
		if (type_a == scendere::block_type::state)
		{
			result = details.deserialize (stream_a);
			uint8_t source_epoch_uint8_t{ 0 };
			scendere::read (stream_a, source_epoch_uint8_t);
			source_epoch = static_cast<scendere::epoch> (source_epoch_uint8_t);
		}
	}
	catch (std::runtime_error &)
	{
		result = true;
	}

	return result;
}

std::shared_ptr<scendere::block> scendere::block_uniquer::unique (std::shared_ptr<scendere::block> const & block_a)
{
	auto result (block_a);
	if (result != nullptr)
	{
		scendere::uint256_union key (block_a->full_hash ());
		scendere::lock_guard<scendere::mutex> lock (mutex);
		auto & existing (blocks[key]);
		if (auto block_l = existing.lock ())
		{
			result = block_l;
		}
		else
		{
			existing = block_a;
		}
		release_assert (std::numeric_limits<CryptoPP::word32>::max () > blocks.size ());
		for (auto i (0); i < cleanup_count && !blocks.empty (); ++i)
		{
			auto random_offset (scendere::random_pool::generate_word32 (0, static_cast<CryptoPP::word32> (blocks.size () - 1)));
			auto existing (std::next (blocks.begin (), random_offset));
			if (existing == blocks.end ())
			{
				existing = blocks.begin ();
			}
			if (existing != blocks.end ())
			{
				if (auto block_l = existing->second.lock ())
				{
					// Still live
				}
				else
				{
					blocks.erase (existing);
				}
			}
		}
	}
	return result;
}

size_t scendere::block_uniquer::size ()
{
	scendere::lock_guard<scendere::mutex> lock (mutex);
	return blocks.size ();
}

std::unique_ptr<scendere::container_info_component> scendere::collect_container_info (block_uniquer & block_uniquer, std::string const & name)
{
	auto count = block_uniquer.size ();
	auto sizeof_element = sizeof (block_uniquer::value_type);
	auto composite = std::make_unique<container_info_composite> (name);
	composite->add_component (std::make_unique<container_info_leaf> (container_info{ "blocks", count, sizeof_element }));
	return composite;
}
