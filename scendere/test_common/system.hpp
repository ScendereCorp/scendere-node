#pragma once

#include <scendere/lib/errors.hpp>
#include <scendere/node/node.hpp>

#include <chrono>

namespace scendere
{
/** Test-system related error codes */
enum class error_system
{
	generic = 1,
	deadline_expired
};
class system final
{
public:
	system ();
	system (uint16_t, scendere::transport::transport_type = scendere::transport::transport_type::tcp, scendere::node_flags = scendere::node_flags ());
	~system ();
	void ledger_initialization_set (std::vector<scendere::keypair> const & reps, scendere::amount const & reserve = 0);
	void generate_activity (scendere::node &, std::vector<scendere::account> &);
	void generate_mass_activity (uint32_t, scendere::node &);
	void generate_usage_traffic (uint32_t, uint32_t, size_t);
	void generate_usage_traffic (uint32_t, uint32_t);
	scendere::account get_random_account (std::vector<scendere::account> &);
	scendere::uint128_t get_random_amount (scendere::transaction const &, scendere::node &, scendere::account const &);
	void generate_rollback (scendere::node &, std::vector<scendere::account> &);
	void generate_change_known (scendere::node &, std::vector<scendere::account> &);
	void generate_change_unknown (scendere::node &, std::vector<scendere::account> &);
	void generate_receive (scendere::node &);
	void generate_send_new (scendere::node &, std::vector<scendere::account> &);
	void generate_send_existing (scendere::node &, std::vector<scendere::account> &);
	std::unique_ptr<scendere::state_block> upgrade_genesis_epoch (scendere::node &, scendere::epoch const);
	std::shared_ptr<scendere::wallet> wallet (size_t);
	scendere::account account (scendere::transaction const &, size_t);
	/** Generate work with difficulty between \p min_difficulty_a (inclusive) and \p max_difficulty_a (exclusive) */
	uint64_t work_generate_limited (scendere::block_hash const & root_a, uint64_t min_difficulty_a, uint64_t max_difficulty_a);
	/**
	 * Polls, sleep if there's no work to be done (default 50ms), then check the deadline
	 * @returns 0 or scendere::deadline_expired
	 */
	std::error_code poll (std::chrono::nanoseconds const & sleep_time = std::chrono::milliseconds (50));
	std::error_code poll_until_true (std::chrono::nanoseconds deadline, std::function<bool ()>);
	void delay_ms (std::chrono::milliseconds const & delay);
	void stop ();
	void deadline_set (std::chrono::duration<double, std::nano> const & delta);
	std::shared_ptr<scendere::node> add_node (scendere::node_flags = scendere::node_flags (), scendere::transport::transport_type = scendere::transport::transport_type::tcp);
	std::shared_ptr<scendere::node> add_node (scendere::node_config const &, scendere::node_flags = scendere::node_flags (), scendere::transport::transport_type = scendere::transport::transport_type::tcp);
	boost::asio::io_context io_ctx;
	std::vector<std::shared_ptr<scendere::node>> nodes;
	scendere::logging logging;
	scendere::work_pool work{ scendere::dev::network_params.network, std::max (std::thread::hardware_concurrency (), 1u) };
	std::chrono::time_point<std::chrono::steady_clock, std::chrono::duration<double>> deadline{ std::chrono::steady_clock::time_point::max () };
	double deadline_scaling_factor{ 1.0 };
	unsigned node_sequence{ 0 };
	std::vector<std::shared_ptr<scendere::block>> initialization_blocks;
};
std::unique_ptr<scendere::state_block> upgrade_epoch (scendere::work_pool &, scendere::ledger &, scendere::epoch);
void blocks_confirm (scendere::node &, std::vector<std::shared_ptr<scendere::block>> const &, bool const = false);
uint16_t get_available_port ();
void cleanup_dev_directories_on_exit ();
}
REGISTER_ERROR_CODES (scendere, error_system);
