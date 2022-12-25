#include <scendere/lib/rep_weights.hpp>
#include <scendere/lib/stats.hpp>
#include <scendere/lib/utility.hpp>
#include <scendere/lib/work.hpp>
#include <scendere/secure/common.hpp>
#include <scendere/secure/ledger.hpp>
#include <scendere/secure/store.hpp>

#include <crypto/cryptopp/words.h>

namespace
{
/**
 * Roll back the visited block
 */
class rollback_visitor : public scendere::block_visitor
{
public:
	rollback_visitor (scendere::write_transaction const & transaction_a, scendere::ledger & ledger_a, std::vector<std::shared_ptr<scendere::block>> & list_a) :
		transaction (transaction_a),
		ledger (ledger_a),
		list (list_a)
	{
	}
	virtual ~rollback_visitor () = default;
	void send_block (scendere::send_block const & block_a) override
	{
		auto hash (block_a.hash ());
		scendere::pending_info pending;
		scendere::pending_key key (block_a.hashables.destination, hash);
		while (!error && ledger.store.pending.get (transaction, key, pending))
		{
			error = ledger.rollback (transaction, ledger.latest (transaction, block_a.hashables.destination), list);
		}
		if (!error)
		{
			scendere::account_info info;
			[[maybe_unused]] auto error (ledger.store.account.get (transaction, pending.source, info));
			debug_assert (!error);
			ledger.store.pending.del (transaction, key);
			ledger.cache.rep_weights.representation_add (info.representative, pending.amount.number ());
			scendere::account_info new_info (block_a.hashables.previous, info.representative, info.open_block, ledger.balance (transaction, block_a.hashables.previous), scendere::seconds_since_epoch (), info.block_count - 1, scendere::epoch::epoch_0);
			ledger.update_account (transaction, pending.source, info, new_info);
			ledger.store.block.del (transaction, hash);
			ledger.store.frontier.del (transaction, hash);
			ledger.store.frontier.put (transaction, block_a.hashables.previous, pending.source);
			ledger.store.block.successor_clear (transaction, block_a.hashables.previous);
			ledger.stats.inc (scendere::stat::type::rollback, scendere::stat::detail::send);
		}
	}
	void receive_block (scendere::receive_block const & block_a) override
	{
		auto hash (block_a.hash ());
		auto amount (ledger.amount (transaction, hash));
		auto destination_account (ledger.account (transaction, hash));
		// Pending account entry can be incorrect if source block was pruned. But it's not affecting correct ledger processing
		[[maybe_unused]] bool is_pruned (false);
		auto source_account (ledger.account_safe (transaction, block_a.hashables.source, is_pruned));
		scendere::account_info info;
		[[maybe_unused]] auto error (ledger.store.account.get (transaction, destination_account, info));
		debug_assert (!error);
		ledger.cache.rep_weights.representation_add (info.representative, 0 - amount);
		scendere::account_info new_info (block_a.hashables.previous, info.representative, info.open_block, ledger.balance (transaction, block_a.hashables.previous), scendere::seconds_since_epoch (), info.block_count - 1, scendere::epoch::epoch_0);
		ledger.update_account (transaction, destination_account, info, new_info);
		ledger.store.block.del (transaction, hash);
		ledger.store.pending.put (transaction, scendere::pending_key (destination_account, block_a.hashables.source), { source_account, amount, scendere::epoch::epoch_0 });
		ledger.store.frontier.del (transaction, hash);
		ledger.store.frontier.put (transaction, block_a.hashables.previous, destination_account);
		ledger.store.block.successor_clear (transaction, block_a.hashables.previous);
		ledger.stats.inc (scendere::stat::type::rollback, scendere::stat::detail::receive);
	}
	void open_block (scendere::open_block const & block_a) override
	{
		auto hash (block_a.hash ());
		auto amount (ledger.amount (transaction, hash));
		auto destination_account (ledger.account (transaction, hash));
		// Pending account entry can be incorrect if source block was pruned. But it's not affecting correct ledger processing
		[[maybe_unused]] bool is_pruned (false);
		auto source_account (ledger.account_safe (transaction, block_a.hashables.source, is_pruned));
		ledger.cache.rep_weights.representation_add (block_a.representative (), 0 - amount);
		scendere::account_info new_info;
		ledger.update_account (transaction, destination_account, new_info, new_info);
		ledger.store.block.del (transaction, hash);
		ledger.store.pending.put (transaction, scendere::pending_key (destination_account, block_a.hashables.source), { source_account, amount, scendere::epoch::epoch_0 });
		ledger.store.frontier.del (transaction, hash);
		ledger.stats.inc (scendere::stat::type::rollback, scendere::stat::detail::open);
	}
	void change_block (scendere::change_block const & block_a) override
	{
		auto hash (block_a.hash ());
		auto rep_block (ledger.representative (transaction, block_a.hashables.previous));
		auto account (ledger.account (transaction, block_a.hashables.previous));
		scendere::account_info info;
		[[maybe_unused]] auto error (ledger.store.account.get (transaction, account, info));
		debug_assert (!error);
		auto balance (ledger.balance (transaction, block_a.hashables.previous));
		auto block = ledger.store.block.get (transaction, rep_block);
		release_assert (block != nullptr);
		auto representative = block->representative ();
		ledger.cache.rep_weights.representation_add_dual (block_a.representative (), 0 - balance, representative, balance);
		ledger.store.block.del (transaction, hash);
		scendere::account_info new_info (block_a.hashables.previous, representative, info.open_block, info.balance, scendere::seconds_since_epoch (), info.block_count - 1, scendere::epoch::epoch_0);
		ledger.update_account (transaction, account, info, new_info);
		ledger.store.frontier.del (transaction, hash);
		ledger.store.frontier.put (transaction, block_a.hashables.previous, account);
		ledger.store.block.successor_clear (transaction, block_a.hashables.previous);
		ledger.stats.inc (scendere::stat::type::rollback, scendere::stat::detail::change);
	}
	void state_block (scendere::state_block const & block_a) override
	{
		auto hash (block_a.hash ());
		scendere::block_hash rep_block_hash (0);
		if (!block_a.hashables.previous.is_zero ())
		{
			rep_block_hash = ledger.representative (transaction, block_a.hashables.previous);
		}
		auto balance (ledger.balance (transaction, block_a.hashables.previous));
		auto is_send (block_a.hashables.balance < balance);
		scendere::account representative{};
		if (!rep_block_hash.is_zero ())
		{
			// Move existing representation & add in amount delta
			auto block (ledger.store.block.get (transaction, rep_block_hash));
			debug_assert (block != nullptr);
			representative = block->representative ();
			ledger.cache.rep_weights.representation_add_dual (representative, balance, block_a.representative (), 0 - block_a.hashables.balance.number ());
		}
		else
		{
			// Add in amount delta only
			ledger.cache.rep_weights.representation_add (block_a.representative (), 0 - block_a.hashables.balance.number ());
		}

		scendere::account_info info;
		auto error (ledger.store.account.get (transaction, block_a.hashables.account, info));

		if (is_send)
		{
			scendere::pending_key key (block_a.hashables.link.as_account (), hash);
			while (!error && !ledger.store.pending.exists (transaction, key))
			{
				error = ledger.rollback (transaction, ledger.latest (transaction, block_a.hashables.link.as_account ()), list);
			}
			ledger.store.pending.del (transaction, key);
			ledger.stats.inc (scendere::stat::type::rollback, scendere::stat::detail::send);
		}
		else if (!block_a.hashables.link.is_zero () && !ledger.is_epoch_link (block_a.hashables.link))
		{
			// Pending account entry can be incorrect if source block was pruned. But it's not affecting correct ledger processing
			[[maybe_unused]] bool is_pruned (false);
			auto source_account (ledger.account_safe (transaction, block_a.hashables.link.as_block_hash (), is_pruned));
			scendere::pending_info pending_info (source_account, block_a.hashables.balance.number () - balance, block_a.sideband ().source_epoch);
			ledger.store.pending.put (transaction, scendere::pending_key (block_a.hashables.account, block_a.hashables.link.as_block_hash ()), pending_info);
			ledger.stats.inc (scendere::stat::type::rollback, scendere::stat::detail::receive);
		}

		debug_assert (!error);
		auto previous_version (ledger.store.block.version (transaction, block_a.hashables.previous));
		scendere::account_info new_info (block_a.hashables.previous, representative, info.open_block, balance, scendere::seconds_since_epoch (), info.block_count - 1, previous_version);
		ledger.update_account (transaction, block_a.hashables.account, info, new_info);

		auto previous (ledger.store.block.get (transaction, block_a.hashables.previous));
		if (previous != nullptr)
		{
			ledger.store.block.successor_clear (transaction, block_a.hashables.previous);
			if (previous->type () < scendere::block_type::state)
			{
				ledger.store.frontier.put (transaction, block_a.hashables.previous, block_a.hashables.account);
			}
		}
		else
		{
			ledger.stats.inc (scendere::stat::type::rollback, scendere::stat::detail::open);
		}
		ledger.store.block.del (transaction, hash);
	}
	scendere::write_transaction const & transaction;
	scendere::ledger & ledger;
	std::vector<std::shared_ptr<scendere::block>> & list;
	bool error{ false };
};

class ledger_processor : public scendere::mutable_block_visitor
{
public:
	ledger_processor (scendere::ledger &, scendere::write_transaction const &, scendere::signature_verification = scendere::signature_verification::unknown);
	virtual ~ledger_processor () = default;
	void send_block (scendere::send_block &) override;
	void receive_block (scendere::receive_block &) override;
	void open_block (scendere::open_block &) override;
	void change_block (scendere::change_block &) override;
	void state_block (scendere::state_block &) override;
	void state_block_impl (scendere::state_block &);
	void epoch_block_impl (scendere::state_block &);
	scendere::ledger & ledger;
	scendere::write_transaction const & transaction;
	scendere::signature_verification verification;
	scendere::process_return result;

private:
	bool validate_epoch_block (scendere::state_block const & block_a);
};

// Returns true if this block which has an epoch link is correctly formed.
bool ledger_processor::validate_epoch_block (scendere::state_block const & block_a)
{
	debug_assert (ledger.is_epoch_link (block_a.hashables.link));
	scendere::amount prev_balance (0);
	if (!block_a.hashables.previous.is_zero ())
	{
		result.code = ledger.store.block.exists (transaction, block_a.hashables.previous) ? scendere::process_result::progress : scendere::process_result::gap_previous;
		if (result.code == scendere::process_result::progress)
		{
			prev_balance = ledger.balance (transaction, block_a.hashables.previous);
		}
		else if (result.verified == scendere::signature_verification::unknown)
		{
			// Check for possible regular state blocks with epoch link (send subtype)
			if (validate_message (block_a.hashables.account, block_a.hash (), block_a.signature))
			{
				// Is epoch block signed correctly
				if (validate_message (ledger.epoch_signer (block_a.link ()), block_a.hash (), block_a.signature))
				{
					result.verified = scendere::signature_verification::invalid;
					result.code = scendere::process_result::bad_signature;
				}
				else
				{
					result.verified = scendere::signature_verification::valid_epoch;
				}
			}
			else
			{
				result.verified = scendere::signature_verification::valid;
			}
		}
	}
	return (block_a.hashables.balance == prev_balance);
}

void ledger_processor::state_block (scendere::state_block & block_a)
{
	result.code = scendere::process_result::progress;
	auto is_epoch_block = false;
	if (ledger.is_epoch_link (block_a.hashables.link))
	{
		// This function also modifies the result variable if epoch is mal-formed
		is_epoch_block = validate_epoch_block (block_a);
	}

	if (result.code == scendere::process_result::progress)
	{
		if (is_epoch_block)
		{
			epoch_block_impl (block_a);
		}
		else
		{
			state_block_impl (block_a);
		}
	}
}

void ledger_processor::state_block_impl (scendere::state_block & block_a)
{
	auto hash (block_a.hash ());
	auto existing (ledger.block_or_pruned_exists (transaction, hash));
	result.code = existing ? scendere::process_result::old : scendere::process_result::progress; // Have we seen this block before? (Unambiguous)
	if (result.code == scendere::process_result::progress)
	{
		// Validate block if not verified outside of ledger
		if (result.verified != scendere::signature_verification::valid)
		{
			result.code = validate_message (block_a.hashables.account, hash, block_a.signature) ? scendere::process_result::bad_signature : scendere::process_result::progress; // Is this block signed correctly (Unambiguous)
		}
		if (result.code == scendere::process_result::progress)
		{
			debug_assert (!validate_message (block_a.hashables.account, hash, block_a.signature));
			result.verified = scendere::signature_verification::valid;
			result.code = block_a.hashables.account.is_zero () ? scendere::process_result::opened_burn_account : scendere::process_result::progress; // Is this for the burn account? (Unambiguous)
			if (result.code == scendere::process_result::progress)
			{
				scendere::epoch epoch (scendere::epoch::epoch_0);
				scendere::epoch source_epoch (scendere::epoch::epoch_0);
				scendere::account_info info;
				scendere::amount amount (block_a.hashables.balance);
				auto is_send (false);
				auto is_receive (false);
				auto account_error (ledger.store.account.get (transaction, block_a.hashables.account, info));
				if (!account_error)
				{
					// Account already exists
					epoch = info.epoch ();
					result.previous_balance = info.balance;
					result.code = block_a.hashables.previous.is_zero () ? scendere::process_result::fork : scendere::process_result::progress; // Has this account already been opened? (Ambigious)
					if (result.code == scendere::process_result::progress)
					{
						result.code = ledger.store.block.exists (transaction, block_a.hashables.previous) ? scendere::process_result::progress : scendere::process_result::gap_previous; // Does the previous block exist in the ledger? (Unambigious)
						if (result.code == scendere::process_result::progress)
						{
							is_send = block_a.hashables.balance < info.balance;
							is_receive = !is_send && !block_a.hashables.link.is_zero ();
							amount = is_send ? (info.balance.number () - amount.number ()) : (amount.number () - info.balance.number ());
							result.code = block_a.hashables.previous == info.head ? scendere::process_result::progress : scendere::process_result::fork; // Is the previous block the account's head block? (Ambigious)
						}
					}
				}
				else
				{
					// Account does not yet exists
					result.previous_balance = 0;
					result.code = block_a.previous ().is_zero () ? scendere::process_result::progress : scendere::process_result::gap_previous; // Does the first block in an account yield 0 for previous() ? (Unambigious)
					if (result.code == scendere::process_result::progress)
					{
						is_receive = true;
						result.code = !block_a.hashables.link.is_zero () ? scendere::process_result::progress : scendere::process_result::gap_source; // Is the first block receiving from a send ? (Unambigious)
					}
				}
				if (result.code == scendere::process_result::progress)
				{
					if (!is_send)
					{
						if (!block_a.hashables.link.is_zero ())
						{
							result.code = ledger.block_or_pruned_exists (transaction, block_a.hashables.link.as_block_hash ()) ? scendere::process_result::progress : scendere::process_result::gap_source; // Have we seen the source block already? (Harmless)
							if (result.code == scendere::process_result::progress)
							{
								scendere::pending_key key (block_a.hashables.account, block_a.hashables.link.as_block_hash ());
								scendere::pending_info pending;
								result.code = ledger.store.pending.get (transaction, key, pending) ? scendere::process_result::unreceivable : scendere::process_result::progress; // Has this source already been received (Malformed)
								if (result.code == scendere::process_result::progress)
								{
									result.code = amount == pending.amount ? scendere::process_result::progress : scendere::process_result::balance_mismatch;
									source_epoch = pending.epoch;
									epoch = std::max (epoch, source_epoch);
								}
							}
						}
						else
						{
							// If there's no link, the balance must remain the same, only the representative can change
							result.code = amount.is_zero () ? scendere::process_result::progress : scendere::process_result::balance_mismatch;
						}
					}
				}
				if (result.code == scendere::process_result::progress)
				{
					scendere::block_details block_details (epoch, is_send, is_receive, false);
					result.code = ledger.constants.work.difficulty (block_a) >= ledger.constants.work.threshold (block_a.work_version (), block_details) ? scendere::process_result::progress : scendere::process_result::insufficient_work; // Does this block have sufficient work? (Malformed)
					if (result.code == scendere::process_result::progress)
					{
						ledger.stats.inc (scendere::stat::type::ledger, scendere::stat::detail::state_block);
						block_a.sideband_set (scendere::block_sideband (block_a.hashables.account /* unused */, 0, 0 /* unused */, info.block_count + 1, scendere::seconds_since_epoch (), block_details, source_epoch));
						ledger.store.block.put (transaction, hash, block_a);

						if (!info.head.is_zero ())
						{
							// Move existing representation & add in amount delta
							ledger.cache.rep_weights.representation_add_dual (info.representative, 0 - info.balance.number (), block_a.representative (), block_a.hashables.balance.number ());
						}
						else
						{
							// Add in amount delta only
							ledger.cache.rep_weights.representation_add (block_a.representative (), block_a.hashables.balance.number ());
						}

						if (is_send)
						{
							scendere::pending_key key (block_a.hashables.link.as_account (), hash);
							scendere::pending_info info (block_a.hashables.account, amount.number (), epoch);
							ledger.store.pending.put (transaction, key, info);
						}
						else if (!block_a.hashables.link.is_zero ())
						{
							ledger.store.pending.del (transaction, scendere::pending_key (block_a.hashables.account, block_a.hashables.link.as_block_hash ()));
						}

						scendere::account_info new_info (hash, block_a.representative (), info.open_block.is_zero () ? hash : info.open_block, block_a.hashables.balance, scendere::seconds_since_epoch (), info.block_count + 1, epoch);
						ledger.update_account (transaction, block_a.hashables.account, info, new_info);
						if (!ledger.store.frontier.get (transaction, info.head).is_zero ())
						{
							ledger.store.frontier.del (transaction, info.head);
						}
					}
				}
			}
		}
	}
}

void ledger_processor::epoch_block_impl (scendere::state_block & block_a)
{
	auto hash (block_a.hash ());
	auto existing (ledger.block_or_pruned_exists (transaction, hash));
	result.code = existing ? scendere::process_result::old : scendere::process_result::progress; // Have we seen this block before? (Unambiguous)
	if (result.code == scendere::process_result::progress)
	{
		// Validate block if not verified outside of ledger
		if (result.verified != scendere::signature_verification::valid_epoch)
		{
			result.code = validate_message (ledger.epoch_signer (block_a.hashables.link), hash, block_a.signature) ? scendere::process_result::bad_signature : scendere::process_result::progress; // Is this block signed correctly (Unambiguous)
		}
		if (result.code == scendere::process_result::progress)
		{
			debug_assert (!validate_message (ledger.epoch_signer (block_a.hashables.link), hash, block_a.signature));
			result.verified = scendere::signature_verification::valid_epoch;
			result.code = block_a.hashables.account.is_zero () ? scendere::process_result::opened_burn_account : scendere::process_result::progress; // Is this for the burn account? (Unambiguous)
			if (result.code == scendere::process_result::progress)
			{
				scendere::account_info info;
				auto account_error (ledger.store.account.get (transaction, block_a.hashables.account, info));
				if (!account_error)
				{
					// Account already exists
					result.previous_balance = info.balance;
					result.code = block_a.hashables.previous.is_zero () ? scendere::process_result::fork : scendere::process_result::progress; // Has this account already been opened? (Ambigious)
					if (result.code == scendere::process_result::progress)
					{
						result.code = block_a.hashables.previous == info.head ? scendere::process_result::progress : scendere::process_result::fork; // Is the previous block the account's head block? (Ambigious)
						if (result.code == scendere::process_result::progress)
						{
							result.code = block_a.hashables.representative == info.representative ? scendere::process_result::progress : scendere::process_result::representative_mismatch;
						}
					}
				}
				else
				{
					result.previous_balance = 0;
					result.code = block_a.hashables.representative.is_zero () ? scendere::process_result::progress : scendere::process_result::representative_mismatch;
					// Non-exisitng account should have pending entries
					if (result.code == scendere::process_result::progress)
					{
						bool pending_exists = ledger.store.pending.any (transaction, block_a.hashables.account);
						result.code = pending_exists ? scendere::process_result::progress : scendere::process_result::gap_epoch_open_pending;
					}
				}
				if (result.code == scendere::process_result::progress)
				{
					auto epoch = ledger.constants.epochs.epoch (block_a.hashables.link);
					// Must be an epoch for an unopened account or the epoch upgrade must be sequential
					auto is_valid_epoch_upgrade = account_error ? static_cast<std::underlying_type_t<scendere::epoch>> (epoch) > 0 : scendere::epochs::is_sequential (info.epoch (), epoch);
					result.code = is_valid_epoch_upgrade ? scendere::process_result::progress : scendere::process_result::block_position;
					if (result.code == scendere::process_result::progress)
					{
						result.code = block_a.hashables.balance == info.balance ? scendere::process_result::progress : scendere::process_result::balance_mismatch;
						if (result.code == scendere::process_result::progress)
						{
							scendere::block_details block_details (epoch, false, false, true);
							result.code = ledger.constants.work.difficulty (block_a) >= ledger.constants.work.threshold (block_a.work_version (), block_details) ? scendere::process_result::progress : scendere::process_result::insufficient_work; // Does this block have sufficient work? (Malformed)
							if (result.code == scendere::process_result::progress)
							{
								ledger.stats.inc (scendere::stat::type::ledger, scendere::stat::detail::epoch_block);
								block_a.sideband_set (scendere::block_sideband (block_a.hashables.account /* unused */, 0, 0 /* unused */, info.block_count + 1, scendere::seconds_since_epoch (), block_details, scendere::epoch::epoch_0 /* unused */));
								ledger.store.block.put (transaction, hash, block_a);
								scendere::account_info new_info (hash, block_a.representative (), info.open_block.is_zero () ? hash : info.open_block, info.balance, scendere::seconds_since_epoch (), info.block_count + 1, epoch);
								ledger.update_account (transaction, block_a.hashables.account, info, new_info);
								if (!ledger.store.frontier.get (transaction, info.head).is_zero ())
								{
									ledger.store.frontier.del (transaction, info.head);
								}
							}
						}
					}
				}
			}
		}
	}
}

void ledger_processor::change_block (scendere::change_block & block_a)
{
	auto hash (block_a.hash ());
	auto existing (ledger.block_or_pruned_exists (transaction, hash));
	result.code = existing ? scendere::process_result::old : scendere::process_result::progress; // Have we seen this block before? (Harmless)
	if (result.code == scendere::process_result::progress)
	{
		auto previous (ledger.store.block.get (transaction, block_a.hashables.previous));
		result.code = previous != nullptr ? scendere::process_result::progress : scendere::process_result::gap_previous; // Have we seen the previous block already? (Harmless)
		if (result.code == scendere::process_result::progress)
		{
			result.code = block_a.valid_predecessor (*previous) ? scendere::process_result::progress : scendere::process_result::block_position;
			if (result.code == scendere::process_result::progress)
			{
				auto account (ledger.store.frontier.get (transaction, block_a.hashables.previous));
				result.code = account.is_zero () ? scendere::process_result::fork : scendere::process_result::progress;
				if (result.code == scendere::process_result::progress)
				{
					scendere::account_info info;
					auto latest_error (ledger.store.account.get (transaction, account, info));
					(void)latest_error;
					debug_assert (!latest_error);
					debug_assert (info.head == block_a.hashables.previous);
					// Validate block if not verified outside of ledger
					if (result.verified != scendere::signature_verification::valid)
					{
						result.code = validate_message (account, hash, block_a.signature) ? scendere::process_result::bad_signature : scendere::process_result::progress; // Is this block signed correctly (Malformed)
					}
					if (result.code == scendere::process_result::progress)
					{
						scendere::block_details block_details (scendere::epoch::epoch_0, false /* unused */, false /* unused */, false /* unused */);
						result.code = ledger.constants.work.difficulty (block_a) >= ledger.constants.work.threshold (block_a.work_version (), block_details) ? scendere::process_result::progress : scendere::process_result::insufficient_work; // Does this block have sufficient work? (Malformed)
						if (result.code == scendere::process_result::progress)
						{
							debug_assert (!validate_message (account, hash, block_a.signature));
							result.verified = scendere::signature_verification::valid;
							block_a.sideband_set (scendere::block_sideband (account, 0, info.balance, info.block_count + 1, scendere::seconds_since_epoch (), block_details, scendere::epoch::epoch_0 /* unused */));
							ledger.store.block.put (transaction, hash, block_a);
							auto balance (ledger.balance (transaction, block_a.hashables.previous));
							ledger.cache.rep_weights.representation_add_dual (block_a.representative (), balance, info.representative, 0 - balance);
							scendere::account_info new_info (hash, block_a.representative (), info.open_block, info.balance, scendere::seconds_since_epoch (), info.block_count + 1, scendere::epoch::epoch_0);
							ledger.update_account (transaction, account, info, new_info);
							ledger.store.frontier.del (transaction, block_a.hashables.previous);
							ledger.store.frontier.put (transaction, hash, account);
							result.previous_balance = info.balance;
							ledger.stats.inc (scendere::stat::type::ledger, scendere::stat::detail::change);
						}
					}
				}
			}
		}
	}
}

void ledger_processor::send_block (scendere::send_block & block_a)
{
	auto hash (block_a.hash ());
	auto existing (ledger.block_or_pruned_exists (transaction, hash));
	result.code = existing ? scendere::process_result::old : scendere::process_result::progress; // Have we seen this block before? (Harmless)
	if (result.code == scendere::process_result::progress)
	{
		auto previous (ledger.store.block.get (transaction, block_a.hashables.previous));
		result.code = previous != nullptr ? scendere::process_result::progress : scendere::process_result::gap_previous; // Have we seen the previous block already? (Harmless)
		if (result.code == scendere::process_result::progress)
		{
			result.code = block_a.valid_predecessor (*previous) ? scendere::process_result::progress : scendere::process_result::block_position;
			if (result.code == scendere::process_result::progress)
			{
				auto account (ledger.store.frontier.get (transaction, block_a.hashables.previous));
				result.code = account.is_zero () ? scendere::process_result::fork : scendere::process_result::progress;
				if (result.code == scendere::process_result::progress)
				{
					// Validate block if not verified outside of ledger
					if (result.verified != scendere::signature_verification::valid)
					{
						result.code = validate_message (account, hash, block_a.signature) ? scendere::process_result::bad_signature : scendere::process_result::progress; // Is this block signed correctly (Malformed)
					}
					if (result.code == scendere::process_result::progress)
					{
						scendere::block_details block_details (scendere::epoch::epoch_0, false /* unused */, false /* unused */, false /* unused */);
						result.code = ledger.constants.work.difficulty (block_a) >= ledger.constants.work.threshold (block_a.work_version (), block_details) ? scendere::process_result::progress : scendere::process_result::insufficient_work; // Does this block have sufficient work? (Malformed)
						if (result.code == scendere::process_result::progress)
						{
							debug_assert (!validate_message (account, hash, block_a.signature));
							result.verified = scendere::signature_verification::valid;
							scendere::account_info info;
							auto latest_error (ledger.store.account.get (transaction, account, info));
							(void)latest_error;
							debug_assert (!latest_error);
							debug_assert (info.head == block_a.hashables.previous);
							result.code = info.balance.number () >= block_a.hashables.balance.number () ? scendere::process_result::progress : scendere::process_result::negative_spend; // Is this trying to spend a negative amount (Malicious)
							if (result.code == scendere::process_result::progress)
							{
								auto amount (info.balance.number () - block_a.hashables.balance.number ());
								ledger.cache.rep_weights.representation_add (info.representative, 0 - amount);
								block_a.sideband_set (scendere::block_sideband (account, 0, block_a.hashables.balance /* unused */, info.block_count + 1, scendere::seconds_since_epoch (), block_details, scendere::epoch::epoch_0 /* unused */));
								ledger.store.block.put (transaction, hash, block_a);
								scendere::account_info new_info (hash, info.representative, info.open_block, block_a.hashables.balance, scendere::seconds_since_epoch (), info.block_count + 1, scendere::epoch::epoch_0);
								ledger.update_account (transaction, account, info, new_info);
								ledger.store.pending.put (transaction, scendere::pending_key (block_a.hashables.destination, hash), { account, amount, scendere::epoch::epoch_0 });
								ledger.store.frontier.del (transaction, block_a.hashables.previous);
								ledger.store.frontier.put (transaction, hash, account);
								result.previous_balance = info.balance;
								ledger.stats.inc (scendere::stat::type::ledger, scendere::stat::detail::send);
							}
						}
					}
				}
			}
		}
	}
}

void ledger_processor::receive_block (scendere::receive_block & block_a)
{
	auto hash (block_a.hash ());
	auto existing (ledger.block_or_pruned_exists (transaction, hash));
	result.code = existing ? scendere::process_result::old : scendere::process_result::progress; // Have we seen this block already?  (Harmless)
	if (result.code == scendere::process_result::progress)
	{
		auto previous (ledger.store.block.get (transaction, block_a.hashables.previous));
		result.code = previous != nullptr ? scendere::process_result::progress : scendere::process_result::gap_previous;
		if (result.code == scendere::process_result::progress)
		{
			result.code = block_a.valid_predecessor (*previous) ? scendere::process_result::progress : scendere::process_result::block_position;
			if (result.code == scendere::process_result::progress)
			{
				auto account (ledger.store.frontier.get (transaction, block_a.hashables.previous));
				result.code = account.is_zero () ? scendere::process_result::gap_previous : scendere::process_result::progress; //Have we seen the previous block? No entries for account at all (Harmless)
				if (result.code == scendere::process_result::progress)
				{
					// Validate block if not verified outside of ledger
					if (result.verified != scendere::signature_verification::valid)
					{
						result.code = validate_message (account, hash, block_a.signature) ? scendere::process_result::bad_signature : scendere::process_result::progress; // Is the signature valid (Malformed)
					}
					if (result.code == scendere::process_result::progress)
					{
						debug_assert (!validate_message (account, hash, block_a.signature));
						result.verified = scendere::signature_verification::valid;
						result.code = ledger.block_or_pruned_exists (transaction, block_a.hashables.source) ? scendere::process_result::progress : scendere::process_result::gap_source; // Have we seen the source block already? (Harmless)
						if (result.code == scendere::process_result::progress)
						{
							scendere::account_info info;
							ledger.store.account.get (transaction, account, info);
							result.code = info.head == block_a.hashables.previous ? scendere::process_result::progress : scendere::process_result::gap_previous; // Block doesn't immediately follow latest block (Harmless)
							if (result.code == scendere::process_result::progress)
							{
								scendere::pending_key key (account, block_a.hashables.source);
								scendere::pending_info pending;
								result.code = ledger.store.pending.get (transaction, key, pending) ? scendere::process_result::unreceivable : scendere::process_result::progress; // Has this source already been received (Malformed)
								if (result.code == scendere::process_result::progress)
								{
									result.code = pending.epoch == scendere::epoch::epoch_0 ? scendere::process_result::progress : scendere::process_result::unreceivable; // Are we receiving a state-only send? (Malformed)
									if (result.code == scendere::process_result::progress)
									{
										scendere::block_details block_details (scendere::epoch::epoch_0, false /* unused */, false /* unused */, false /* unused */);
										result.code = ledger.constants.work.difficulty (block_a) >= ledger.constants.work.threshold (block_a.work_version (), block_details) ? scendere::process_result::progress : scendere::process_result::insufficient_work; // Does this block have sufficient work? (Malformed)
										if (result.code == scendere::process_result::progress)
										{
											auto new_balance (info.balance.number () + pending.amount.number ());
#ifdef NDEBUG
											if (ledger.store.block.exists (transaction, block_a.hashables.source))
											{
												scendere::account_info source_info;
												[[maybe_unused]] auto error (ledger.store.account.get (transaction, pending.source, source_info));
												debug_assert (!error);
											}
#endif
											ledger.store.pending.del (transaction, key);
											block_a.sideband_set (scendere::block_sideband (account, 0, new_balance, info.block_count + 1, scendere::seconds_since_epoch (), block_details, scendere::epoch::epoch_0 /* unused */));
											ledger.store.block.put (transaction, hash, block_a);
											scendere::account_info new_info (hash, info.representative, info.open_block, new_balance, scendere::seconds_since_epoch (), info.block_count + 1, scendere::epoch::epoch_0);
											ledger.update_account (transaction, account, info, new_info);
											ledger.cache.rep_weights.representation_add (info.representative, pending.amount.number ());
											ledger.store.frontier.del (transaction, block_a.hashables.previous);
											ledger.store.frontier.put (transaction, hash, account);
											result.previous_balance = info.balance;
											ledger.stats.inc (scendere::stat::type::ledger, scendere::stat::detail::receive);
										}
									}
								}
							}
						}
					}
				}
				else
				{
					result.code = ledger.store.block.exists (transaction, block_a.hashables.previous) ? scendere::process_result::fork : scendere::process_result::gap_previous; // If we have the block but it's not the latest we have a signed fork (Malicious)
				}
			}
		}
	}
}

void ledger_processor::open_block (scendere::open_block & block_a)
{
	auto hash (block_a.hash ());
	auto existing (ledger.block_or_pruned_exists (transaction, hash));
	result.code = existing ? scendere::process_result::old : scendere::process_result::progress; // Have we seen this block already? (Harmless)
	if (result.code == scendere::process_result::progress)
	{
		// Validate block if not verified outside of ledger
		if (result.verified != scendere::signature_verification::valid)
		{
			result.code = validate_message (block_a.hashables.account, hash, block_a.signature) ? scendere::process_result::bad_signature : scendere::process_result::progress; // Is the signature valid (Malformed)
		}
		if (result.code == scendere::process_result::progress)
		{
			debug_assert (!validate_message (block_a.hashables.account, hash, block_a.signature));
			result.verified = scendere::signature_verification::valid;
			result.code = ledger.block_or_pruned_exists (transaction, block_a.hashables.source) ? scendere::process_result::progress : scendere::process_result::gap_source; // Have we seen the source block? (Harmless)
			if (result.code == scendere::process_result::progress)
			{
				scendere::account_info info;
				result.code = ledger.store.account.get (transaction, block_a.hashables.account, info) ? scendere::process_result::progress : scendere::process_result::fork; // Has this account already been opened? (Malicious)
				if (result.code == scendere::process_result::progress)
				{
					scendere::pending_key key (block_a.hashables.account, block_a.hashables.source);
					scendere::pending_info pending;
					result.code = ledger.store.pending.get (transaction, key, pending) ? scendere::process_result::unreceivable : scendere::process_result::progress; // Has this source already been received (Malformed)
					if (result.code == scendere::process_result::progress)
					{
						result.code = block_a.hashables.account == ledger.constants.burn_account ? scendere::process_result::opened_burn_account : scendere::process_result::progress; // Is it burning 0 account? (Malicious)
						if (result.code == scendere::process_result::progress)
						{
							result.code = pending.epoch == scendere::epoch::epoch_0 ? scendere::process_result::progress : scendere::process_result::unreceivable; // Are we receiving a state-only send? (Malformed)
							if (result.code == scendere::process_result::progress)
							{
								scendere::block_details block_details (scendere::epoch::epoch_0, false /* unused */, false /* unused */, false /* unused */);
								result.code = ledger.constants.work.difficulty (block_a) >= ledger.constants.work.threshold (block_a.work_version (), block_details) ? scendere::process_result::progress : scendere::process_result::insufficient_work; // Does this block have sufficient work? (Malformed)
								if (result.code == scendere::process_result::progress)
								{
#ifdef NDEBUG
									if (ledger.store.block.exists (transaction, block_a.hashables.source))
									{
										scendere::account_info source_info;
										[[maybe_unused]] auto error (ledger.store.account.get (transaction, pending.source, source_info));
										debug_assert (!error);
									}
#endif
									ledger.store.pending.del (transaction, key);
									block_a.sideband_set (scendere::block_sideband (block_a.hashables.account, 0, pending.amount, 1, scendere::seconds_since_epoch (), block_details, scendere::epoch::epoch_0 /* unused */));
									ledger.store.block.put (transaction, hash, block_a);
									scendere::account_info new_info (hash, block_a.representative (), hash, pending.amount.number (), scendere::seconds_since_epoch (), 1, scendere::epoch::epoch_0);
									ledger.update_account (transaction, block_a.hashables.account, info, new_info);
									ledger.cache.rep_weights.representation_add (block_a.representative (), pending.amount.number ());
									ledger.store.frontier.put (transaction, hash, block_a.hashables.account);
									result.previous_balance = 0;
									ledger.stats.inc (scendere::stat::type::ledger, scendere::stat::detail::open);
								}
							}
						}
					}
				}
			}
		}
	}
}

ledger_processor::ledger_processor (scendere::ledger & ledger_a, scendere::write_transaction const & transaction_a, scendere::signature_verification verification_a) :
	ledger (ledger_a),
	transaction (transaction_a),
	verification (verification_a)
{
	result.verified = verification;
}
} // namespace

scendere::ledger::ledger (scendere::store & store_a, scendere::stat & stat_a, scendere::ledger_constants & constants, scendere::generate_cache const & generate_cache_a) :
	constants{ constants },
	store{ store_a },
	stats{ stat_a },
	check_bootstrap_weights{ true }
{
	if (!store.init_error ())
	{
		initialize (generate_cache_a);
	}
}

void scendere::ledger::initialize (scendere::generate_cache const & generate_cache_a)
{
	if (generate_cache_a.reps || generate_cache_a.account_count || generate_cache_a.block_count)
	{
		store.account.for_each_par (
		[this] (scendere::read_transaction const & /*unused*/, scendere::store_iterator<scendere::account, scendere::account_info> i, scendere::store_iterator<scendere::account, scendere::account_info> n) {
			uint64_t block_count_l{ 0 };
			uint64_t account_count_l{ 0 };
			decltype (this->cache.rep_weights) rep_weights_l;
			for (; i != n; ++i)
			{
				scendere::account_info const & info (i->second);
				block_count_l += info.block_count;
				++account_count_l;
				rep_weights_l.representation_add (info.representative, info.balance.number ());
			}
			this->cache.block_count += block_count_l;
			this->cache.account_count += account_count_l;
			this->cache.rep_weights.copy_from (rep_weights_l);
		});
	}

	if (generate_cache_a.cemented_count)
	{
		store.confirmation_height.for_each_par (
		[this] (scendere::read_transaction const & /*unused*/, scendere::store_iterator<scendere::account, scendere::confirmation_height_info> i, scendere::store_iterator<scendere::account, scendere::confirmation_height_info> n) {
			uint64_t cemented_count_l (0);
			for (; i != n; ++i)
			{
				cemented_count_l += i->second.height;
			}
			this->cache.cemented_count += cemented_count_l;
		});
	}

	auto transaction (store.tx_begin_read ());
	cache.pruned_count = store.pruned.count (transaction);

	// Final votes requirement for confirmation canary block
	scendere::confirmation_height_info confirmation_height_info;
	if (!store.confirmation_height.get (transaction, constants.final_votes_canary_account, confirmation_height_info))
	{
		cache.final_votes_confirmation_canary = (confirmation_height_info.height >= constants.final_votes_canary_height);
	}
}

// Balance for account containing hash
scendere::uint128_t scendere::ledger::balance (scendere::transaction const & transaction_a, scendere::block_hash const & hash_a) const
{
	return hash_a.is_zero () ? 0 : store.block.balance (transaction_a, hash_a);
}

scendere::uint128_t scendere::ledger::balance_safe (scendere::transaction const & transaction_a, scendere::block_hash const & hash_a, bool & error_a) const
{
	scendere::uint128_t result (0);
	if (pruning && !hash_a.is_zero () && !store.block.exists (transaction_a, hash_a))
	{
		error_a = true;
		result = 0;
	}
	else
	{
		result = balance (transaction_a, hash_a);
	}
	return result;
}

// Balance for an account by account number
scendere::uint128_t scendere::ledger::account_balance (scendere::transaction const & transaction_a, scendere::account const & account_a, bool only_confirmed_a)
{
	scendere::uint128_t result (0);
	if (only_confirmed_a)
	{
		scendere::confirmation_height_info info;
		if (!store.confirmation_height.get (transaction_a, account_a, info))
		{
			result = balance (transaction_a, info.frontier);
		}
	}
	else
	{
		scendere::account_info info;
		auto none (store.account.get (transaction_a, account_a, info));
		if (!none)
		{
			result = info.balance.number ();
		}
	}
	return result;
}

scendere::uint128_t scendere::ledger::account_receivable (scendere::transaction const & transaction_a, scendere::account const & account_a, bool only_confirmed_a)
{
	scendere::uint128_t result (0);
	scendere::account end (account_a.number () + 1);
	for (auto i (store.pending.begin (transaction_a, scendere::pending_key (account_a, 0))), n (store.pending.begin (transaction_a, scendere::pending_key (end, 0))); i != n; ++i)
	{
		scendere::pending_info const & info (i->second);
		if (only_confirmed_a)
		{
			if (block_confirmed (transaction_a, i->first.hash))
			{
				result += info.amount.number ();
			}
		}
		else
		{
			result += info.amount.number ();
		}
	}
	return result;
}

scendere::process_return scendere::ledger::process (scendere::write_transaction const & transaction_a, scendere::block & block_a, scendere::signature_verification verification)
{
	debug_assert (!constants.work.validate_entry (block_a) || constants.genesis == scendere::dev::genesis);
	ledger_processor processor (*this, transaction_a, verification);
	block_a.visit (processor);
	if (processor.result.code == scendere::process_result::progress)
	{
		++cache.block_count;
	}
	return processor.result;
}

scendere::block_hash scendere::ledger::representative (scendere::transaction const & transaction_a, scendere::block_hash const & hash_a)
{
	auto result (representative_calculated (transaction_a, hash_a));
	debug_assert (result.is_zero () || store.block.exists (transaction_a, result));
	return result;
}

scendere::block_hash scendere::ledger::representative_calculated (scendere::transaction const & transaction_a, scendere::block_hash const & hash_a)
{
	representative_visitor visitor (transaction_a, store);
	visitor.compute (hash_a);
	return visitor.result;
}

bool scendere::ledger::block_or_pruned_exists (scendere::block_hash const & hash_a) const
{
	return block_or_pruned_exists (store.tx_begin_read (), hash_a);
}

bool scendere::ledger::block_or_pruned_exists (scendere::transaction const & transaction_a, scendere::block_hash const & hash_a) const
{
	if (store.pruned.exists (transaction_a, hash_a))
	{
		return true;
	}
	return store.block.exists (transaction_a, hash_a);
}

std::string scendere::ledger::block_text (char const * hash_a)
{
	return block_text (scendere::block_hash (hash_a));
}

std::string scendere::ledger::block_text (scendere::block_hash const & hash_a)
{
	std::string result;
	auto transaction (store.tx_begin_read ());
	auto block (store.block.get (transaction, hash_a));
	if (block != nullptr)
	{
		block->serialize_json (result);
	}
	return result;
}

bool scendere::ledger::is_send (scendere::transaction const & transaction_a, scendere::state_block const & block_a) const
{
	/*
	 * if block_a does not have a sideband, then is_send()
	 * requires that the previous block exists in the database.
	 * This is because it must retrieve the balance of the previous block.
	 */
	debug_assert (block_a.has_sideband () || block_a.hashables.previous.is_zero () || store.block.exists (transaction_a, block_a.hashables.previous));

	bool result (false);
	if (block_a.has_sideband ())
	{
		result = block_a.sideband ().details.is_send;
	}
	else
	{
		scendere::block_hash previous (block_a.hashables.previous);
		if (!previous.is_zero ())
		{
			if (block_a.hashables.balance < balance (transaction_a, previous))
			{
				result = true;
			}
		}
	}
	return result;
}

scendere::account const & scendere::ledger::block_destination (scendere::transaction const & transaction_a, scendere::block const & block_a)
{
	scendere::send_block const * send_block (dynamic_cast<scendere::send_block const *> (&block_a));
	scendere::state_block const * state_block (dynamic_cast<scendere::state_block const *> (&block_a));
	if (send_block != nullptr)
	{
		return send_block->hashables.destination;
	}
	else if (state_block != nullptr && is_send (transaction_a, *state_block))
	{
		return state_block->hashables.link.as_account ();
	}

	return scendere::account::null ();
}

scendere::block_hash scendere::ledger::block_source (scendere::transaction const & transaction_a, scendere::block const & block_a)
{
	/*
	 * block_source() requires that the previous block of the block
	 * passed in exist in the database.  This is because it will try
	 * to check account balances to determine if it is a send block.
	 */
	debug_assert (block_a.previous ().is_zero () || store.block.exists (transaction_a, block_a.previous ()));

	// If block_a.source () is nonzero, then we have our source.
	// However, universal blocks will always return zero.
	scendere::block_hash result (block_a.source ());
	scendere::state_block const * state_block (dynamic_cast<scendere::state_block const *> (&block_a));
	if (state_block != nullptr && !is_send (transaction_a, *state_block))
	{
		result = state_block->hashables.link.as_block_hash ();
	}
	return result;
}

std::pair<scendere::block_hash, scendere::block_hash> scendere::ledger::hash_root_random (scendere::transaction const & transaction_a) const
{
	scendere::block_hash hash (0);
	scendere::root root (0);
	if (!pruning)
	{
		auto block (store.block.random (transaction_a));
		hash = block->hash ();
		root = block->root ();
	}
	else
	{
		uint64_t count (cache.block_count);
		release_assert (std::numeric_limits<CryptoPP::word32>::max () > count);
		auto region = static_cast<size_t> (scendere::random_pool::generate_word32 (0, static_cast<CryptoPP::word32> (count - 1)));
		// Pruned cache cannot guarantee that pruned blocks are already commited
		if (region < cache.pruned_count)
		{
			hash = store.pruned.random (transaction_a);
		}
		if (hash.is_zero ())
		{
			auto block (store.block.random (transaction_a));
			hash = block->hash ();
			root = block->root ();
		}
	}
	return std::make_pair (hash, root.as_block_hash ());
}

// Vote weight of an account
scendere::uint128_t scendere::ledger::weight (scendere::account const & account_a)
{
	if (check_bootstrap_weights.load ())
	{
		if (cache.block_count < bootstrap_weight_max_blocks)
		{
			auto weight = bootstrap_weights.find (account_a);
			if (weight != bootstrap_weights.end ())
			{
				return weight->second;
			}
		}
		else
		{
			check_bootstrap_weights = false;
		}
	}
	return cache.rep_weights.representation_get (account_a);
}

// Rollback blocks until `block_a' doesn't exist or it tries to penetrate the confirmation height
bool scendere::ledger::rollback (scendere::write_transaction const & transaction_a, scendere::block_hash const & block_a, std::vector<std::shared_ptr<scendere::block>> & list_a)
{
	debug_assert (store.block.exists (transaction_a, block_a));
	auto account_l (account (transaction_a, block_a));
	auto block_account_height (store.block.account_height (transaction_a, block_a));
	rollback_visitor rollback (transaction_a, *this, list_a);
	scendere::account_info account_info;
	auto error (false);
	while (!error && store.block.exists (transaction_a, block_a))
	{
		scendere::confirmation_height_info confirmation_height_info;
		store.confirmation_height.get (transaction_a, account_l, confirmation_height_info);
		if (block_account_height > confirmation_height_info.height)
		{
			auto latest_error = store.account.get (transaction_a, account_l, account_info);
			debug_assert (!latest_error);
			auto block (store.block.get (transaction_a, account_info.head));
			list_a.push_back (block);
			block->visit (rollback);
			error = rollback.error;
			if (!error)
			{
				--cache.block_count;
			}
		}
		else
		{
			error = true;
		}
	}
	return error;
}

bool scendere::ledger::rollback (scendere::write_transaction const & transaction_a, scendere::block_hash const & block_a)
{
	std::vector<std::shared_ptr<scendere::block>> rollback_list;
	return rollback (transaction_a, block_a, rollback_list);
}

// Return account containing hash
scendere::account scendere::ledger::account (scendere::transaction const & transaction_a, scendere::block_hash const & hash_a) const
{
	return store.block.account (transaction_a, hash_a);
}

scendere::account scendere::ledger::account_safe (scendere::transaction const & transaction_a, scendere::block_hash const & hash_a, bool & error_a) const
{
	if (!pruning)
	{
		return store.block.account (transaction_a, hash_a);
	}
	else
	{
		auto block (store.block.get (transaction_a, hash_a));
		if (block != nullptr)
		{
			return store.block.account_calculated (*block);
		}
		else
		{
			error_a = true;
			return 0;
		}
	}
}

// Return amount decrease or increase for block
scendere::uint128_t scendere::ledger::amount (scendere::transaction const & transaction_a, scendere::account const & account_a)
{
	release_assert (account_a == constants.genesis->account ());
	return scendere::dev::constants.genesis_amount;
}

scendere::uint128_t scendere::ledger::amount (scendere::transaction const & transaction_a, scendere::block_hash const & hash_a)
{
	auto block (store.block.get (transaction_a, hash_a));
	auto block_balance (balance (transaction_a, hash_a));
	auto previous_balance (balance (transaction_a, block->previous ()));
	return block_balance > previous_balance ? block_balance - previous_balance : previous_balance - block_balance;
}

scendere::uint128_t scendere::ledger::amount_safe (scendere::transaction const & transaction_a, scendere::block_hash const & hash_a, bool & error_a) const
{
	auto block (store.block.get (transaction_a, hash_a));
	debug_assert (block);
	auto block_balance (balance (transaction_a, hash_a));
	auto previous_balance (balance_safe (transaction_a, block->previous (), error_a));
	return error_a ? 0 : block_balance > previous_balance ? block_balance - previous_balance
														  : previous_balance - block_balance;
}

// Return latest block for account
scendere::block_hash scendere::ledger::latest (scendere::transaction const & transaction_a, scendere::account const & account_a)
{
	scendere::account_info info;
	auto latest_error (store.account.get (transaction_a, account_a, info));
	return latest_error ? 0 : info.head;
}

// Return latest root for account, account number if there are no blocks for this account.
scendere::root scendere::ledger::latest_root (scendere::transaction const & transaction_a, scendere::account const & account_a)
{
	scendere::account_info info;
	if (store.account.get (transaction_a, account_a, info))
	{
		return account_a;
	}
	else
	{
		return info.head;
	}
}

void scendere::ledger::dump_account_chain (scendere::account const & account_a, std::ostream & stream)
{
	auto transaction (store.tx_begin_read ());
	auto hash (latest (transaction, account_a));
	while (!hash.is_zero ())
	{
		auto block (store.block.get (transaction, hash));
		debug_assert (block != nullptr);
		stream << hash.to_string () << std::endl;
		hash = block->previous ();
	}
}

bool scendere::ledger::could_fit (scendere::transaction const & transaction_a, scendere::block const & block_a) const
{
	auto dependencies (dependent_blocks (transaction_a, block_a));
	return std::all_of (dependencies.begin (), dependencies.end (), [this, &transaction_a] (scendere::block_hash const & hash_a) {
		return hash_a.is_zero () || store.block.exists (transaction_a, hash_a);
	});
}

bool scendere::ledger::dependents_confirmed (scendere::transaction const & transaction_a, scendere::block const & block_a) const
{
	auto dependencies (dependent_blocks (transaction_a, block_a));
	return std::all_of (dependencies.begin (), dependencies.end (), [this, &transaction_a] (scendere::block_hash const & hash_a) {
		auto result (hash_a.is_zero ());
		if (!result)
		{
			result = block_confirmed (transaction_a, hash_a);
		}
		return result;
	});
}

bool scendere::ledger::is_epoch_link (scendere::link const & link_a) const
{
	return constants.epochs.is_epoch_link (link_a);
}

class dependent_block_visitor : public scendere::block_visitor
{
public:
	dependent_block_visitor (scendere::ledger const & ledger_a, scendere::transaction const & transaction_a) :
		ledger (ledger_a),
		transaction (transaction_a),
		result ({ 0, 0 })
	{
	}
	void send_block (scendere::send_block const & block_a) override
	{
		result[0] = block_a.previous ();
	}
	void receive_block (scendere::receive_block const & block_a) override
	{
		result[0] = block_a.previous ();
		result[1] = block_a.source ();
	}
	void open_block (scendere::open_block const & block_a) override
	{
		if (block_a.source () != ledger.constants.genesis->account ())
		{
			result[0] = block_a.source ();
		}
	}
	void change_block (scendere::change_block const & block_a) override
	{
		result[0] = block_a.previous ();
	}
	void state_block (scendere::state_block const & block_a) override
	{
		result[0] = block_a.hashables.previous;
		result[1] = block_a.hashables.link.as_block_hash ();
		// ledger.is_send will check the sideband first, if block_a has a loaded sideband the check that previous block exists can be skipped
		if (ledger.is_epoch_link (block_a.hashables.link) || ((block_a.has_sideband () || ledger.store.block.exists (transaction, block_a.hashables.previous)) && ledger.is_send (transaction, block_a)))
		{
			result[1].clear ();
		}
	}
	scendere::ledger const & ledger;
	scendere::transaction const & transaction;
	std::array<scendere::block_hash, 2> result;
};

std::array<scendere::block_hash, 2> scendere::ledger::dependent_blocks (scendere::transaction const & transaction_a, scendere::block const & block_a) const
{
	dependent_block_visitor visitor (*this, transaction_a);
	block_a.visit (visitor);
	return visitor.result;
}

/** Given the block hash of a send block, find the associated receive block that receives that send.
 *  The send block hash is not checked in any way, it is assumed to be correct.
 * @return Return the receive block on success and null on failure
 */
std::shared_ptr<scendere::block> scendere::ledger::find_receive_block_by_send_hash (scendere::transaction const & transaction, scendere::account const & destination, scendere::block_hash const & send_block_hash)
{
	std::shared_ptr<scendere::block> result;
	debug_assert (send_block_hash != 0);

	// get the cemented frontier
	scendere::confirmation_height_info info;
	if (store.confirmation_height.get (transaction, destination, info))
	{
		return nullptr;
	}
	auto possible_receive_block = store.block.get (transaction, info.frontier);

	// walk down the chain until the source field of a receive block matches the send block hash
	while (possible_receive_block != nullptr)
	{
		// if source is non-zero then it is a legacy receive or open block
		scendere::block_hash source = possible_receive_block->source ();

		// if source is zero then it could be a state block, which needs a different kind of access
		auto state_block = dynamic_cast<scendere::state_block const *> (possible_receive_block.get ());
		if (state_block != nullptr)
		{
			// we read the block from the database, so we expect it to have sideband
			debug_assert (state_block->has_sideband ());
			if (state_block->sideband ().details.is_receive)
			{
				source = state_block->hashables.link.as_block_hash ();
			}
		}

		if (send_block_hash == source)
		{
			// we have a match
			result = possible_receive_block;
			break;
		}

		possible_receive_block = store.block.get (transaction, possible_receive_block->previous ());
	}

	return result;
}

scendere::account const & scendere::ledger::epoch_signer (scendere::link const & link_a) const
{
	return constants.epochs.signer (constants.epochs.epoch (link_a));
}

scendere::link const & scendere::ledger::epoch_link (scendere::epoch epoch_a) const
{
	return constants.epochs.link (epoch_a);
}

void scendere::ledger::update_account (scendere::write_transaction const & transaction_a, scendere::account const & account_a, scendere::account_info const & old_a, scendere::account_info const & new_a)
{
	if (!new_a.head.is_zero ())
	{
		if (old_a.head.is_zero () && new_a.open_block == new_a.head)
		{
			++cache.account_count;
		}
		if (!old_a.head.is_zero () && old_a.epoch () != new_a.epoch ())
		{
			// store.account.put won't erase existing entries if they're in different tables
			store.account.del (transaction_a, account_a);
		}
		store.account.put (transaction_a, account_a, new_a);
	}
	else
	{
		debug_assert (!store.confirmation_height.exists (transaction_a, account_a));
		store.account.del (transaction_a, account_a);
		debug_assert (cache.account_count > 0);
		--cache.account_count;
	}
}

std::shared_ptr<scendere::block> scendere::ledger::successor (scendere::transaction const & transaction_a, scendere::qualified_root const & root_a)
{
	scendere::block_hash successor (0);
	auto get_from_previous = false;
	if (root_a.previous ().is_zero ())
	{
		scendere::account_info info;
		if (!store.account.get (transaction_a, root_a.root ().as_account (), info))
		{
			successor = info.open_block;
		}
		else
		{
			get_from_previous = true;
		}
	}
	else
	{
		get_from_previous = true;
	}

	if (get_from_previous)
	{
		successor = store.block.successor (transaction_a, root_a.previous ());
	}
	std::shared_ptr<scendere::block> result;
	if (!successor.is_zero ())
	{
		result = store.block.get (transaction_a, successor);
	}
	debug_assert (successor.is_zero () || result != nullptr);
	return result;
}

std::shared_ptr<scendere::block> scendere::ledger::forked_block (scendere::transaction const & transaction_a, scendere::block const & block_a)
{
	debug_assert (!store.block.exists (transaction_a, block_a.hash ()));
	auto root (block_a.root ());
	debug_assert (store.block.exists (transaction_a, root.as_block_hash ()) || store.account.exists (transaction_a, root.as_account ()));
	auto result (store.block.get (transaction_a, store.block.successor (transaction_a, root.as_block_hash ())));
	if (result == nullptr)
	{
		scendere::account_info info;
		auto error (store.account.get (transaction_a, root.as_account (), info));
		(void)error;
		debug_assert (!error);
		result = store.block.get (transaction_a, info.open_block);
		debug_assert (result != nullptr);
	}
	return result;
}

bool scendere::ledger::block_confirmed (scendere::transaction const & transaction_a, scendere::block_hash const & hash_a) const
{
	if (store.pruned.exists (transaction_a, hash_a))
	{
		return true;
	}
	auto block = store.block.get (transaction_a, hash_a);
	if (block)
	{
		scendere::confirmation_height_info confirmation_height_info;
		store.confirmation_height.get (transaction_a, block->account ().is_zero () ? block->sideband ().account : block->account (), confirmation_height_info);
		auto confirmed (confirmation_height_info.height >= block->sideband ().height);
		return confirmed;
	}
	return false;
}

uint64_t scendere::ledger::pruning_action (scendere::write_transaction & transaction_a, scendere::block_hash const & hash_a, uint64_t const batch_size_a)
{
	uint64_t pruned_count (0);
	scendere::block_hash hash (hash_a);
	while (!hash.is_zero () && hash != constants.genesis->hash ())
	{
		auto block (store.block.get (transaction_a, hash));
		if (block != nullptr)
		{
			store.block.del (transaction_a, hash);
			store.pruned.put (transaction_a, hash);
			hash = block->previous ();
			++pruned_count;
			++cache.pruned_count;
			if (pruned_count % batch_size_a == 0)
			{
				transaction_a.commit ();
				transaction_a.renew ();
			}
		}
		else if (store.pruned.exists (transaction_a, hash))
		{
			hash = 0;
		}
		else
		{
			hash = 0;
			release_assert (false && "Error finding block for pruning");
		}
	}
	return pruned_count;
}

std::multimap<uint64_t, scendere::uncemented_info, std::greater<>> scendere::ledger::unconfirmed_frontiers () const
{
	scendere::locked<std::multimap<uint64_t, scendere::uncemented_info, std::greater<>>> result;
	using result_t = decltype (result)::value_type;

	store.account.for_each_par ([this, &result] (scendere::read_transaction const & transaction_a, scendere::store_iterator<scendere::account, scendere::account_info> i, scendere::store_iterator<scendere::account, scendere::account_info> n) {
		result_t unconfirmed_frontiers_l;
		for (; i != n; ++i)
		{
			auto const & account (i->first);
			auto const & account_info (i->second);

			scendere::confirmation_height_info conf_height_info;
			this->store.confirmation_height.get (transaction_a, account, conf_height_info);

			if (account_info.block_count != conf_height_info.height)
			{
				// Always output as no confirmation height has been set on the account yet
				auto height_delta = account_info.block_count - conf_height_info.height;
				auto const & frontier = account_info.head;
				auto const & cemented_frontier = conf_height_info.frontier;
				unconfirmed_frontiers_l.emplace (std::piecewise_construct, std::forward_as_tuple (height_delta), std::forward_as_tuple (cemented_frontier, frontier, i->first));
			}
		}
		// Merge results
		auto result_locked = result.lock ();
		result_locked->insert (unconfirmed_frontiers_l.begin (), unconfirmed_frontiers_l.end ());
	});
	return result;
}

// A precondition is that the store is an LMDB store
bool scendere::ledger::migrate_lmdb_to_rocksdb (boost::filesystem::path const & data_path_a) const
{
	boost::system::error_code error_chmod;
	scendere::set_secure_perm_directory (data_path_a, error_chmod);
	auto rockdb_data_path = data_path_a / "rocksdb";
	boost::filesystem::remove_all (rockdb_data_path);

	scendere::logger_mt logger;
	auto error (false);

	// Open rocksdb database
	scendere::rocksdb_config rocksdb_config;
	rocksdb_config.enable = true;
	auto rocksdb_store = scendere::make_store (logger, data_path_a, scendere::dev::constants, false, true, rocksdb_config);

	if (!rocksdb_store->init_error ())
	{
		store.block.for_each_par (
		[&rocksdb_store] (scendere::read_transaction const & /*unused*/, auto i, auto n) {
			for (; i != n; ++i)
			{
				auto rocksdb_transaction (rocksdb_store->tx_begin_write ({}, { scendere::tables::blocks }));

				std::vector<uint8_t> vector;
				{
					scendere::vectorstream stream (vector);
					scendere::serialize_block (stream, *i->second.block);
					i->second.sideband.serialize (stream, i->second.block->type ());
				}
				rocksdb_store->block.raw_put (rocksdb_transaction, vector, i->first);
			}
		});

		store.pending.for_each_par (
		[&rocksdb_store] (scendere::read_transaction const & /*unused*/, auto i, auto n) {
			for (; i != n; ++i)
			{
				auto rocksdb_transaction (rocksdb_store->tx_begin_write ({}, { scendere::tables::pending }));
				rocksdb_store->pending.put (rocksdb_transaction, i->first, i->second);
			}
		});

		store.confirmation_height.for_each_par (
		[&rocksdb_store] (scendere::read_transaction const & /*unused*/, auto i, auto n) {
			for (; i != n; ++i)
			{
				auto rocksdb_transaction (rocksdb_store->tx_begin_write ({}, { scendere::tables::confirmation_height }));
				rocksdb_store->confirmation_height.put (rocksdb_transaction, i->first, i->second);
			}
		});

		store.account.for_each_par (
		[&rocksdb_store] (scendere::read_transaction const & /*unused*/, auto i, auto n) {
			for (; i != n; ++i)
			{
				auto rocksdb_transaction (rocksdb_store->tx_begin_write ({}, { scendere::tables::accounts }));
				rocksdb_store->account.put (rocksdb_transaction, i->first, i->second);
			}
		});

		store.frontier.for_each_par (
		[&rocksdb_store] (scendere::read_transaction const & /*unused*/, auto i, auto n) {
			for (; i != n; ++i)
			{
				auto rocksdb_transaction (rocksdb_store->tx_begin_write ({}, { scendere::tables::frontiers }));
				rocksdb_store->frontier.put (rocksdb_transaction, i->first, i->second);
			}
		});

		store.pruned.for_each_par (
		[&rocksdb_store] (scendere::read_transaction const & /*unused*/, auto i, auto n) {
			for (; i != n; ++i)
			{
				auto rocksdb_transaction (rocksdb_store->tx_begin_write ({}, { scendere::tables::pruned }));
				rocksdb_store->pruned.put (rocksdb_transaction, i->first);
			}
		});

		store.final_vote.for_each_par (
		[&rocksdb_store] (scendere::read_transaction const & /*unused*/, auto i, auto n) {
			for (; i != n; ++i)
			{
				auto rocksdb_transaction (rocksdb_store->tx_begin_write ({}, { scendere::tables::final_votes }));
				rocksdb_store->final_vote.put (rocksdb_transaction, i->first, i->second);
			}
		});

		auto lmdb_transaction (store.tx_begin_read ());
		auto version = store.version.get (lmdb_transaction);
		auto rocksdb_transaction (rocksdb_store->tx_begin_write ());
		rocksdb_store->version.put (rocksdb_transaction, version);

		for (auto i (store.online_weight.begin (lmdb_transaction)), n (store.online_weight.end ()); i != n; ++i)
		{
			rocksdb_store->online_weight.put (rocksdb_transaction, i->first, i->second);
		}

		for (auto i (store.peer.begin (lmdb_transaction)), n (store.peer.end ()); i != n; ++i)
		{
			rocksdb_store->peer.put (rocksdb_transaction, i->first);
		}

		// Compare counts
		error |= store.peer.count (lmdb_transaction) != rocksdb_store->peer.count (rocksdb_transaction);
		error |= store.pruned.count (lmdb_transaction) != rocksdb_store->pruned.count (rocksdb_transaction);
		error |= store.final_vote.count (lmdb_transaction) != rocksdb_store->final_vote.count (rocksdb_transaction);
		error |= store.online_weight.count (lmdb_transaction) != rocksdb_store->online_weight.count (rocksdb_transaction);
		error |= store.version.get (lmdb_transaction) != rocksdb_store->version.get (rocksdb_transaction);

		// For large tables a random key is used instead and makes sure it exists
		auto random_block (store.block.random (lmdb_transaction));
		error |= rocksdb_store->block.get (rocksdb_transaction, random_block->hash ()) == nullptr;

		auto account = random_block->account ().is_zero () ? random_block->sideband ().account : random_block->account ();
		scendere::account_info account_info;
		error |= rocksdb_store->account.get (rocksdb_transaction, account, account_info);

		// If confirmation height exists in the lmdb ledger for this account it should exist in the rocksdb ledger
		scendere::confirmation_height_info confirmation_height_info{};
		if (!store.confirmation_height.get (lmdb_transaction, account, confirmation_height_info))
		{
			error |= rocksdb_store->confirmation_height.get (rocksdb_transaction, account, confirmation_height_info);
		}
	}
	else
	{
		error = true;
	}
	return error;
}

scendere::uncemented_info::uncemented_info (scendere::block_hash const & cemented_frontier, scendere::block_hash const & frontier, scendere::account const & account) :
	cemented_frontier (cemented_frontier), frontier (frontier), account (account)
{
}

std::unique_ptr<scendere::container_info_component> scendere::collect_container_info (ledger & ledger, std::string const & name)
{
	auto count = ledger.bootstrap_weights_size.load ();
	auto sizeof_element = sizeof (decltype (ledger.bootstrap_weights)::value_type);
	auto composite = std::make_unique<container_info_composite> (name);
	composite->add_component (std::make_unique<container_info_leaf> (container_info{ "bootstrap_weights", count, sizeof_element }));
	composite->add_component (collect_container_info (ledger.cache.rep_weights, "rep_weights"));
	return composite;
}
