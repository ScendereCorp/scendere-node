#pragma once

#include <scendere/lib/numbers.hpp>
#include <scendere/node/ipc/flatbuffers_handler.hpp>
#include <scendere/node/wallet.hpp>
#include <scendere/rpc/rpc.hpp>

#include <boost/property_tree/ptree.hpp>

#include <functional>
#include <string>

namespace scendere
{
namespace ipc
{
	class ipc_server;
}
class node;
class node_rpc_config;

class json_handler : public std::enable_shared_from_this<scendere::json_handler>
{
public:
	json_handler (
	scendere::node &, scendere::node_rpc_config const &, std::string const &, std::function<void (std::string const &)> const &, std::function<void ()> stop_callback = [] () {});
	void process_request (bool unsafe = false);
	void account_balance ();
	void account_block_count ();
	void account_count ();
	void account_create ();
	void account_get ();
	void account_history ();
	void account_info ();
	void account_key ();
	void account_list ();
	void account_move ();
	void account_remove ();
	void account_representative ();
	void account_representative_set ();
	void account_weight ();
	void accounts_balances ();
	void accounts_representatives ();
	void accounts_create ();
	void accounts_frontiers ();
	void accounts_pending ();
	void accounts_receivable ();
	void active_difficulty ();
	void available_supply ();
	void block_info ();
	void block_confirm ();
	void blocks ();
	void blocks_info ();
	void block_account ();
	void block_count ();
	void block_create ();
	void block_hash ();
	void bootstrap ();
	void bootstrap_any ();
	void bootstrap_lazy ();
	void bootstrap_status ();
	void chain (bool = false);
	void confirmation_active ();
	void confirmation_history ();
	void confirmation_info ();
	void confirmation_quorum ();
	void confirmation_height_currently_processing ();
	void database_txn_tracker ();
	void delegators ();
	void delegators_count ();
	void deterministic_key ();
	void epoch_upgrade ();
	void frontiers ();
	void keepalive ();
	void key_create ();
	void key_expand ();
	void ledger ();
	void mscendere_to_raw (scendere::uint128_t = scendere::Mxrb_ratio);
	void mscendere_from_raw (scendere::uint128_t = scendere::Mxrb_ratio);
	void scendere_to_raw ();
	void raw_to_scendere ();
	void node_id ();
	void node_id_delete ();
	void password_change ();
	void password_enter ();
	void password_valid (bool = false);
	void peers ();
	void pending ();
	void pending_exists ();
	void receivable ();
	void receivable_exists ();
	void process ();
	void pruned_exists ();
	void receive ();
	void receive_minimum ();
	void receive_minimum_set ();
	void representatives ();
	void representatives_online ();
	void republish ();
	void search_pending ();
	void search_receivable ();
	void search_pending_all ();
	void search_receivable_all ();
	void send ();
	void sign ();
	void stats ();
	void stats_clear ();
	void stop ();
	void telemetry ();
	void unchecked ();
	void unchecked_clear ();
	void unchecked_get ();
	void unchecked_keys ();
	void unopened ();
	void uptime ();
	void validate_account_number ();
	void version ();
	void wallet_add ();
	void wallet_add_watch ();
	void wallet_balances ();
	void wallet_change_seed ();
	void wallet_contains ();
	void wallet_create ();
	void wallet_destroy ();
	void wallet_export ();
	void wallet_frontiers ();
	void wallet_history ();
	void wallet_info ();
	void wallet_key_valid ();
	void wallet_ledger ();
	void wallet_lock ();
	void wallet_pending ();
	void wallet_receivable ();
	void wallet_representative ();
	void wallet_representative_set ();
	void wallet_republish ();
	void wallet_seed ();
	void wallet_work_get ();
	void work_cancel ();
	void work_generate ();
	void work_get ();
	void work_peer_add ();
	void work_peers ();
	void work_peers_clear ();
	void work_set ();
	void work_validate ();
	std::string body;
	scendere::node & node;
	boost::property_tree::ptree request;
	std::function<void (std::string const &)> response;
	void response_errors ();
	std::error_code ec;
	std::string action;
	boost::property_tree::ptree response_l;
	std::shared_ptr<scendere::wallet> wallet_impl ();
	bool wallet_locked_impl (scendere::transaction const &, std::shared_ptr<scendere::wallet> const &);
	bool wallet_account_impl (scendere::transaction const &, std::shared_ptr<scendere::wallet> const &, scendere::account const &);
	scendere::account account_impl (std::string = "", std::error_code = scendere::error_common::bad_account_number);
	scendere::account_info account_info_impl (scendere::transaction const &, scendere::account const &);
	scendere::amount amount_impl ();
	std::shared_ptr<scendere::block> block_impl (bool = true);
	scendere::block_hash hash_impl (std::string = "hash");
	scendere::amount threshold_optional_impl ();
	uint64_t work_optional_impl ();
	uint64_t count_impl ();
	uint64_t count_optional_impl (uint64_t = std::numeric_limits<uint64_t>::max ());
	uint64_t offset_optional_impl (uint64_t = 0);
	uint64_t difficulty_optional_impl (scendere::work_version const);
	uint64_t difficulty_ledger (scendere::block const &);
	double multiplier_optional_impl (scendere::work_version const, uint64_t &);
	scendere::work_version work_version_optional_impl (scendere::work_version const default_a);
	bool enable_sign_hash{ false };
	std::function<void ()> stop_callback;
	scendere::node_rpc_config const & node_rpc_config;
	std::function<void ()> create_worker_task (std::function<void (std::shared_ptr<scendere::json_handler> const &)> const &);
};

class inprocess_rpc_handler final : public scendere::rpc_handler_interface
{
public:
	inprocess_rpc_handler (
	scendere::node & node_a, scendere::ipc::ipc_server & ipc_server_a, scendere::node_rpc_config const & node_rpc_config_a, std::function<void ()> stop_callback_a = [] () {}) :
		node (node_a),
		ipc_server (ipc_server_a),
		stop_callback (stop_callback_a),
		node_rpc_config (node_rpc_config_a)
	{
	}

	void process_request (std::string const &, std::string const & body_a, std::function<void (std::string const &)> response_a) override;
	void process_request_v2 (rpc_handler_request_params const & params_a, std::string const & body_a, std::function<void (std::shared_ptr<std::string> const &)> response_a) override;

	void stop () override
	{
		if (rpc)
		{
			rpc->stop ();
		}
	}

	void rpc_instance (scendere::rpc & rpc_a) override
	{
		rpc = rpc_a;
	}

private:
	scendere::node & node;
	scendere::ipc::ipc_server & ipc_server;
	boost::optional<scendere::rpc &> rpc;
	std::function<void ()> stop_callback;
	scendere::node_rpc_config const & node_rpc_config;
};
}
