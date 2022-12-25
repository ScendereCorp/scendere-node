#include <scendere/lib/blocks.hpp>
#include <scendere/lib/numbers.hpp>
#include <scendere/node/ipc/flatbuffers_util.hpp>
#include <scendere/secure/common.hpp>

std::unique_ptr<scendereapi::BlockStateT> scendere::ipc::flatbuffers_builder::from (scendere::state_block const & block_a, scendere::amount const & amount_a, bool is_state_send_a, bool is_state_epoch_a)
{
	auto block (std::make_unique<scendereapi::BlockStateT> ());
	block->account = block_a.account ().to_account ();
	block->hash = block_a.hash ().to_string ();
	block->previous = block_a.previous ().to_string ();
	block->representative = block_a.representative ().to_account ();
	block->balance = block_a.balance ().to_string_dec ();
	block->link = block_a.link ().to_string ();
	block->link_as_account = block_a.link ().to_account ();
	block_a.signature.encode_hex (block->signature);
	block->work = scendere::to_string_hex (block_a.work);

	if (is_state_send_a)
	{
		block->subtype = scendereapi::BlockSubType::BlockSubType_send;
	}
	else if (block_a.link ().is_zero ())
	{
		block->subtype = scendereapi::BlockSubType::BlockSubType_change;
	}
	else if (amount_a == 0 && is_state_epoch_a)
	{
		block->subtype = scendereapi::BlockSubType::BlockSubType_epoch;
	}
	else
	{
		block->subtype = scendereapi::BlockSubType::BlockSubType_receive;
	}
	return block;
}

std::unique_ptr<scendereapi::BlockSendT> scendere::ipc::flatbuffers_builder::from (scendere::send_block const & block_a)
{
	auto block (std::make_unique<scendereapi::BlockSendT> ());
	block->hash = block_a.hash ().to_string ();
	block->balance = block_a.balance ().to_string_dec ();
	block->destination = block_a.hashables.destination.to_account ();
	block->previous = block_a.previous ().to_string ();
	block_a.signature.encode_hex (block->signature);
	block->work = scendere::to_string_hex (block_a.work);
	return block;
}

std::unique_ptr<scendereapi::BlockReceiveT> scendere::ipc::flatbuffers_builder::from (scendere::receive_block const & block_a)
{
	auto block (std::make_unique<scendereapi::BlockReceiveT> ());
	block->hash = block_a.hash ().to_string ();
	block->source = block_a.source ().to_string ();
	block->previous = block_a.previous ().to_string ();
	block_a.signature.encode_hex (block->signature);
	block->work = scendere::to_string_hex (block_a.work);
	return block;
}

std::unique_ptr<scendereapi::BlockOpenT> scendere::ipc::flatbuffers_builder::from (scendere::open_block const & block_a)
{
	auto block (std::make_unique<scendereapi::BlockOpenT> ());
	block->hash = block_a.hash ().to_string ();
	block->source = block_a.source ().to_string ();
	block->account = block_a.account ().to_account ();
	block->representative = block_a.representative ().to_account ();
	block_a.signature.encode_hex (block->signature);
	block->work = scendere::to_string_hex (block_a.work);
	return block;
}

std::unique_ptr<scendereapi::BlockChangeT> scendere::ipc::flatbuffers_builder::from (scendere::change_block const & block_a)
{
	auto block (std::make_unique<scendereapi::BlockChangeT> ());
	block->hash = block_a.hash ().to_string ();
	block->previous = block_a.previous ().to_string ();
	block->representative = block_a.representative ().to_account ();
	block_a.signature.encode_hex (block->signature);
	block->work = scendere::to_string_hex (block_a.work);
	return block;
}

scendereapi::BlockUnion scendere::ipc::flatbuffers_builder::block_to_union (scendere::block const & block_a, scendere::amount const & amount_a, bool is_state_send_a, bool is_state_epoch_a)
{
	scendereapi::BlockUnion u;
	switch (block_a.type ())
	{
		case scendere::block_type::state:
		{
			u.Set (*from (dynamic_cast<scendere::state_block const &> (block_a), amount_a, is_state_send_a, is_state_epoch_a));
			break;
		}
		case scendere::block_type::send:
		{
			u.Set (*from (dynamic_cast<scendere::send_block const &> (block_a)));
			break;
		}
		case scendere::block_type::receive:
		{
			u.Set (*from (dynamic_cast<scendere::receive_block const &> (block_a)));
			break;
		}
		case scendere::block_type::open:
		{
			u.Set (*from (dynamic_cast<scendere::open_block const &> (block_a)));
			break;
		}
		case scendere::block_type::change:
		{
			u.Set (*from (dynamic_cast<scendere::change_block const &> (block_a)));
			break;
		}

		default:
			debug_assert (false);
	}
	return u;
}
