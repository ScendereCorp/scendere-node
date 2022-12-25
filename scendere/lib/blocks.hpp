#pragma once

#include <scendere/crypto/blake2/blake2.h>
#include <scendere/lib/epoch.hpp>
#include <scendere/lib/errors.hpp>
#include <scendere/lib/numbers.hpp>
#include <scendere/lib/optional_ptr.hpp>
#include <scendere/lib/stream.hpp>
#include <scendere/lib/utility.hpp>
#include <scendere/lib/work.hpp>

#include <boost/property_tree/ptree_fwd.hpp>

#include <unordered_map>

namespace scendere
{
class block_visitor;
class mutable_block_visitor;
enum class block_type : uint8_t
{
	invalid = 0,
	not_a_block = 1,
	send = 2,
	receive = 3,
	open = 4,
	change = 5,
	state = 6
};
class block_details
{
	static_assert (std::is_same<std::underlying_type<scendere::epoch>::type, uint8_t> (), "Epoch enum is not the proper type");
	static_assert (static_cast<uint8_t> (scendere::epoch::max) < (1 << 5), "Epoch max is too large for the sideband");

public:
	block_details () = default;
	block_details (scendere::epoch const epoch_a, bool const is_send_a, bool const is_receive_a, bool const is_epoch_a);
	static constexpr size_t size ()
	{
		return 1;
	}
	bool operator== (block_details const & other_a) const;
	void serialize (scendere::stream &) const;
	bool deserialize (scendere::stream &);
	scendere::epoch epoch{ scendere::epoch::epoch_0 };
	bool is_send{ false };
	bool is_receive{ false };
	bool is_epoch{ false };

private:
	uint8_t packed () const;
	void unpack (uint8_t);
};

std::string state_subtype (scendere::block_details const);

class block_sideband final
{
public:
	block_sideband () = default;
	block_sideband (scendere::account const &, scendere::block_hash const &, scendere::amount const &, uint64_t const, uint64_t const, scendere::block_details const &, scendere::epoch const source_epoch_a);
	block_sideband (scendere::account const &, scendere::block_hash const &, scendere::amount const &, uint64_t const, uint64_t const, scendere::epoch const epoch_a, bool const is_send, bool const is_receive, bool const is_epoch, scendere::epoch const source_epoch_a);
	void serialize (scendere::stream &, scendere::block_type) const;
	bool deserialize (scendere::stream &, scendere::block_type);
	static size_t size (scendere::block_type);
	scendere::block_hash successor{ 0 };
	scendere::account account{};
	scendere::amount balance{ 0 };
	uint64_t height{ 0 };
	uint64_t timestamp{ 0 };
	scendere::block_details details;
	scendere::epoch source_epoch{ scendere::epoch::epoch_0 };
};
class block
{
public:
	// Return a digest of the hashables in this block.
	scendere::block_hash const & hash () const;
	// Return a digest of hashables and non-hashables in this block.
	scendere::block_hash full_hash () const;
	scendere::block_sideband const & sideband () const;
	void sideband_set (scendere::block_sideband const &);
	bool has_sideband () const;
	std::string to_json () const;
	virtual void hash (blake2b_state &) const = 0;
	virtual uint64_t block_work () const = 0;
	virtual void block_work_set (uint64_t) = 0;
	virtual scendere::account const & account () const;
	// Previous block in account's chain, zero for open block
	virtual scendere::block_hash const & previous () const = 0;
	// Source block for open/receive blocks, zero otherwise.
	virtual scendere::block_hash const & source () const;
	// Destination account for send blocks, zero otherwise.
	virtual scendere::account const & destination () const;
	// Previous block or account number for open blocks
	virtual scendere::root const & root () const = 0;
	// Qualified root value based on previous() and root()
	virtual scendere::qualified_root qualified_root () const;
	// Link field for state blocks, zero otherwise.
	virtual scendere::link const & link () const;
	virtual scendere::account const & representative () const;
	virtual scendere::amount const & balance () const;
	virtual void serialize (scendere::stream &) const = 0;
	virtual void serialize_json (std::string &, bool = false) const = 0;
	virtual void serialize_json (boost::property_tree::ptree &) const = 0;
	virtual void visit (scendere::block_visitor &) const = 0;
	virtual void visit (scendere::mutable_block_visitor &) = 0;
	virtual bool operator== (scendere::block const &) const = 0;
	virtual scendere::block_type type () const = 0;
	virtual scendere::signature const & block_signature () const = 0;
	virtual void signature_set (scendere::signature const &) = 0;
	virtual ~block () = default;
	virtual bool valid_predecessor (scendere::block const &) const = 0;
	static size_t size (scendere::block_type);
	virtual scendere::work_version work_version () const;
	// If there are any changes to the hashables, call this to update the cached hash
	void refresh ();

protected:
	mutable scendere::block_hash cached_hash{ 0 };
	/**
	 * Contextual details about a block, some fields may or may not be set depending on block type.
	 * This field is set via sideband_set in ledger processing or deserializing blocks from the database.
	 * Otherwise it may be null (for example, an old block or fork).
	 */
	scendere::optional_ptr<scendere::block_sideband> sideband_m;

private:
	scendere::block_hash generate_hash () const;
};
class send_hashables
{
public:
	send_hashables () = default;
	send_hashables (scendere::block_hash const &, scendere::account const &, scendere::amount const &);
	send_hashables (bool &, scendere::stream &);
	send_hashables (bool &, boost::property_tree::ptree const &);
	void hash (blake2b_state &) const;
	scendere::block_hash previous;
	scendere::account destination;
	scendere::amount balance;
	static std::size_t constexpr size = sizeof (previous) + sizeof (destination) + sizeof (balance);
};
class send_block : public scendere::block
{
public:
	send_block () = default;
	send_block (scendere::block_hash const &, scendere::account const &, scendere::amount const &, scendere::raw_key const &, scendere::public_key const &, uint64_t);
	send_block (bool &, scendere::stream &);
	send_block (bool &, boost::property_tree::ptree const &);
	virtual ~send_block () = default;
	using scendere::block::hash;
	void hash (blake2b_state &) const override;
	uint64_t block_work () const override;
	void block_work_set (uint64_t) override;
	scendere::block_hash const & previous () const override;
	scendere::account const & destination () const override;
	scendere::root const & root () const override;
	scendere::amount const & balance () const override;
	void serialize (scendere::stream &) const override;
	bool deserialize (scendere::stream &);
	void serialize_json (std::string &, bool = false) const override;
	void serialize_json (boost::property_tree::ptree &) const override;
	bool deserialize_json (boost::property_tree::ptree const &);
	void visit (scendere::block_visitor &) const override;
	void visit (scendere::mutable_block_visitor &) override;
	scendere::block_type type () const override;
	scendere::signature const & block_signature () const override;
	void signature_set (scendere::signature const &) override;
	bool operator== (scendere::block const &) const override;
	bool operator== (scendere::send_block const &) const;
	bool valid_predecessor (scendere::block const &) const override;
	send_hashables hashables;
	scendere::signature signature;
	uint64_t work;
	static std::size_t constexpr size = scendere::send_hashables::size + sizeof (signature) + sizeof (work);
};
class receive_hashables
{
public:
	receive_hashables () = default;
	receive_hashables (scendere::block_hash const &, scendere::block_hash const &);
	receive_hashables (bool &, scendere::stream &);
	receive_hashables (bool &, boost::property_tree::ptree const &);
	void hash (blake2b_state &) const;
	scendere::block_hash previous;
	scendere::block_hash source;
	static std::size_t constexpr size = sizeof (previous) + sizeof (source);
};
class receive_block : public scendere::block
{
public:
	receive_block () = default;
	receive_block (scendere::block_hash const &, scendere::block_hash const &, scendere::raw_key const &, scendere::public_key const &, uint64_t);
	receive_block (bool &, scendere::stream &);
	receive_block (bool &, boost::property_tree::ptree const &);
	virtual ~receive_block () = default;
	using scendere::block::hash;
	void hash (blake2b_state &) const override;
	uint64_t block_work () const override;
	void block_work_set (uint64_t) override;
	scendere::block_hash const & previous () const override;
	scendere::block_hash const & source () const override;
	scendere::root const & root () const override;
	void serialize (scendere::stream &) const override;
	bool deserialize (scendere::stream &);
	void serialize_json (std::string &, bool = false) const override;
	void serialize_json (boost::property_tree::ptree &) const override;
	bool deserialize_json (boost::property_tree::ptree const &);
	void visit (scendere::block_visitor &) const override;
	void visit (scendere::mutable_block_visitor &) override;
	scendere::block_type type () const override;
	scendere::signature const & block_signature () const override;
	void signature_set (scendere::signature const &) override;
	bool operator== (scendere::block const &) const override;
	bool operator== (scendere::receive_block const &) const;
	bool valid_predecessor (scendere::block const &) const override;
	receive_hashables hashables;
	scendere::signature signature;
	uint64_t work;
	static std::size_t constexpr size = scendere::receive_hashables::size + sizeof (signature) + sizeof (work);
};
class open_hashables
{
public:
	open_hashables () = default;
	open_hashables (scendere::block_hash const &, scendere::account const &, scendere::account const &);
	open_hashables (bool &, scendere::stream &);
	open_hashables (bool &, boost::property_tree::ptree const &);
	void hash (blake2b_state &) const;
	scendere::block_hash source;
	scendere::account representative;
	scendere::account account;
	static std::size_t constexpr size = sizeof (source) + sizeof (representative) + sizeof (account);
};
class open_block : public scendere::block
{
public:
	open_block () = default;
	open_block (scendere::block_hash const &, scendere::account const &, scendere::account const &, scendere::raw_key const &, scendere::public_key const &, uint64_t);
	open_block (scendere::block_hash const &, scendere::account const &, scendere::account const &, std::nullptr_t);
	open_block (bool &, scendere::stream &);
	open_block (bool &, boost::property_tree::ptree const &);
	virtual ~open_block () = default;
	using scendere::block::hash;
	void hash (blake2b_state &) const override;
	uint64_t block_work () const override;
	void block_work_set (uint64_t) override;
	scendere::block_hash const & previous () const override;
	scendere::account const & account () const override;
	scendere::block_hash const & source () const override;
	scendere::root const & root () const override;
	scendere::account const & representative () const override;
	void serialize (scendere::stream &) const override;
	bool deserialize (scendere::stream &);
	void serialize_json (std::string &, bool = false) const override;
	void serialize_json (boost::property_tree::ptree &) const override;
	bool deserialize_json (boost::property_tree::ptree const &);
	void visit (scendere::block_visitor &) const override;
	void visit (scendere::mutable_block_visitor &) override;
	scendere::block_type type () const override;
	scendere::signature const & block_signature () const override;
	void signature_set (scendere::signature const &) override;
	bool operator== (scendere::block const &) const override;
	bool operator== (scendere::open_block const &) const;
	bool valid_predecessor (scendere::block const &) const override;
	scendere::open_hashables hashables;
	scendere::signature signature;
	uint64_t work;
	static std::size_t constexpr size = scendere::open_hashables::size + sizeof (signature) + sizeof (work);
};
class change_hashables
{
public:
	change_hashables () = default;
	change_hashables (scendere::block_hash const &, scendere::account const &);
	change_hashables (bool &, scendere::stream &);
	change_hashables (bool &, boost::property_tree::ptree const &);
	void hash (blake2b_state &) const;
	scendere::block_hash previous;
	scendere::account representative;
	static std::size_t constexpr size = sizeof (previous) + sizeof (representative);
};
class change_block : public scendere::block
{
public:
	change_block () = default;
	change_block (scendere::block_hash const &, scendere::account const &, scendere::raw_key const &, scendere::public_key const &, uint64_t);
	change_block (bool &, scendere::stream &);
	change_block (bool &, boost::property_tree::ptree const &);
	virtual ~change_block () = default;
	using scendere::block::hash;
	void hash (blake2b_state &) const override;
	uint64_t block_work () const override;
	void block_work_set (uint64_t) override;
	scendere::block_hash const & previous () const override;
	scendere::root const & root () const override;
	scendere::account const & representative () const override;
	void serialize (scendere::stream &) const override;
	bool deserialize (scendere::stream &);
	void serialize_json (std::string &, bool = false) const override;
	void serialize_json (boost::property_tree::ptree &) const override;
	bool deserialize_json (boost::property_tree::ptree const &);
	void visit (scendere::block_visitor &) const override;
	void visit (scendere::mutable_block_visitor &) override;
	scendere::block_type type () const override;
	scendere::signature const & block_signature () const override;
	void signature_set (scendere::signature const &) override;
	bool operator== (scendere::block const &) const override;
	bool operator== (scendere::change_block const &) const;
	bool valid_predecessor (scendere::block const &) const override;
	scendere::change_hashables hashables;
	scendere::signature signature;
	uint64_t work;
	static std::size_t constexpr size = scendere::change_hashables::size + sizeof (signature) + sizeof (work);
};
class state_hashables
{
public:
	state_hashables () = default;
	state_hashables (scendere::account const &, scendere::block_hash const &, scendere::account const &, scendere::amount const &, scendere::link const &);
	state_hashables (bool &, scendere::stream &);
	state_hashables (bool &, boost::property_tree::ptree const &);
	void hash (blake2b_state &) const;
	// Account# / public key that operates this account
	// Uses:
	// Bulk signature validation in advance of further ledger processing
	// Arranging uncomitted transactions by account
	scendere::account account;
	// Previous transaction in this chain
	scendere::block_hash previous;
	// Representative of this account
	scendere::account representative;
	// Current balance of this account
	// Allows lookup of account balance simply by looking at the head block
	scendere::amount balance;
	// Link field contains source block_hash if receiving, destination account if sending
	scendere::link link;
	// Serialized size
	static std::size_t constexpr size = sizeof (account) + sizeof (previous) + sizeof (representative) + sizeof (balance) + sizeof (link);
};
class state_block : public scendere::block
{
public:
	state_block () = default;
	state_block (scendere::account const &, scendere::block_hash const &, scendere::account const &, scendere::amount const &, scendere::link const &, scendere::raw_key const &, scendere::public_key const &, uint64_t);
	state_block (bool &, scendere::stream &);
	state_block (bool &, boost::property_tree::ptree const &);
	virtual ~state_block () = default;
	using scendere::block::hash;
	void hash (blake2b_state &) const override;
	uint64_t block_work () const override;
	void block_work_set (uint64_t) override;
	scendere::block_hash const & previous () const override;
	scendere::account const & account () const override;
	scendere::root const & root () const override;
	scendere::link const & link () const override;
	scendere::account const & representative () const override;
	scendere::amount const & balance () const override;
	void serialize (scendere::stream &) const override;
	bool deserialize (scendere::stream &);
	void serialize_json (std::string &, bool = false) const override;
	void serialize_json (boost::property_tree::ptree &) const override;
	bool deserialize_json (boost::property_tree::ptree const &);
	void visit (scendere::block_visitor &) const override;
	void visit (scendere::mutable_block_visitor &) override;
	scendere::block_type type () const override;
	scendere::signature const & block_signature () const override;
	void signature_set (scendere::signature const &) override;
	bool operator== (scendere::block const &) const override;
	bool operator== (scendere::state_block const &) const;
	bool valid_predecessor (scendere::block const &) const override;
	scendere::state_hashables hashables;
	scendere::signature signature;
	uint64_t work;
	static std::size_t constexpr size = scendere::state_hashables::size + sizeof (signature) + sizeof (work);
};
class block_visitor
{
public:
	virtual void send_block (scendere::send_block const &) = 0;
	virtual void receive_block (scendere::receive_block const &) = 0;
	virtual void open_block (scendere::open_block const &) = 0;
	virtual void change_block (scendere::change_block const &) = 0;
	virtual void state_block (scendere::state_block const &) = 0;
	virtual ~block_visitor () = default;
};
class mutable_block_visitor
{
public:
	virtual void send_block (scendere::send_block &) = 0;
	virtual void receive_block (scendere::receive_block &) = 0;
	virtual void open_block (scendere::open_block &) = 0;
	virtual void change_block (scendere::change_block &) = 0;
	virtual void state_block (scendere::state_block &) = 0;
	virtual ~mutable_block_visitor () = default;
};
/**
 * This class serves to find and return unique variants of a block in order to minimize memory usage
 */
class block_uniquer
{
public:
	using value_type = std::pair<scendere::uint256_union const, std::weak_ptr<scendere::block>>;

	std::shared_ptr<scendere::block> unique (std::shared_ptr<scendere::block> const &);
	size_t size ();

private:
	scendere::mutex mutex{ mutex_identifier (mutexes::block_uniquer) };
	std::unordered_map<std::remove_const_t<value_type::first_type>, value_type::second_type> blocks;
	static unsigned constexpr cleanup_count = 2;
};

std::unique_ptr<container_info_component> collect_container_info (block_uniquer & block_uniquer, std::string const & name);

std::shared_ptr<scendere::block> deserialize_block (scendere::stream &);
std::shared_ptr<scendere::block> deserialize_block (scendere::stream &, scendere::block_type, scendere::block_uniquer * = nullptr);
std::shared_ptr<scendere::block> deserialize_block_json (boost::property_tree::ptree const &, scendere::block_uniquer * = nullptr);
void serialize_block (scendere::stream &, scendere::block const &);
void block_memory_pool_purge ();
}
