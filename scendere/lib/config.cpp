#include <scendere/lib/blocks.hpp>
#include <scendere/lib/config.hpp>

#include <boost/filesystem/path.hpp>
#include <boost/lexical_cast.hpp>

#include <valgrind/valgrind.h>

namespace
{
// useful for boost_lexical cast to allow conversion of hex strings
template <typename ElemT>
struct HexTo
{
	ElemT value;
	operator ElemT () const
	{
		return value;
	}
	friend std::istream & operator>> (std::istream & in, HexTo & out)
	{
		in >> std::hex >> out.value;
		return in;
	}
};
} // namespace

scendere::work_thresholds const scendere::work_thresholds::publish_full (
//0xffffffc000000000,
//0xfffffff800000000, // 8x higher than epoch_1
//0xfffffe0000000000 // 8x lower than epoch_1
//TO-CHANGE
0xf000000000000000, // 64x lower
0xf000000000000000, // 64x lower
0xf000000000000000	// 64x lower
);

scendere::work_thresholds const scendere::work_thresholds::publish_beta (
// 0xfffff00000000000, // 64x lower than publish_full.epoch_1
// 0xfffff00000000000, // same as epoch_1
// 0xffffe00000000000 // 2x lower than epoch_1
//TO-CHANGE
0xf000000000000000, // 64x lower
0xf000000000000000, // 64x lower
0xf000000000000000	// 64x lower
);

scendere::work_thresholds const scendere::work_thresholds::publish_dev (
// 0xfe00000000000000, // Very low for tests
// 0xffc0000000000000, // 8x higher than epoch_1
// 0xf000000000000000 // 8x lower than epoch_1
//TO-CHANGE
0xf000000000000000, // 64x lower
0xf000000000000000, // 64x lower
0xf000000000000000	// 64x lower
);

//TO-CHANGE
scendere::work_thresholds const scendere::work_thresholds::publish_test ( //defaults to live network levels
get_env_threshold_or_default ("SCENDERE_TEST_EPOCH_1", 0xf000000000000000),//0xffffffc000000000),
get_env_threshold_or_default ("SCENDERE_TEST_EPOCH_2",  0xf000000000000000),//0xfffffff800000000), // 8x higher than epoch_1
get_env_threshold_or_default ("SCENDERE_TEST_EPOCH_2_RECV",  0xf000000000000000)//0xfffffe0000000000) // 8x lower than epoch_1
);

uint64_t scendere::work_thresholds::threshold_entry (scendere::work_version const version_a, scendere::block_type const type_a) const
{
	uint64_t result{ std::numeric_limits<uint64_t>::max () };
	if (type_a == scendere::block_type::state)
	{
		switch (version_a)
		{
			case scendere::work_version::work_1:
				result = entry;
				break;
			default:
				debug_assert (false && "Invalid version specified to work_threshold_entry");
		}
	}
	else
	{
		result = epoch_1;
	}
	return result;
}

#ifndef SCENDERE_FUZZER_TEST
uint64_t scendere::work_thresholds::value (scendere::root const & root_a, uint64_t work_a) const
{
	uint64_t result;
	blake2b_state hash;
	blake2b_init (&hash, sizeof (result));
	blake2b_update (&hash, reinterpret_cast<uint8_t *> (&work_a), sizeof (work_a));
	blake2b_update (&hash, root_a.bytes.data (), root_a.bytes.size ());
	blake2b_final (&hash, reinterpret_cast<uint8_t *> (&result), sizeof (result));
	return result;
}
#else
uint64_t scendere::work_thresholds::value (scendere::root const & root_a, uint64_t work_a) const
{
	return base + 1;
}
#endif

uint64_t scendere::work_thresholds::threshold (scendere::block_details const & details_a) const
{
	static_assert (scendere::epoch::max == scendere::epoch::epoch_2, "work_v1::threshold is ill-defined");

	uint64_t result{ std::numeric_limits<uint64_t>::max () };
	switch (details_a.epoch)
	{
		case scendere::epoch::epoch_2:
			result = (details_a.is_receive || details_a.is_epoch) ? epoch_2_receive : epoch_2;
			break;
		case scendere::epoch::epoch_1:
		case scendere::epoch::epoch_0:
			result = epoch_1;
			break;
		default:
			debug_assert (false && "Invalid epoch specified to work_v1 ledger work_threshold");
	}
	return result;
}

uint64_t scendere::work_thresholds::threshold (scendere::work_version const version_a, scendere::block_details const details_a) const
{
	uint64_t result{ std::numeric_limits<uint64_t>::max () };
	switch (version_a)
	{
		case scendere::work_version::work_1:
			result = threshold (details_a);
			break;
		default:
			debug_assert (false && "Invalid version specified to ledger work_threshold");
	}
	return result;
}

double scendere::work_thresholds::normalized_multiplier (double const multiplier_a, uint64_t const threshold_a) const
{
	debug_assert (multiplier_a >= 1);
	auto multiplier (multiplier_a);
	/* Normalization rules
	ratio = multiplier of max work threshold (send epoch 2) from given threshold
	i.e. max = 0xfe00000000000000, given = 0xf000000000000000, ratio = 8.0
	normalized = (multiplier + (ratio - 1)) / ratio;
	Epoch 1
	multiplier	 | normalized
	1.0 		 | 1.0
	9.0 		 | 2.0
	25.0 		 | 4.0
	Epoch 2 (receive / epoch subtypes)
	multiplier	 | normalized
	1.0 		 | 1.0
	65.0 		 | 2.0
	241.0 		 | 4.0
	*/
	if (threshold_a == epoch_1 || threshold_a == epoch_2_receive)
	{
		auto ratio (scendere::difficulty::to_multiplier (epoch_2, threshold_a));
		debug_assert (ratio >= 1);
		multiplier = (multiplier + (ratio - 1.0)) / ratio;
		debug_assert (multiplier >= 1);
	}
	return multiplier;
}

double scendere::work_thresholds::denormalized_multiplier (double const multiplier_a, uint64_t const threshold_a) const
{
	debug_assert (multiplier_a >= 1);
	auto multiplier (multiplier_a);
	if (threshold_a == epoch_1 || threshold_a == epoch_2_receive)
	{
		auto ratio (scendere::difficulty::to_multiplier (epoch_2, threshold_a));
		debug_assert (ratio >= 1);
		multiplier = multiplier * ratio + 1.0 - ratio;
		debug_assert (multiplier >= 1);
	}
	return multiplier;
}

uint64_t scendere::work_thresholds::threshold_base (scendere::work_version const version_a) const
{
	uint64_t result{ std::numeric_limits<uint64_t>::max () };
	switch (version_a)
	{
		case scendere::work_version::work_1:
			result = base;
			break;
		default:
			debug_assert (false && "Invalid version specified to work_threshold_base");
	}
	return result;
}

uint64_t scendere::work_thresholds::difficulty (scendere::work_version const version_a, scendere::root const & root_a, uint64_t const work_a) const
{
	uint64_t result{ 0 };
	switch (version_a)
	{
		case scendere::work_version::work_1:
			result = value (root_a, work_a);
			break;
		default:
			debug_assert (false && "Invalid version specified to work_difficulty");
	}
	return result;
}

uint64_t scendere::work_thresholds::difficulty (scendere::block const & block_a) const
{
	return difficulty (block_a.work_version (), block_a.root (), block_a.block_work ());
}

bool scendere::work_thresholds::validate_entry (scendere::work_version const version_a, scendere::root const & root_a, uint64_t const work_a) const
{
	return difficulty (version_a, root_a, work_a) < threshold_entry (version_a, scendere::block_type::state);
}

bool scendere::work_thresholds::validate_entry (scendere::block const & block_a) const
{
	return difficulty (block_a) < threshold_entry (block_a.work_version (), block_a.type ());
}

namespace scendere
{
char const * network_constants::active_network_err_msg = "Invalid network. Valid values are live, test, beta and dev.";

uint8_t get_major_node_version ()
{
	return boost::numeric_cast<uint8_t> (boost::lexical_cast<int> (SCENDERE_MAJOR_VERSION_STRING));
}
uint8_t get_minor_node_version ()
{
	return boost::numeric_cast<uint8_t> (boost::lexical_cast<int> (SCENDERE_MINOR_VERSION_STRING));
}
uint8_t get_patch_node_version ()
{
	return boost::numeric_cast<uint8_t> (boost::lexical_cast<int> (SCENDERE_PATCH_VERSION_STRING));
}
uint8_t get_pre_release_node_version ()
{
	return boost::numeric_cast<uint8_t> (boost::lexical_cast<int> (SCENDERE_PRE_RELEASE_VERSION_STRING));
}

std::string get_env_or_default (char const * variable_name, std::string default_value)
{
	auto value = getenv (variable_name);
	return value ? value : default_value;
}

uint64_t get_env_threshold_or_default (char const * variable_name, uint64_t const default_value)
{
	auto * value = getenv (variable_name);
	return value ? boost::lexical_cast<HexTo<uint64_t>> (value) : default_value;
}

uint16_t test_node_port ()
{
	auto test_env = scendere::get_env_or_default ("SCENDERE_TEST_NODE_PORT", "17155");
	return boost::lexical_cast<uint16_t> (test_env);
}
uint16_t test_rpc_port ()
{
	auto test_env = scendere::get_env_or_default ("SCENDERE_TEST_RPC_PORT", "17156");
	return boost::lexical_cast<uint16_t> (test_env);
}
uint16_t test_ipc_port ()
{
	auto test_env = scendere::get_env_or_default ("SCENDERE_TEST_IPC_PORT", "17157");
	return boost::lexical_cast<uint16_t> (test_env);
}
uint16_t test_websocket_port ()
{
	auto test_env = scendere::get_env_or_default ("SCENDERE_TEST_WEBSOCKET_PORT", "17158");
	return boost::lexical_cast<uint16_t> (test_env);
}

std::array<uint8_t, 2> test_magic_number ()
{
	auto test_env = get_env_or_default ("SCENDERE_TEST_MAGIC_NUMBER", "RX");
	std::array<uint8_t, 2> ret;
	std::copy (test_env.begin (), test_env.end (), ret.data ());
	return ret;
}

void force_scendere_dev_network ()
{
	scendere::network_constants::set_active_network (scendere::networks::scendere_dev_network);
}

bool running_within_valgrind ()
{
	return (RUNNING_ON_VALGRIND > 0);
}

std::string get_node_toml_config_path (boost::filesystem::path const & data_path)
{
	return (data_path / "config-node.toml").string ();
}

std::string get_rpc_toml_config_path (boost::filesystem::path const & data_path)
{
	return (data_path / "config-rpc.toml").string ();
}

std::string get_qtwallet_toml_config_path (boost::filesystem::path const & data_path)
{
	return (data_path / "config-qtwallet.toml").string ();
}

std::string get_access_toml_config_path (boost::filesystem::path const & data_path)
{
	return (data_path / "config-access.toml").string ();
}

std::string get_tls_toml_config_path (boost::filesystem::path const & data_path)
{
	return (data_path / "config-tls.toml").string ();
}
} // namespace scendere
