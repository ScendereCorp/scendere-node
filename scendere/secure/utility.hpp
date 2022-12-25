#pragma once

#include <scendere/lib/config.hpp>

#include <boost/filesystem/path.hpp>

namespace scendere
{
// OS-specific way of finding a path to a home directory.
boost::filesystem::path working_path (scendere::networks network = scendere::network_constants::active_network);
// Get a unique path within the home directory, used for testing.
// Any directories created at this location will be removed when a test finishes.
boost::filesystem::path unique_path (scendere::networks network = scendere::network_constants::active_network);
// Remove all unique tmp directories created by the process
void remove_temporary_directories ();
// Generic signal handler declarations
extern std::function<void ()> signal_handler_impl;
void signal_handler (int sig);
}
