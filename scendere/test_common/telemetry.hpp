#pragma once

#include <cstdint>

namespace scendere
{
class keypair;
class network_params;
class telemetry_data;

void compare_default_telemetry_response_data_excluding_signature (scendere::telemetry_data const & telemetry_data_a, scendere::network_params const & network_params_a, uint64_t bandwidth_limit_a, uint64_t active_difficulty_a);
void compare_default_telemetry_response_data (scendere::telemetry_data const & telemetry_data_a, scendere::network_params const & network_params_a, uint64_t bandwidth_limit_a, uint64_t active_difficulty_a, scendere::keypair const & node_id_a);
}