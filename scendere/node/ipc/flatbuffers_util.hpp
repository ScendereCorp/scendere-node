#pragma once

#include <scendere/ipc_flatbuffers_lib/generated/flatbuffers/scendereapi_generated.h>

#include <memory>

namespace scendere
{
class amount;
class block;
class send_block;
class receive_block;
class change_block;
class open_block;
class state_block;
namespace ipc
{
	/**
	 * Utilities to convert between blocks and Flatbuffers equivalents
	 */
	class flatbuffers_builder
	{
	public:
		static scendereapi::BlockUnion block_to_union (scendere::block const & block_a, scendere::amount const & amount_a, bool is_state_send_a = false, bool is_state_epoch_a = false);
		static std::unique_ptr<scendereapi::BlockStateT> from (scendere::state_block const & block_a, scendere::amount const & amount_a, bool is_state_send_a, bool is_state_epoch_a);
		static std::unique_ptr<scendereapi::BlockSendT> from (scendere::send_block const & block_a);
		static std::unique_ptr<scendereapi::BlockReceiveT> from (scendere::receive_block const & block_a);
		static std::unique_ptr<scendereapi::BlockOpenT> from (scendere::open_block const & block_a);
		static std::unique_ptr<scendereapi::BlockChangeT> from (scendere::change_block const & block_a);
	};
}
}
