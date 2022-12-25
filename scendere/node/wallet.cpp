#include <scendere/crypto_lib/random_pool.hpp>
#include <scendere/lib/threading.hpp>
#include <scendere/lib/utility.hpp>
#include <scendere/node/election.hpp>
#include <scendere/node/lmdb/lmdb_iterator.hpp>
#include <scendere/node/node.hpp>
#include <scendere/node/wallet.hpp>

#include <boost/filesystem.hpp>
#include <boost/format.hpp>
#include <boost/polymorphic_cast.hpp>
#include <boost/property_tree/json_parser.hpp>

#include <future>

#include <argon2.h>

scendere::uint256_union scendere::wallet_store::check (scendere::transaction const & transaction_a)
{
	scendere::wallet_value value (entry_get_raw (transaction_a, scendere::wallet_store::check_special));
	return value.key;
}

scendere::uint256_union scendere::wallet_store::salt (scendere::transaction const & transaction_a)
{
	scendere::wallet_value value (entry_get_raw (transaction_a, scendere::wallet_store::salt_special));
	return value.key;
}

void scendere::wallet_store::wallet_key (scendere::raw_key & prv_a, scendere::transaction const & transaction_a)
{
	scendere::lock_guard<std::recursive_mutex> lock (mutex);
	scendere::raw_key wallet_l;
	wallet_key_mem.value (wallet_l);
	scendere::raw_key password_l;
	password.value (password_l);
	prv_a.decrypt (wallet_l, password_l, salt (transaction_a).owords[0]);
}

void scendere::wallet_store::seed (scendere::raw_key & prv_a, scendere::transaction const & transaction_a)
{
	scendere::wallet_value value (entry_get_raw (transaction_a, scendere::wallet_store::seed_special));
	scendere::raw_key password_l;
	wallet_key (password_l, transaction_a);
	prv_a.decrypt (value.key, password_l, salt (transaction_a).owords[seed_iv_index]);
}

void scendere::wallet_store::seed_set (scendere::transaction const & transaction_a, scendere::raw_key const & prv_a)
{
	scendere::raw_key password_l;
	wallet_key (password_l, transaction_a);
	scendere::raw_key ciphertext;
	ciphertext.encrypt (prv_a, password_l, salt (transaction_a).owords[seed_iv_index]);
	entry_put_raw (transaction_a, scendere::wallet_store::seed_special, scendere::wallet_value (ciphertext, 0));
	deterministic_clear (transaction_a);
}

scendere::public_key scendere::wallet_store::deterministic_insert (scendere::transaction const & transaction_a)
{
	auto index (deterministic_index_get (transaction_a));
	auto prv = deterministic_key (transaction_a, index);
	scendere::public_key result (scendere::pub_key (prv));
	while (exists (transaction_a, result))
	{
		++index;
		prv = deterministic_key (transaction_a, index);
		result = scendere::pub_key (prv);
	}
	uint64_t marker (1);
	marker <<= 32;
	marker |= index;
	entry_put_raw (transaction_a, result, scendere::wallet_value (marker, 0));
	++index;
	deterministic_index_set (transaction_a, index);
	return result;
}

scendere::public_key scendere::wallet_store::deterministic_insert (scendere::transaction const & transaction_a, uint32_t const index)
{
	auto prv = deterministic_key (transaction_a, index);
	scendere::public_key result (scendere::pub_key (prv));
	uint64_t marker (1);
	marker <<= 32;
	marker |= index;
	entry_put_raw (transaction_a, result, scendere::wallet_value (marker, 0));
	return result;
}

scendere::raw_key scendere::wallet_store::deterministic_key (scendere::transaction const & transaction_a, uint32_t index_a)
{
	debug_assert (valid_password (transaction_a));
	scendere::raw_key seed_l;
	seed (seed_l, transaction_a);
	return scendere::deterministic_key (seed_l, index_a);
}

uint32_t scendere::wallet_store::deterministic_index_get (scendere::transaction const & transaction_a)
{
	scendere::wallet_value value (entry_get_raw (transaction_a, scendere::wallet_store::deterministic_index_special));
	return static_cast<uint32_t> (value.key.number () & static_cast<uint32_t> (-1));
}

void scendere::wallet_store::deterministic_index_set (scendere::transaction const & transaction_a, uint32_t index_a)
{
	scendere::raw_key index_l (index_a);
	scendere::wallet_value value (index_l, 0);
	entry_put_raw (transaction_a, scendere::wallet_store::deterministic_index_special, value);
}

void scendere::wallet_store::deterministic_clear (scendere::transaction const & transaction_a)
{
	scendere::uint256_union key (0);
	for (auto i (begin (transaction_a)), n (end ()); i != n;)
	{
		switch (key_type (scendere::wallet_value (i->second)))
		{
			case scendere::key_type::deterministic:
			{
				auto const & key (i->first);
				erase (transaction_a, key);
				i = begin (transaction_a, key);
				break;
			}
			default:
			{
				++i;
				break;
			}
		}
	}
	deterministic_index_set (transaction_a, 0);
}

bool scendere::wallet_store::valid_password (scendere::transaction const & transaction_a)
{
	scendere::raw_key zero;
	zero.clear ();
	scendere::raw_key wallet_key_l;
	wallet_key (wallet_key_l, transaction_a);
	scendere::uint256_union check_l;
	check_l.encrypt (zero, wallet_key_l, salt (transaction_a).owords[check_iv_index]);
	bool ok = check (transaction_a) == check_l;
	return ok;
}

bool scendere::wallet_store::attempt_password (scendere::transaction const & transaction_a, std::string const & password_a)
{
	bool result = false;
	{
		scendere::lock_guard<std::recursive_mutex> lock (mutex);
		scendere::raw_key password_l;
		derive_key (password_l, transaction_a, password_a);
		password.value_set (password_l);
		result = !valid_password (transaction_a);
	}
	if (!result)
	{
		switch (version (transaction_a))
		{
			case version_4:
				break;
			default:
				debug_assert (false);
		}
	}
	return result;
}

bool scendere::wallet_store::rekey (scendere::transaction const & transaction_a, std::string const & password_a)
{
	scendere::lock_guard<std::recursive_mutex> lock (mutex);
	bool result (false);
	if (valid_password (transaction_a))
	{
		scendere::raw_key password_new;
		derive_key (password_new, transaction_a, password_a);
		scendere::raw_key wallet_key_l;
		wallet_key (wallet_key_l, transaction_a);
		scendere::raw_key password_l;
		password.value (password_l);
		password.value_set (password_new);
		scendere::raw_key encrypted;
		encrypted.encrypt (wallet_key_l, password_new, salt (transaction_a).owords[0]);
		scendere::raw_key wallet_enc;
		wallet_enc = encrypted;
		wallet_key_mem.value_set (wallet_enc);
		entry_put_raw (transaction_a, scendere::wallet_store::wallet_key_special, scendere::wallet_value (encrypted, 0));
	}
	else
	{
		result = true;
	}
	return result;
}

void scendere::wallet_store::derive_key (scendere::raw_key & prv_a, scendere::transaction const & transaction_a, std::string const & password_a)
{
	auto salt_l (salt (transaction_a));
	kdf.phs (prv_a, password_a, salt_l);
}

scendere::fan::fan (scendere::raw_key const & key, std::size_t count_a)
{
	auto first (std::make_unique<scendere::raw_key> (key));
	for (auto i (1); i < count_a; ++i)
	{
		auto entry (std::make_unique<scendere::raw_key> ());
		scendere::random_pool::generate_block (entry->bytes.data (), entry->bytes.size ());
		*first ^= *entry;
		values.push_back (std::move (entry));
	}
	values.push_back (std::move (first));
}

void scendere::fan::value (scendere::raw_key & prv_a)
{
	scendere::lock_guard<scendere::mutex> lock (mutex);
	value_get (prv_a);
}

void scendere::fan::value_get (scendere::raw_key & prv_a)
{
	debug_assert (!mutex.try_lock ());
	prv_a.clear ();
	for (auto & i : values)
	{
		prv_a ^= *i;
	}
}

void scendere::fan::value_set (scendere::raw_key const & value_a)
{
	scendere::lock_guard<scendere::mutex> lock (mutex);
	scendere::raw_key value_l;
	value_get (value_l);
	*(values[0]) ^= value_l;
	*(values[0]) ^= value_a;
}

// Wallet version number
scendere::account const scendere::wallet_store::version_special{};
// Random number used to salt private key encryption
scendere::account const scendere::wallet_store::salt_special (1);
// Key used to encrypt wallet keys, encrypted itself by the user password
scendere::account const scendere::wallet_store::wallet_key_special (2);
// Check value used to see if password is valid
scendere::account const scendere::wallet_store::check_special (3);
// Representative account to be used if we open a new account
scendere::account const scendere::wallet_store::representative_special (4);
// Wallet seed for deterministic key generation
scendere::account const scendere::wallet_store::seed_special (5);
// Current key index for deterministic keys
scendere::account const scendere::wallet_store::deterministic_index_special (6);
int const scendere::wallet_store::special_count (7);
std::size_t const scendere::wallet_store::check_iv_index (0);
std::size_t const scendere::wallet_store::seed_iv_index (1);

scendere::wallet_store::wallet_store (bool & init_a, scendere::kdf & kdf_a, scendere::transaction & transaction_a, scendere::account representative_a, unsigned fanout_a, std::string const & wallet_a, std::string const & json_a) :
	password (0, fanout_a),
	wallet_key_mem (0, fanout_a),
	kdf (kdf_a)
{
	init_a = false;
	initialize (transaction_a, init_a, wallet_a);
	if (!init_a)
	{
		MDB_val junk;
		debug_assert (mdb_get (tx (transaction_a), handle, scendere::mdb_val (version_special), &junk) == MDB_NOTFOUND);
		boost::property_tree::ptree wallet_l;
		std::stringstream istream (json_a);
		try
		{
			boost::property_tree::read_json (istream, wallet_l);
		}
		catch (...)
		{
			init_a = true;
		}
		for (auto i (wallet_l.begin ()), n (wallet_l.end ()); i != n; ++i)
		{
			scendere::account key;
			init_a = key.decode_hex (i->first);
			if (!init_a)
			{
				scendere::raw_key value;
				init_a = value.decode_hex (wallet_l.get<std::string> (i->first));
				if (!init_a)
				{
					entry_put_raw (transaction_a, key, scendere::wallet_value (value, 0));
				}
				else
				{
					init_a = true;
				}
			}
			else
			{
				init_a = true;
			}
		}
		init_a |= mdb_get (tx (transaction_a), handle, scendere::mdb_val (version_special), &junk) != 0;
		init_a |= mdb_get (tx (transaction_a), handle, scendere::mdb_val (wallet_key_special), &junk) != 0;
		init_a |= mdb_get (tx (transaction_a), handle, scendere::mdb_val (salt_special), &junk) != 0;
		init_a |= mdb_get (tx (transaction_a), handle, scendere::mdb_val (check_special), &junk) != 0;
		init_a |= mdb_get (tx (transaction_a), handle, scendere::mdb_val (representative_special), &junk) != 0;
		scendere::raw_key key;
		key.clear ();
		password.value_set (key);
		key = entry_get_raw (transaction_a, scendere::wallet_store::wallet_key_special).key;
		wallet_key_mem.value_set (key);
	}
}

scendere::wallet_store::wallet_store (bool & init_a, scendere::kdf & kdf_a, scendere::transaction & transaction_a, scendere::account representative_a, unsigned fanout_a, std::string const & wallet_a) :
	password (0, fanout_a),
	wallet_key_mem (0, fanout_a),
	kdf (kdf_a)
{
	init_a = false;
	initialize (transaction_a, init_a, wallet_a);
	if (!init_a)
	{
		int version_status;
		MDB_val version_value;
		version_status = mdb_get (tx (transaction_a), handle, scendere::mdb_val (version_special), &version_value);
		if (version_status == MDB_NOTFOUND)
		{
			version_put (transaction_a, version_current);
			scendere::raw_key salt_l;
			random_pool::generate_block (salt_l.bytes.data (), salt_l.bytes.size ());
			entry_put_raw (transaction_a, scendere::wallet_store::salt_special, scendere::wallet_value (salt_l, 0));
			// Wallet key is a fixed random key that encrypts all entries
			scendere::raw_key wallet_key;
			random_pool::generate_block (wallet_key.bytes.data (), sizeof (wallet_key.bytes));
			scendere::raw_key password_l;
			password_l.clear ();
			password.value_set (password_l);
			scendere::raw_key zero;
			zero.clear ();
			// Wallet key is encrypted by the user's password
			scendere::raw_key encrypted;
			encrypted.encrypt (wallet_key, zero, salt_l.owords[0]);
			entry_put_raw (transaction_a, scendere::wallet_store::wallet_key_special, scendere::wallet_value (encrypted, 0));
			scendere::raw_key wallet_key_enc;
			wallet_key_enc = encrypted;
			wallet_key_mem.value_set (wallet_key_enc);
			scendere::raw_key check;
			check.encrypt (zero, wallet_key, salt_l.owords[check_iv_index]);
			entry_put_raw (transaction_a, scendere::wallet_store::check_special, scendere::wallet_value (check, 0));
			scendere::raw_key rep;
			rep.bytes = representative_a.bytes;
			entry_put_raw (transaction_a, scendere::wallet_store::representative_special, scendere::wallet_value (rep, 0));
			scendere::raw_key seed;
			random_pool::generate_block (seed.bytes.data (), seed.bytes.size ());
			seed_set (transaction_a, seed);
			entry_put_raw (transaction_a, scendere::wallet_store::deterministic_index_special, scendere::wallet_value (0, 0));
		}
	}
	scendere::raw_key key;
	key = entry_get_raw (transaction_a, scendere::wallet_store::wallet_key_special).key;
	wallet_key_mem.value_set (key);
}

std::vector<scendere::account> scendere::wallet_store::accounts (scendere::transaction const & transaction_a)
{
	std::vector<scendere::account> result;
	for (auto i (begin (transaction_a)), n (end ()); i != n; ++i)
	{
		scendere::account const & account (i->first);
		result.push_back (account);
	}
	return result;
}

void scendere::wallet_store::initialize (scendere::transaction const & transaction_a, bool & init_a, std::string const & path_a)
{
	debug_assert (strlen (path_a.c_str ()) == path_a.size ());
	auto error (0);
	MDB_dbi handle_l;
	error |= mdb_dbi_open (tx (transaction_a), path_a.c_str (), MDB_CREATE, &handle_l);
	handle = handle_l;
	init_a = error != 0;
}

bool scendere::wallet_store::is_representative (scendere::transaction const & transaction_a)
{
	return exists (transaction_a, representative (transaction_a));
}

void scendere::wallet_store::representative_set (scendere::transaction const & transaction_a, scendere::account const & representative_a)
{
	scendere::raw_key rep;
	rep.bytes = representative_a.bytes;
	entry_put_raw (transaction_a, scendere::wallet_store::representative_special, scendere::wallet_value (rep, 0));
}

scendere::account scendere::wallet_store::representative (scendere::transaction const & transaction_a)
{
	scendere::wallet_value value (entry_get_raw (transaction_a, scendere::wallet_store::representative_special));
	return reinterpret_cast<scendere::account const &> (value.key);
}

scendere::public_key scendere::wallet_store::insert_adhoc (scendere::transaction const & transaction_a, scendere::raw_key const & prv)
{
	debug_assert (valid_password (transaction_a));
	scendere::public_key pub (scendere::pub_key (prv));
	scendere::raw_key password_l;
	wallet_key (password_l, transaction_a);
	scendere::raw_key ciphertext;
	ciphertext.encrypt (prv, password_l, pub.owords[0].number ());
	entry_put_raw (transaction_a, pub, scendere::wallet_value (ciphertext, 0));
	return pub;
}

bool scendere::wallet_store::insert_watch (scendere::transaction const & transaction_a, scendere::account const & pub_a)
{
	bool error (!valid_public_key (pub_a));
	if (!error)
	{
		entry_put_raw (transaction_a, pub_a, scendere::wallet_value (scendere::raw_key (0), 0));
	}
	return error;
}

void scendere::wallet_store::erase (scendere::transaction const & transaction_a, scendere::account const & pub)
{
	auto status (mdb_del (tx (transaction_a), handle, scendere::mdb_val (pub), nullptr));
	(void)status;
	debug_assert (status == 0);
}

scendere::wallet_value scendere::wallet_store::entry_get_raw (scendere::transaction const & transaction_a, scendere::account const & pub_a)
{
	scendere::wallet_value result;
	scendere::mdb_val value;
	auto status (mdb_get (tx (transaction_a), handle, scendere::mdb_val (pub_a), value));
	if (status == 0)
	{
		result = scendere::wallet_value (value);
	}
	else
	{
		result.key.clear ();
		result.work = 0;
	}
	return result;
}

void scendere::wallet_store::entry_put_raw (scendere::transaction const & transaction_a, scendere::account const & pub_a, scendere::wallet_value const & entry_a)
{
	auto status (mdb_put (tx (transaction_a), handle, scendere::mdb_val (pub_a), scendere::mdb_val (sizeof (entry_a), const_cast<scendere::wallet_value *> (&entry_a)), 0));
	(void)status;
	debug_assert (status == 0);
}

scendere::key_type scendere::wallet_store::key_type (scendere::wallet_value const & value_a)
{
	auto number (value_a.key.number ());
	scendere::key_type result;
	auto text (number.convert_to<std::string> ());
	if (number > std::numeric_limits<uint64_t>::max ())
	{
		result = scendere::key_type::adhoc;
	}
	else
	{
		if ((number >> 32).convert_to<uint32_t> () == 1)
		{
			result = scendere::key_type::deterministic;
		}
		else
		{
			result = scendere::key_type::unknown;
		}
	}
	return result;
}

bool scendere::wallet_store::fetch (scendere::transaction const & transaction_a, scendere::account const & pub, scendere::raw_key & prv)
{
	auto result (false);
	if (valid_password (transaction_a))
	{
		scendere::wallet_value value (entry_get_raw (transaction_a, pub));
		if (!value.key.is_zero ())
		{
			switch (key_type (value))
			{
				case scendere::key_type::deterministic:
				{
					scendere::raw_key seed_l;
					seed (seed_l, transaction_a);
					uint32_t index (static_cast<uint32_t> (value.key.number () & static_cast<uint32_t> (-1)));
					prv = deterministic_key (transaction_a, index);
					break;
				}
				case scendere::key_type::adhoc:
				{
					// Ad-hoc keys
					scendere::raw_key password_l;
					wallet_key (password_l, transaction_a);
					prv.decrypt (value.key, password_l, pub.owords[0].number ());
					break;
				}
				default:
				{
					result = true;
					break;
				}
			}
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
	if (!result)
	{
		scendere::public_key compare (scendere::pub_key (prv));
		if (!(pub == compare))
		{
			result = true;
		}
	}
	return result;
}

bool scendere::wallet_store::valid_public_key (scendere::public_key const & pub)
{
	return pub.number () >= special_count;
}

bool scendere::wallet_store::exists (scendere::transaction const & transaction_a, scendere::public_key const & pub)
{
	return valid_public_key (pub) && find (transaction_a, pub) != end ();
}

void scendere::wallet_store::serialize_json (scendere::transaction const & transaction_a, std::string & string_a)
{
	boost::property_tree::ptree tree;
	for (scendere::store_iterator<scendere::uint256_union, scendere::wallet_value> i (std::make_unique<scendere::mdb_iterator<scendere::uint256_union, scendere::wallet_value>> (transaction_a, handle)), n (nullptr); i != n; ++i)
	{
		tree.put (i->first.to_string (), i->second.key.to_string ());
	}
	std::stringstream ostream;
	boost::property_tree::write_json (ostream, tree);
	string_a = ostream.str ();
}

void scendere::wallet_store::write_backup (scendere::transaction const & transaction_a, boost::filesystem::path const & path_a)
{
	std::ofstream backup_file;
	backup_file.open (path_a.string ());
	if (!backup_file.fail ())
	{
		// Set permissions to 600
		boost::system::error_code ec;
		scendere::set_secure_perm_file (path_a, ec);

		std::string json;
		serialize_json (transaction_a, json);
		backup_file << json;
	}
}

bool scendere::wallet_store::move (scendere::transaction const & transaction_a, scendere::wallet_store & other_a, std::vector<scendere::public_key> const & keys)
{
	debug_assert (valid_password (transaction_a));
	debug_assert (other_a.valid_password (transaction_a));
	auto result (false);
	for (auto i (keys.begin ()), n (keys.end ()); i != n; ++i)
	{
		scendere::raw_key prv;
		auto error (other_a.fetch (transaction_a, *i, prv));
		result = result | error;
		if (!result)
		{
			insert_adhoc (transaction_a, prv);
			other_a.erase (transaction_a, *i);
		}
	}
	return result;
}

bool scendere::wallet_store::import (scendere::transaction const & transaction_a, scendere::wallet_store & other_a)
{
	debug_assert (valid_password (transaction_a));
	debug_assert (other_a.valid_password (transaction_a));
	auto result (false);
	for (auto i (other_a.begin (transaction_a)), n (end ()); i != n; ++i)
	{
		scendere::raw_key prv;
		auto error (other_a.fetch (transaction_a, i->first, prv));
		result = result | error;
		if (!result)
		{
			if (!prv.is_zero ())
			{
				insert_adhoc (transaction_a, prv);
			}
			else
			{
				insert_watch (transaction_a, i->first);
			}
			other_a.erase (transaction_a, i->first);
		}
	}
	return result;
}

bool scendere::wallet_store::work_get (scendere::transaction const & transaction_a, scendere::public_key const & pub_a, uint64_t & work_a)
{
	auto result (false);
	auto entry (entry_get_raw (transaction_a, pub_a));
	if (!entry.key.is_zero ())
	{
		work_a = entry.work;
	}
	else
	{
		result = true;
	}
	return result;
}

void scendere::wallet_store::work_put (scendere::transaction const & transaction_a, scendere::public_key const & pub_a, uint64_t work_a)
{
	auto entry (entry_get_raw (transaction_a, pub_a));
	debug_assert (!entry.key.is_zero ());
	entry.work = work_a;
	entry_put_raw (transaction_a, pub_a, entry);
}

unsigned scendere::wallet_store::version (scendere::transaction const & transaction_a)
{
	scendere::wallet_value value (entry_get_raw (transaction_a, scendere::wallet_store::version_special));
	auto entry (value.key);
	auto result (static_cast<unsigned> (entry.bytes[31]));
	return result;
}

void scendere::wallet_store::version_put (scendere::transaction const & transaction_a, unsigned version_a)
{
	scendere::raw_key entry (version_a);
	entry_put_raw (transaction_a, scendere::wallet_store::version_special, scendere::wallet_value (entry, 0));
}

void scendere::kdf::phs (scendere::raw_key & result_a, std::string const & password_a, scendere::uint256_union const & salt_a)
{
	scendere::lock_guard<scendere::mutex> lock (mutex);
	auto success (argon2_hash (1, kdf_work, 1, password_a.data (), password_a.size (), salt_a.bytes.data (), salt_a.bytes.size (), result_a.bytes.data (), result_a.bytes.size (), NULL, 0, Argon2_d, 0x10));
	debug_assert (success == 0);
	(void)success;
}

scendere::wallet::wallet (bool & init_a, scendere::transaction & transaction_a, scendere::wallets & wallets_a, std::string const & wallet_a) :
	lock_observer ([] (bool, bool) {}),
	store (init_a, wallets_a.kdf, transaction_a, wallets_a.node.config.random_representative (), wallets_a.node.config.password_fanout, wallet_a),
	wallets (wallets_a)
{
}

scendere::wallet::wallet (bool & init_a, scendere::transaction & transaction_a, scendere::wallets & wallets_a, std::string const & wallet_a, std::string const & json) :
	lock_observer ([] (bool, bool) {}),
	store (init_a, wallets_a.kdf, transaction_a, wallets_a.node.config.random_representative (), wallets_a.node.config.password_fanout, wallet_a, json),
	wallets (wallets_a)
{
}

void scendere::wallet::enter_initial_password ()
{
	scendere::raw_key password_l;
	{
		scendere::lock_guard<std::recursive_mutex> lock (store.mutex);
		store.password.value (password_l);
	}
	if (password_l.is_zero ())
	{
		auto transaction (wallets.tx_begin_write ());
		if (store.valid_password (transaction))
		{
			// Newly created wallets have a zero key
			store.rekey (transaction, "");
		}
		else
		{
			enter_password (transaction, "");
		}
	}
}

bool scendere::wallet::enter_password (scendere::transaction const & transaction_a, std::string const & password_a)
{
	auto result (store.attempt_password (transaction_a, password_a));
	if (!result)
	{
		auto this_l (shared_from_this ());
		wallets.node.background ([this_l] () {
			this_l->search_receivable (this_l->wallets.tx_begin_read ());
		});
		wallets.node.logger.try_log ("Wallet unlocked");
	}
	else
	{
		wallets.node.logger.try_log ("Invalid password, wallet locked");
	}
	lock_observer (result, password_a.empty ());
	return result;
}

scendere::public_key scendere::wallet::deterministic_insert (scendere::transaction const & transaction_a, bool generate_work_a)
{
	scendere::public_key key{};
	if (store.valid_password (transaction_a))
	{
		key = store.deterministic_insert (transaction_a);
		if (generate_work_a)
		{
			work_ensure (key, key);
		}
		auto half_principal_weight (wallets.node.minimum_principal_weight () / 2);
		if (wallets.check_rep (key, half_principal_weight))
		{
			scendere::lock_guard<scendere::mutex> lock (representatives_mutex);
			representatives.insert (key);
		}
	}
	return key;
}

scendere::public_key scendere::wallet::deterministic_insert (uint32_t const index, bool generate_work_a)
{
	auto transaction (wallets.tx_begin_write ());
	scendere::public_key key{};
	if (store.valid_password (transaction))
	{
		key = store.deterministic_insert (transaction, index);
		if (generate_work_a)
		{
			work_ensure (key, key);
		}
	}
	return key;
}

scendere::public_key scendere::wallet::deterministic_insert (bool generate_work_a)
{
	auto transaction (wallets.tx_begin_write ());
	auto result (deterministic_insert (transaction, generate_work_a));
	return result;
}

scendere::public_key scendere::wallet::insert_adhoc (scendere::raw_key const & key_a, bool generate_work_a)
{
	scendere::public_key key{};
	auto transaction (wallets.tx_begin_write ());
	if (store.valid_password (transaction))
	{
		key = store.insert_adhoc (transaction, key_a);
		auto block_transaction (wallets.node.store.tx_begin_read ());
		if (generate_work_a)
		{
			work_ensure (key, wallets.node.ledger.latest_root (block_transaction, key));
		}
		auto half_principal_weight (wallets.node.minimum_principal_weight () / 2);
		// Makes sure that the representatives container will
		// be in sync with any added keys.
		transaction.commit ();
		if (wallets.check_rep (key, half_principal_weight))
		{
			scendere::lock_guard<scendere::mutex> lock (representatives_mutex);
			representatives.insert (key);
		}
	}
	return key;
}

bool scendere::wallet::insert_watch (scendere::transaction const & transaction_a, scendere::public_key const & pub_a)
{
	return store.insert_watch (transaction_a, pub_a);
}

bool scendere::wallet::exists (scendere::public_key const & account_a)
{
	auto transaction (wallets.tx_begin_read ());
	return store.exists (transaction, account_a);
}

bool scendere::wallet::import (std::string const & json_a, std::string const & password_a)
{
	auto error (false);
	std::unique_ptr<scendere::wallet_store> temp;
	{
		auto transaction (wallets.tx_begin_write ());
		scendere::uint256_union id;
		random_pool::generate_block (id.bytes.data (), id.bytes.size ());
		temp = std::make_unique<scendere::wallet_store> (error, wallets.node.wallets.kdf, transaction, 0, 1, id.to_string (), json_a);
	}
	if (!error)
	{
		auto transaction (wallets.tx_begin_write ());
		error = temp->attempt_password (transaction, password_a);
	}
	auto transaction (wallets.tx_begin_write ());
	if (!error)
	{
		error = store.import (transaction, *temp);
	}
	temp->destroy (transaction);
	return error;
}

void scendere::wallet::serialize (std::string & json_a)
{
	auto transaction (wallets.tx_begin_read ());
	store.serialize_json (transaction, json_a);
}

void scendere::wallet_store::destroy (scendere::transaction const & transaction_a)
{
	auto status (mdb_drop (tx (transaction_a), handle, 1));
	(void)status;
	debug_assert (status == 0);
	handle = 0;
}

std::shared_ptr<scendere::block> scendere::wallet::receive_action (scendere::block_hash const & send_hash_a, scendere::account const & representative_a, scendere::uint128_union const & amount_a, scendere::account const & account_a, uint64_t work_a, bool generate_work_a)
{
	std::shared_ptr<scendere::block> block;
	scendere::block_details details;
	details.is_receive = true;
	if (wallets.node.config.receive_minimum.number () <= amount_a.number ())
	{
		auto block_transaction (wallets.node.ledger.store.tx_begin_read ());
		auto transaction (wallets.tx_begin_read ());
		scendere::pending_info pending_info;
		if (wallets.node.ledger.block_or_pruned_exists (block_transaction, send_hash_a))
		{
			if (!wallets.node.ledger.store.pending.get (block_transaction, scendere::pending_key (account_a, send_hash_a), pending_info))
			{
				scendere::raw_key prv;
				if (!store.fetch (transaction, account_a, prv))
				{
					if (work_a == 0)
					{
						store.work_get (transaction, account_a, work_a);
					}
					scendere::account_info info;
					auto new_account (wallets.node.ledger.store.account.get (block_transaction, account_a, info));
					if (!new_account)
					{
						block = std::make_shared<scendere::state_block> (account_a, info.head, info.representative, info.balance.number () + pending_info.amount.number (), send_hash_a, prv, account_a, work_a);
						details.epoch = std::max (info.epoch (), pending_info.epoch);
					}
					else
					{
						block = std::make_shared<scendere::state_block> (account_a, 0, representative_a, pending_info.amount, reinterpret_cast<scendere::link const &> (send_hash_a), prv, account_a, work_a);
						details.epoch = pending_info.epoch;
					}
				}
				else
				{
					wallets.node.logger.try_log ("Unable to receive, wallet locked");
				}
			}
			else
			{
				// Ledger doesn't have this marked as available to receive anymore
			}
		}
		else
		{
			// Ledger doesn't have this block anymore.
		}
	}
	else
	{
		wallets.node.logger.try_log (boost::str (boost::format ("Not receiving block %1% due to minimum receive threshold") % send_hash_a.to_string ()));
		// Someone sent us something below the threshold of receiving
	}
	if (block != nullptr)
	{
		if (action_complete (block, account_a, generate_work_a, details))
		{
			// Return null block after work generation or ledger process error
			block = nullptr;
		}
	}
	return block;
}

std::shared_ptr<scendere::block> scendere::wallet::change_action (scendere::account const & source_a, scendere::account const & representative_a, uint64_t work_a, bool generate_work_a)
{
	std::shared_ptr<scendere::block> block;
	scendere::block_details details;
	{
		auto transaction (wallets.tx_begin_read ());
		auto block_transaction (wallets.node.store.tx_begin_read ());
		if (store.valid_password (transaction))
		{
			auto existing (store.find (transaction, source_a));
			if (existing != store.end () && !wallets.node.ledger.latest (block_transaction, source_a).is_zero ())
			{
				scendere::account_info info;
				auto error1 (wallets.node.ledger.store.account.get (block_transaction, source_a, info));
				(void)error1;
				debug_assert (!error1);
				scendere::raw_key prv;
				auto error2 (store.fetch (transaction, source_a, prv));
				(void)error2;
				debug_assert (!error2);
				if (work_a == 0)
				{
					store.work_get (transaction, source_a, work_a);
				}
				block = std::make_shared<scendere::state_block> (source_a, info.head, representative_a, info.balance, 0, prv, source_a, work_a);
				details.epoch = info.epoch ();
			}
		}
	}
	if (block != nullptr)
	{
		if (action_complete (block, source_a, generate_work_a, details))
		{
			// Return null block after work generation or ledger process error
			block = nullptr;
		}
	}
	return block;
}

std::shared_ptr<scendere::block> scendere::wallet::send_action (scendere::account const & source_a, scendere::account const & account_a, scendere::uint128_t const & amount_a, uint64_t work_a, bool generate_work_a, boost::optional<std::string> id_a)
{
	boost::optional<scendere::mdb_val> id_mdb_val;
	if (id_a)
	{
		id_mdb_val = scendere::mdb_val (id_a->size (), const_cast<char *> (id_a->data ()));
	}

	auto prepare_send = [&id_mdb_val, &wallets = this->wallets, &store = this->store, &source_a, &amount_a, &work_a, &account_a] (auto const & transaction) {
		auto block_transaction (wallets.node.store.tx_begin_read ());
		auto error (false);
		auto cached_block (false);
		std::shared_ptr<scendere::block> block;
		scendere::block_details details;
		details.is_send = true;
		if (id_mdb_val)
		{
			scendere::mdb_val result;
			auto status (mdb_get (wallets.env.tx (transaction), wallets.node.wallets.send_action_ids, *id_mdb_val, result));
			if (status == 0)
			{
				scendere::block_hash hash (result);
				block = wallets.node.store.block.get (block_transaction, hash);
				if (block != nullptr)
				{
					cached_block = true;
					wallets.node.network.flood_block (block, scendere::buffer_drop_policy::no_limiter_drop);
				}
			}
			else if (status != MDB_NOTFOUND)
			{
				error = true;
			}
		}
		if (!error && block == nullptr)
		{
			if (store.valid_password (transaction))
			{
				auto existing (store.find (transaction, source_a));
				if (existing != store.end ())
				{
					auto balance (wallets.node.ledger.account_balance (block_transaction, source_a));
					if (!balance.is_zero () && balance >= amount_a)
					{
						scendere::account_info info;
						auto error1 (wallets.node.ledger.store.account.get (block_transaction, source_a, info));
						(void)error1;
						debug_assert (!error1);
						scendere::raw_key prv;
						auto error2 (store.fetch (transaction, source_a, prv));
						(void)error2;
						debug_assert (!error2);
						if (work_a == 0)
						{
							store.work_get (transaction, source_a, work_a);
						}
						block = std::make_shared<scendere::state_block> (source_a, info.head, info.representative, balance - amount_a, account_a, prv, source_a, work_a);
						details.epoch = info.epoch ();
						if (id_mdb_val && block != nullptr)
						{
							auto status (mdb_put (wallets.env.tx (transaction), wallets.node.wallets.send_action_ids, *id_mdb_val, scendere::mdb_val (block->hash ()), 0));
							if (status != 0)
							{
								block = nullptr;
								error = true;
							}
						}
					}
				}
			}
		}
		return std::make_tuple (block, error, cached_block, details);
	};

	std::tuple<std::shared_ptr<scendere::block>, bool, bool, scendere::block_details> result;
	{
		if (id_mdb_val)
		{
			result = prepare_send (wallets.tx_begin_write ());
		}
		else
		{
			result = prepare_send (wallets.tx_begin_read ());
		}
	}

	std::shared_ptr<scendere::block> block;
	bool error;
	bool cached_block;
	scendere::block_details details;
	std::tie (block, error, cached_block, details) = result;

	if (!error && block != nullptr && !cached_block)
	{
		if (action_complete (block, source_a, generate_work_a, details))
		{
			// Return null block after work generation or ledger process error
			block = nullptr;
		}
	}
	return block;
}

bool scendere::wallet::action_complete (std::shared_ptr<scendere::block> const & block_a, scendere::account const & account_a, bool const generate_work_a, scendere::block_details const & details_a)
{
	bool error{ false };
	// Unschedule any work caching for this account
	wallets.delayed_work->erase (account_a);
	if (block_a != nullptr)
	{
		auto required_difficulty{ wallets.node.network_params.work.threshold (block_a->work_version (), details_a) };
		if (wallets.node.network_params.work.difficulty (*block_a) < required_difficulty)
		{
			wallets.node.logger.try_log (boost::str (boost::format ("Cached or provided work for block %1% account %2% is invalid, regenerating") % block_a->hash ().to_string () % account_a.to_account ()));
			debug_assert (required_difficulty <= wallets.node.max_work_generate_difficulty (block_a->work_version ()));
			error = !wallets.node.work_generate_blocking (*block_a, required_difficulty).is_initialized ();
		}
		if (!error)
		{
			error = wallets.node.process_local (block_a).code != scendere::process_result::progress;
			debug_assert (error || block_a->sideband ().details == details_a);
		}
		if (!error && generate_work_a)
		{
			work_ensure (account_a, block_a->hash ());
		}
	}
	return error;
}

bool scendere::wallet::change_sync (scendere::account const & source_a, scendere::account const & representative_a)
{
	std::promise<bool> result;
	std::future<bool> future = result.get_future ();
	change_async (
	source_a, representative_a, [&result] (std::shared_ptr<scendere::block> const & block_a) {
		result.set_value (block_a == nullptr);
	},
	true);
	return future.get ();
}

void scendere::wallet::change_async (scendere::account const & source_a, scendere::account const & representative_a, std::function<void (std::shared_ptr<scendere::block> const &)> const & action_a, uint64_t work_a, bool generate_work_a)
{
	auto this_l (shared_from_this ());
	wallets.node.wallets.queue_wallet_action (scendere::wallets::high_priority, this_l, [this_l, source_a, representative_a, action_a, work_a, generate_work_a] (scendere::wallet & wallet_a) {
		auto block (wallet_a.change_action (source_a, representative_a, work_a, generate_work_a));
		action_a (block);
	});
}

bool scendere::wallet::receive_sync (std::shared_ptr<scendere::block> const & block_a, scendere::account const & representative_a, scendere::uint128_t const & amount_a)
{
	std::promise<bool> result;
	std::future<bool> future = result.get_future ();
	auto destination (block_a->link ().is_zero () ? block_a->destination () : block_a->link ().as_account ());
	receive_async (
	block_a->hash (), representative_a, amount_a, destination, [&result] (std::shared_ptr<scendere::block> const & block_a) {
		result.set_value (block_a == nullptr);
	},
	true);
	return future.get ();
}

void scendere::wallet::receive_async (scendere::block_hash const & hash_a, scendere::account const & representative_a, scendere::uint128_t const & amount_a, scendere::account const & account_a, std::function<void (std::shared_ptr<scendere::block> const &)> const & action_a, uint64_t work_a, bool generate_work_a)
{
	auto this_l (shared_from_this ());
	wallets.node.wallets.queue_wallet_action (amount_a, this_l, [this_l, hash_a, representative_a, amount_a, account_a, action_a, work_a, generate_work_a] (scendere::wallet & wallet_a) {
		auto block (wallet_a.receive_action (hash_a, representative_a, amount_a, account_a, work_a, generate_work_a));
		action_a (block);
	});
}

scendere::block_hash scendere::wallet::send_sync (scendere::account const & source_a, scendere::account const & account_a, scendere::uint128_t const & amount_a)
{
	std::promise<scendere::block_hash> result;
	std::future<scendere::block_hash> future = result.get_future ();
	send_async (
	source_a, account_a, amount_a, [&result] (std::shared_ptr<scendere::block> const & block_a) {
		result.set_value (block_a->hash ());
	},
	true);
	return future.get ();
}

void scendere::wallet::send_async (scendere::account const & source_a, scendere::account const & account_a, scendere::uint128_t const & amount_a, std::function<void (std::shared_ptr<scendere::block> const &)> const & action_a, uint64_t work_a, bool generate_work_a, boost::optional<std::string> id_a)
{
	auto this_l (shared_from_this ());
	wallets.node.wallets.queue_wallet_action (scendere::wallets::high_priority, this_l, [this_l, source_a, account_a, amount_a, action_a, work_a, generate_work_a, id_a] (scendere::wallet & wallet_a) {
		auto block (wallet_a.send_action (source_a, account_a, amount_a, work_a, generate_work_a, id_a));
		action_a (block);
	});
}

// Update work for account if latest root is root_a
void scendere::wallet::work_update (scendere::transaction const & transaction_a, scendere::account const & account_a, scendere::root const & root_a, uint64_t work_a)
{
	debug_assert (!wallets.node.network_params.work.validate_entry (scendere::work_version::work_1, root_a, work_a));
	debug_assert (store.exists (transaction_a, account_a));
	auto block_transaction (wallets.node.store.tx_begin_read ());
	auto latest (wallets.node.ledger.latest_root (block_transaction, account_a));
	if (latest == root_a)
	{
		store.work_put (transaction_a, account_a, work_a);
	}
	else
	{
		wallets.node.logger.try_log ("Cached work no longer valid, discarding");
	}
}

void scendere::wallet::work_ensure (scendere::account const & account_a, scendere::root const & root_a)
{
	using namespace std::chrono_literals;
	std::chrono::seconds const precache_delay = wallets.node.network_params.network.is_dev_network () ? 1s : 10s;

	wallets.delayed_work->operator[] (account_a) = root_a;

	wallets.node.workers.add_timed_task (std::chrono::steady_clock::now () + precache_delay, [this_l = shared_from_this (), account_a, root_a] {
		auto delayed_work = this_l->wallets.delayed_work.lock ();
		auto existing (delayed_work->find (account_a));
		if (existing != delayed_work->end () && existing->second == root_a)
		{
			delayed_work->erase (existing);
			this_l->wallets.queue_wallet_action (scendere::wallets::generate_priority, this_l, [account_a, root_a] (scendere::wallet & wallet_a) {
				wallet_a.work_cache_blocking (account_a, root_a);
			});
		}
	});
}

bool scendere::wallet::search_receivable (scendere::transaction const & wallet_transaction_a)
{
	auto result (!store.valid_password (wallet_transaction_a));
	if (!result)
	{
		wallets.node.logger.try_log ("Beginning receivable block search");
		for (auto i (store.begin (wallet_transaction_a)), n (store.end ()); i != n; ++i)
		{
			auto block_transaction (wallets.node.store.tx_begin_read ());
			scendere::account const & account (i->first);
			// Don't search pending for watch-only accounts
			if (!scendere::wallet_value (i->second).key.is_zero ())
			{
				for (auto j (wallets.node.store.pending.begin (block_transaction, scendere::pending_key (account, 0))), k (wallets.node.store.pending.end ()); j != k && scendere::pending_key (j->first).account == account; ++j)
				{
					scendere::pending_key key (j->first);
					auto hash (key.hash);
					scendere::pending_info pending (j->second);
					auto amount (pending.amount.number ());
					if (wallets.node.config.receive_minimum.number () <= amount)
					{
						wallets.node.logger.try_log (boost::str (boost::format ("Found a receivable block %1% for account %2%") % hash.to_string () % pending.source.to_account ()));
						if (wallets.node.ledger.block_confirmed (block_transaction, hash))
						{
							auto representative = store.representative (wallet_transaction_a);
							// Receive confirmed block
							receive_async (hash, representative, amount, account, [] (std::shared_ptr<scendere::block> const &) {});
						}
						else if (!wallets.node.confirmation_height_processor.is_processing_block (hash))
						{
							auto block (wallets.node.store.block.get (block_transaction, hash));
							if (block)
							{
								// Request confirmation for block which is not being processed yet
								wallets.node.block_confirm (block);
							}
						}
					}
				}
			}
		}
		wallets.node.logger.try_log ("Receivable block search phase completed");
	}
	else
	{
		wallets.node.logger.try_log ("Stopping search, wallet is locked");
	}
	return result;
}

void scendere::wallet::init_free_accounts (scendere::transaction const & transaction_a)
{
	free_accounts.clear ();
	for (auto i (store.begin (transaction_a)), n (store.end ()); i != n; ++i)
	{
		free_accounts.insert (i->first);
	}
}

uint32_t scendere::wallet::deterministic_check (scendere::transaction const & transaction_a, uint32_t index)
{
	auto block_transaction (wallets.node.store.tx_begin_read ());
	for (uint32_t i (index + 1), n (index + 64); i < n; ++i)
	{
		auto prv = store.deterministic_key (transaction_a, i);
		scendere::keypair pair (prv.to_string ());
		// Check if account received at least 1 block
		auto latest (wallets.node.ledger.latest (block_transaction, pair.pub));
		if (!latest.is_zero ())
		{
			index = i;
			// i + 64 - Check additional 64 accounts
			// i/64 - Check additional accounts for large wallets. I.e. 64000/64 = 1000 accounts to check
			n = i + 64 + (i / 64);
		}
		else
		{
			// Check if there are pending blocks for account
			for (auto ii (wallets.node.store.pending.begin (block_transaction, scendere::pending_key (pair.pub, 0))), nn (wallets.node.store.pending.end ()); ii != nn && scendere::pending_key (ii->first).account == pair.pub; ++ii)
			{
				index = i;
				n = i + 64 + (i / 64);
				break;
			}
		}
	}
	return index;
}

scendere::public_key scendere::wallet::change_seed (scendere::transaction const & transaction_a, scendere::raw_key const & prv_a, uint32_t count)
{
	store.seed_set (transaction_a, prv_a);
	auto account = deterministic_insert (transaction_a);
	if (count == 0)
	{
		count = deterministic_check (transaction_a, 0);
	}
	for (uint32_t i (0); i < count; ++i)
	{
		// Disable work generation to prevent weak CPU nodes stuck
		account = deterministic_insert (transaction_a, false);
	}
	return account;
}

void scendere::wallet::deterministic_restore (scendere::transaction const & transaction_a)
{
	auto index (store.deterministic_index_get (transaction_a));
	auto new_index (deterministic_check (transaction_a, index));
	for (uint32_t i (index); i <= new_index && index != new_index; ++i)
	{
		// Disable work generation to prevent weak CPU nodes stuck
		deterministic_insert (transaction_a, false);
	}
}

bool scendere::wallet::live ()
{
	return store.handle != 0;
}

void scendere::wallet::work_cache_blocking (scendere::account const & account_a, scendere::root const & root_a)
{
	if (wallets.node.work_generation_enabled ())
	{
		auto difficulty (wallets.node.default_difficulty (scendere::work_version::work_1));
		auto opt_work_l (wallets.node.work_generate_blocking (scendere::work_version::work_1, root_a, difficulty, account_a));
		if (opt_work_l.is_initialized ())
		{
			auto transaction_l (wallets.tx_begin_write ());
			if (live () && store.exists (transaction_l, account_a))
			{
				work_update (transaction_l, account_a, root_a, *opt_work_l);
			}
		}
		else if (!wallets.node.stopped)
		{
			wallets.node.logger.try_log (boost::str (boost::format ("Could not precache work for root %1% due to work generation failure") % root_a.to_string ()));
		}
	}
}

void scendere::wallets::do_wallet_actions ()
{
	scendere::unique_lock<scendere::mutex> action_lock (action_mutex);
	while (!stopped)
	{
		if (!actions.empty ())
		{
			auto first (actions.begin ());
			auto wallet (first->second.first);
			auto current (std::move (first->second.second));
			actions.erase (first);
			if (wallet->live ())
			{
				action_lock.unlock ();
				observer (true);
				current (*wallet);
				observer (false);
				action_lock.lock ();
			}
		}
		else
		{
			condition.wait (action_lock);
		}
	}
}

scendere::wallets::wallets (bool error_a, scendere::node & node_a) :
	network_params{ node_a.config.network_params },
	observer ([] (bool) {}),
	kdf{ node_a.config.network_params.kdf_work },
	node (node_a),
	env (boost::polymorphic_downcast<scendere::mdb_wallets_store *> (node_a.wallets_store_impl.get ())->environment),
	stopped (false)
{
	scendere::unique_lock<scendere::mutex> lock (mutex);
	if (!error_a)
	{
		auto transaction (tx_begin_write ());
		auto status (mdb_dbi_open (env.tx (transaction), nullptr, MDB_CREATE, &handle));
		split_if_needed (transaction, node.store);
		status |= mdb_dbi_open (env.tx (transaction), "send_action_ids", MDB_CREATE, &send_action_ids);
		release_assert (status == 0);
		std::string beginning (scendere::uint256_union (0).to_string ());
		std::string end ((scendere::uint256_union (scendere::uint256_t (0) - scendere::uint256_t (1))).to_string ());
		scendere::store_iterator<std::array<char, 64>, scendere::no_value> i (std::make_unique<scendere::mdb_iterator<std::array<char, 64>, scendere::no_value>> (transaction, handle, scendere::mdb_val (beginning.size (), const_cast<char *> (beginning.c_str ()))));
		scendere::store_iterator<std::array<char, 64>, scendere::no_value> n (std::make_unique<scendere::mdb_iterator<std::array<char, 64>, scendere::no_value>> (transaction, handle, scendere::mdb_val (end.size (), const_cast<char *> (end.c_str ()))));
		for (; i != n; ++i)
		{
			scendere::wallet_id id;
			std::string text (i->first.data (), i->first.size ());
			auto error (id.decode_hex (text));
			release_assert (!error);
			release_assert (items.find (id) == items.end ());
			auto wallet (std::make_shared<scendere::wallet> (error, transaction, *this, text));
			if (!error)
			{
				items[id] = wallet;
			}
			else
			{
				// Couldn't open wallet
			}
		}
	}
	// Backup before upgrade wallets
	bool backup_required (false);
	if (node.config.backup_before_upgrade)
	{
		auto transaction (tx_begin_read ());
		for (auto & item : items)
		{
			if (item.second->store.version (transaction) != scendere::wallet_store::version_current)
			{
				backup_required = true;
				break;
			}
		}
	}
	if (backup_required)
	{
		char const * store_path;
		mdb_env_get_path (env, &store_path);
		boost::filesystem::path const path (store_path);
		scendere::mdb_store::create_backup_file (env, path, node_a.logger);
	}
	for (auto & item : items)
	{
		item.second->enter_initial_password ();
	}
	if (node_a.config.enable_voting)
	{
		lock.unlock ();
		ongoing_compute_reps ();
	}
}

scendere::wallets::~wallets ()
{
	stop ();
}

std::shared_ptr<scendere::wallet> scendere::wallets::open (scendere::wallet_id const & id_a)
{
	scendere::lock_guard<scendere::mutex> lock (mutex);
	std::shared_ptr<scendere::wallet> result;
	auto existing (items.find (id_a));
	if (existing != items.end ())
	{
		result = existing->second;
	}
	return result;
}

std::shared_ptr<scendere::wallet> scendere::wallets::create (scendere::wallet_id const & id_a)
{
	scendere::lock_guard<scendere::mutex> lock (mutex);
	debug_assert (items.find (id_a) == items.end ());
	std::shared_ptr<scendere::wallet> result;
	bool error;
	{
		auto transaction (tx_begin_write ());
		result = std::make_shared<scendere::wallet> (error, transaction, *this, id_a.to_string ());
	}
	if (!error)
	{
		items[id_a] = result;
		result->enter_initial_password ();
	}
	return result;
}

bool scendere::wallets::search_receivable (scendere::wallet_id const & wallet_a)
{
	auto result (false);
	if (auto wallet = open (wallet_a); wallet != nullptr)
	{
		result = wallet->search_receivable (tx_begin_read ());
	}
	return result;
}

void scendere::wallets::search_receivable_all ()
{
	scendere::unique_lock<scendere::mutex> lk (mutex);
	auto wallets_l = get_wallets ();
	auto wallet_transaction (tx_begin_read ());
	lk.unlock ();
	for (auto const & [id, wallet] : wallets_l)
	{
		wallet->search_receivable (wallet_transaction);
	}
}

void scendere::wallets::destroy (scendere::wallet_id const & id_a)
{
	scendere::lock_guard<scendere::mutex> lock (mutex);
	auto transaction (tx_begin_write ());
	// action_mutex should be after transactions to prevent deadlocks in deterministic_insert () & insert_adhoc ()
	scendere::lock_guard<scendere::mutex> action_lock (action_mutex);
	auto existing (items.find (id_a));
	debug_assert (existing != items.end ());
	auto wallet (existing->second);
	items.erase (existing);
	wallet->store.destroy (transaction);
}

void scendere::wallets::reload ()
{
	scendere::lock_guard<scendere::mutex> lock (mutex);
	auto transaction (tx_begin_write ());
	std::unordered_set<scendere::uint256_union> stored_items;
	std::string beginning (scendere::uint256_union (0).to_string ());
	std::string end ((scendere::uint256_union (scendere::uint256_t (0) - scendere::uint256_t (1))).to_string ());
	scendere::store_iterator<std::array<char, 64>, scendere::no_value> i (std::make_unique<scendere::mdb_iterator<std::array<char, 64>, scendere::no_value>> (transaction, handle, scendere::mdb_val (beginning.size (), const_cast<char *> (beginning.c_str ()))));
	scendere::store_iterator<std::array<char, 64>, scendere::no_value> n (std::make_unique<scendere::mdb_iterator<std::array<char, 64>, scendere::no_value>> (transaction, handle, scendere::mdb_val (end.size (), const_cast<char *> (end.c_str ()))));
	for (; i != n; ++i)
	{
		scendere::wallet_id id;
		std::string text (i->first.data (), i->first.size ());
		auto error (id.decode_hex (text));
		debug_assert (!error);
		// New wallet
		if (items.find (id) == items.end ())
		{
			auto wallet (std::make_shared<scendere::wallet> (error, transaction, *this, text));
			if (!error)
			{
				items[id] = wallet;
			}
		}
		// List of wallets on disk
		stored_items.insert (id);
	}
	// Delete non existing wallets from memory
	std::vector<scendere::wallet_id> deleted_items;
	for (auto i : items)
	{
		if (stored_items.find (i.first) == stored_items.end ())
		{
			deleted_items.push_back (i.first);
		}
	}
	for (auto & i : deleted_items)
	{
		debug_assert (items.find (i) == items.end ());
		items.erase (i);
	}
}

void scendere::wallets::queue_wallet_action (scendere::uint128_t const & amount_a, std::shared_ptr<scendere::wallet> const & wallet_a, std::function<void (scendere::wallet &)> action_a)
{
	{
		scendere::lock_guard<scendere::mutex> action_lock (action_mutex);
		actions.emplace (amount_a, std::make_pair (wallet_a, action_a));
	}
	condition.notify_all ();
}

void scendere::wallets::foreach_representative (std::function<void (scendere::public_key const & pub_a, scendere::raw_key const & prv_a)> const & action_a)
{
	if (node.config.enable_voting)
	{
		std::vector<std::pair<scendere::public_key const, scendere::raw_key const>> action_accounts_l;
		{
			auto transaction_l (tx_begin_read ());
			scendere::lock_guard<scendere::mutex> lock (mutex);
			for (auto i (items.begin ()), n (items.end ()); i != n; ++i)
			{
				auto & wallet (*i->second);
				scendere::lock_guard<std::recursive_mutex> store_lock (wallet.store.mutex);
				decltype (wallet.representatives) representatives_l;
				{
					scendere::lock_guard<scendere::mutex> representatives_lock (wallet.representatives_mutex);
					representatives_l = wallet.representatives;
				}
				for (auto const & account : representatives_l)
				{
					if (wallet.store.exists (transaction_l, account))
					{
						if (!node.ledger.weight (account).is_zero ())
						{
							if (wallet.store.valid_password (transaction_l))
							{
								scendere::raw_key prv;
								auto error (wallet.store.fetch (transaction_l, account, prv));
								(void)error;
								debug_assert (!error);
								action_accounts_l.emplace_back (account, prv);
							}
							else
							{
								static auto last_log = std::chrono::steady_clock::time_point ();
								if (last_log < std::chrono::steady_clock::now () - std::chrono::seconds (60))
								{
									last_log = std::chrono::steady_clock::now ();
									node.logger.always_log (boost::str (boost::format ("Representative locked inside wallet %1%") % i->first.to_string ()));
								}
							}
						}
					}
				}
			}
		}
		for (auto const & representative : action_accounts_l)
		{
			action_a (representative.first, representative.second);
		}
	}
}

bool scendere::wallets::exists (scendere::transaction const & transaction_a, scendere::account const & account_a)
{
	scendere::lock_guard<scendere::mutex> lock (mutex);
	auto result (false);
	for (auto i (items.begin ()), n (items.end ()); !result && i != n; ++i)
	{
		result = i->second->store.exists (transaction_a, account_a);
	}
	return result;
}

void scendere::wallets::stop ()
{
	{
		scendere::lock_guard<scendere::mutex> action_lock (action_mutex);
		stopped = true;
		actions.clear ();
	}
	condition.notify_all ();
	if (thread.joinable ())
	{
		thread.join ();
	}
}

void scendere::wallets::start ()
{
	thread = std::thread{ [this] () {
		scendere::thread_role::set (scendere::thread_role::name::wallet_actions);
		do_wallet_actions ();
	} };
}

scendere::write_transaction scendere::wallets::tx_begin_write ()
{
	return env.tx_begin_write ();
}

scendere::read_transaction scendere::wallets::tx_begin_read ()
{
	return env.tx_begin_read ();
}

void scendere::wallets::clear_send_ids (scendere::transaction const & transaction_a)
{
	auto status (mdb_drop (env.tx (transaction_a), send_action_ids, 0));
	(void)status;
	debug_assert (status == 0);
}

scendere::wallet_representatives scendere::wallets::reps () const
{
	scendere::lock_guard<scendere::mutex> counts_guard (reps_cache_mutex);
	return representatives;
}

bool scendere::wallets::check_rep (scendere::account const & account_a, scendere::uint128_t const & half_principal_weight_a, bool const acquire_lock_a)
{
	auto weight = node.ledger.weight (account_a);

	if (weight < node.config.vote_minimum.number ())
	{
		return false; // account not a representative
	}

	scendere::unique_lock<scendere::mutex> lock;
	if (acquire_lock_a)
	{
		lock = scendere::unique_lock<scendere::mutex> (reps_cache_mutex);
	}

	if (weight >= half_principal_weight_a)
	{
		representatives.half_principal = true;
	}

	auto insert_result = representatives.accounts.insert (account_a);
	if (!insert_result.second)
	{
		return false; // account already exists
	}

	++representatives.voting;

	return true;
}

void scendere::wallets::compute_reps ()
{
	scendere::lock_guard<scendere::mutex> guard (mutex);
	scendere::lock_guard<scendere::mutex> counts_guard (reps_cache_mutex);
	representatives.clear ();
	auto half_principal_weight (node.minimum_principal_weight () / 2);
	auto transaction (tx_begin_read ());
	for (auto i (items.begin ()), n (items.end ()); i != n; ++i)
	{
		auto & wallet (*i->second);
		decltype (wallet.representatives) representatives_l;
		for (auto ii (wallet.store.begin (transaction)), nn (wallet.store.end ()); ii != nn; ++ii)
		{
			auto account (ii->first);
			if (check_rep (account, half_principal_weight, false))
			{
				representatives_l.insert (account);
			}
		}
		scendere::lock_guard<scendere::mutex> representatives_guard (wallet.representatives_mutex);
		wallet.representatives.swap (representatives_l);
	}
}

void scendere::wallets::ongoing_compute_reps ()
{
	compute_reps ();
	auto & node_l (node);
	auto compute_delay (network_params.network.is_dev_network () ? std::chrono::milliseconds (10) : std::chrono::milliseconds (15 * 60 * 1000)); // Representation drifts quickly on the test network but very slowly on the live network
	node.workers.add_timed_task (std::chrono::steady_clock::now () + compute_delay, [&node_l] () {
		node_l.wallets.ongoing_compute_reps ();
	});
}

void scendere::wallets::split_if_needed (scendere::transaction & transaction_destination, scendere::store & store_a)
{
	auto store_l (dynamic_cast<scendere::mdb_store *> (&store_a));
	if (store_l != nullptr)
	{
		if (items.empty ())
		{
			std::string beginning (scendere::uint256_union (0).to_string ());
			std::string end ((scendere::uint256_union (scendere::uint256_t (0) - scendere::uint256_t (1))).to_string ());

			auto get_store_it = [&handle = handle] (scendere::transaction const & transaction_source, std::string const & hash) {
				return scendere::store_iterator<std::array<char, 64>, scendere::no_value> (std::make_unique<scendere::mdb_iterator<std::array<char, 64>, scendere::no_value>> (transaction_source, handle, scendere::mdb_val (hash.size (), const_cast<char *> (hash.c_str ()))));
			};

			// First do a read pass to check if there are any wallets that need extracting (to save holding a write lock and potentially being blocked)
			auto wallets_need_splitting (false);
			{
				auto transaction_source (store_l->tx_begin_read ());
				auto i = get_store_it (transaction_source, beginning);
				auto n = get_store_it (transaction_source, end);
				wallets_need_splitting = (i != n);
			}

			if (wallets_need_splitting)
			{
				auto transaction_source (store_l->tx_begin_write ());
				auto i = get_store_it (transaction_source, beginning);
				auto n = get_store_it (transaction_source, end);
				auto tx_source = static_cast<MDB_txn *> (transaction_source.get_handle ());
				auto tx_destination = static_cast<MDB_txn *> (transaction_destination.get_handle ());
				for (; i != n; ++i)
				{
					scendere::uint256_union id;
					std::string text (i->first.data (), i->first.size ());
					auto error1 (id.decode_hex (text));
					(void)error1;
					debug_assert (!error1);
					debug_assert (strlen (text.c_str ()) == text.size ());
					move_table (text, tx_source, tx_destination);
				}
			}
		}
	}
}

void scendere::wallets::move_table (std::string const & name_a, MDB_txn * tx_source, MDB_txn * tx_destination)
{
	MDB_dbi handle_source;
	auto error2 (mdb_dbi_open (tx_source, name_a.c_str (), MDB_CREATE, &handle_source));
	(void)error2;
	debug_assert (!error2);
	MDB_dbi handle_destination;
	auto error3 (mdb_dbi_open (tx_destination, name_a.c_str (), MDB_CREATE, &handle_destination));
	(void)error3;
	debug_assert (!error3);
	MDB_cursor * cursor;
	auto error4 (mdb_cursor_open (tx_source, handle_source, &cursor));
	(void)error4;
	debug_assert (!error4);
	MDB_val val_key;
	MDB_val val_value;
	auto cursor_status (mdb_cursor_get (cursor, &val_key, &val_value, MDB_FIRST));
	while (cursor_status == MDB_SUCCESS)
	{
		auto error5 (mdb_put (tx_destination, handle_destination, &val_key, &val_value, 0));
		(void)error5;
		debug_assert (!error5);
		cursor_status = mdb_cursor_get (cursor, &val_key, &val_value, MDB_NEXT);
	}
	auto error6 (mdb_drop (tx_source, handle_source, 1));
	(void)error6;
	debug_assert (!error6);
}

std::unordered_map<scendere::wallet_id, std::shared_ptr<scendere::wallet>> scendere::wallets::get_wallets ()
{
	debug_assert (!mutex.try_lock ());
	return items;
}

scendere::uint128_t const scendere::wallets::generate_priority = std::numeric_limits<scendere::uint128_t>::max ();
scendere::uint128_t const scendere::wallets::high_priority = std::numeric_limits<scendere::uint128_t>::max () - 1;

scendere::store_iterator<scendere::account, scendere::wallet_value> scendere::wallet_store::begin (scendere::transaction const & transaction_a)
{
	scendere::store_iterator<scendere::account, scendere::wallet_value> result (std::make_unique<scendere::mdb_iterator<scendere::account, scendere::wallet_value>> (transaction_a, handle, scendere::mdb_val (scendere::account (special_count))));
	return result;
}

scendere::store_iterator<scendere::account, scendere::wallet_value> scendere::wallet_store::begin (scendere::transaction const & transaction_a, scendere::account const & key)
{
	scendere::store_iterator<scendere::account, scendere::wallet_value> result (std::make_unique<scendere::mdb_iterator<scendere::account, scendere::wallet_value>> (transaction_a, handle, scendere::mdb_val (key)));
	return result;
}

scendere::store_iterator<scendere::account, scendere::wallet_value> scendere::wallet_store::find (scendere::transaction const & transaction_a, scendere::account const & key)
{
	auto result (begin (transaction_a, key));
	scendere::store_iterator<scendere::account, scendere::wallet_value> end (nullptr);
	if (result != end)
	{
		if (result->first == key)
		{
			return result;
		}
		else
		{
			return end;
		}
	}
	else
	{
		return end;
	}
	return result;
}

scendere::store_iterator<scendere::account, scendere::wallet_value> scendere::wallet_store::end ()
{
	return scendere::store_iterator<scendere::account, scendere::wallet_value> (nullptr);
}
scendere::mdb_wallets_store::mdb_wallets_store (boost::filesystem::path const & path_a, scendere::lmdb_config const & lmdb_config_a) :
	environment (error, path_a, scendere::mdb_env::options::make ().set_config (lmdb_config_a).override_config_sync (scendere::lmdb_config::sync_strategy::always).override_config_map_size (1ULL * 1024 * 1024 * 1024))
{
}

bool scendere::mdb_wallets_store::init_error () const
{
	return error;
}

MDB_txn * scendere::wallet_store::tx (scendere::transaction const & transaction_a) const
{
	return static_cast<MDB_txn *> (transaction_a.get_handle ());
}

std::unique_ptr<scendere::container_info_component> scendere::collect_container_info (wallets & wallets, std::string const & name)
{
	std::size_t items_count;
	std::size_t actions_count;
	{
		scendere::lock_guard<scendere::mutex> guard (wallets.mutex);
		items_count = wallets.items.size ();
		actions_count = wallets.actions.size ();
	}

	auto sizeof_item_element = sizeof (decltype (wallets.items)::value_type);
	auto sizeof_actions_element = sizeof (decltype (wallets.actions)::value_type);
	auto composite = std::make_unique<container_info_composite> (name);
	composite->add_component (std::make_unique<container_info_leaf> (container_info{ "items", items_count, sizeof_item_element }));
	composite->add_component (std::make_unique<container_info_leaf> (container_info{ "actions", actions_count, sizeof_actions_element }));
	return composite;
}
