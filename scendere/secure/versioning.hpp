#pragma once

#include <scendere/lib/blocks.hpp>
#include <scendere/secure/common.hpp>

struct MDB_val;

namespace scendere
{
class pending_info_v14 final
{
public:
	pending_info_v14 () = default;
	pending_info_v14 (scendere::account const &, scendere::amount const &, scendere::epoch);
	size_t db_size () const;
	bool deserialize (scendere::stream &);
	bool operator== (scendere::pending_info_v14 const &) const;
	scendere::account source{};
	scendere::amount amount{ 0 };
	scendere::epoch epoch{ scendere::epoch::epoch_0 };
};
class account_info_v14 final
{
public:
	account_info_v14 () = default;
	account_info_v14 (scendere::block_hash const &, scendere::block_hash const &, scendere::block_hash const &, scendere::amount const &, uint64_t, uint64_t, uint64_t, scendere::epoch);
	size_t db_size () const;
	scendere::block_hash head{ 0 };
	scendere::block_hash rep_block{ 0 };
	scendere::block_hash open_block{ 0 };
	scendere::amount balance{ 0 };
	uint64_t modified{ 0 };
	uint64_t block_count{ 0 };
	uint64_t confirmation_height{ 0 };
	scendere::epoch epoch{ scendere::epoch::epoch_0 };
};
class block_sideband_v14 final
{
public:
	block_sideband_v14 () = default;
	block_sideband_v14 (scendere::block_type, scendere::account const &, scendere::block_hash const &, scendere::amount const &, uint64_t, uint64_t);
	void serialize (scendere::stream &) const;
	bool deserialize (scendere::stream &);
	static size_t size (scendere::block_type);
	scendere::block_type type{ scendere::block_type::invalid };
	scendere::block_hash successor{ 0 };
	scendere::account account{};
	scendere::amount balance{ 0 };
	uint64_t height{ 0 };
	uint64_t timestamp{ 0 };
};
class state_block_w_sideband_v14
{
public:
	std::shared_ptr<scendere::state_block> state_block;
	scendere::block_sideband_v14 sideband;
};
class block_sideband_v18 final
{
public:
	block_sideband_v18 () = default;
	block_sideband_v18 (scendere::account const &, scendere::block_hash const &, scendere::amount const &, uint64_t, uint64_t, scendere::block_details const &);
	block_sideband_v18 (scendere::account const &, scendere::block_hash const &, scendere::amount const &, uint64_t, uint64_t, scendere::epoch, bool is_send, bool is_receive, bool is_epoch);
	void serialize (scendere::stream &, scendere::block_type) const;
	bool deserialize (scendere::stream &, scendere::block_type);
	static size_t size (scendere::block_type);
	scendere::block_hash successor{ 0 };
	scendere::account account{};
	scendere::amount balance{ 0 };
	uint64_t height{ 0 };
	uint64_t timestamp{ 0 };
	scendere::block_details details;
};
}
