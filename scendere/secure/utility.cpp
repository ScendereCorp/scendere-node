#include <scendere/lib/config.hpp>
#include <scendere/secure/utility.hpp>
#include <scendere/secure/working.hpp>

#include <boost/filesystem.hpp>

static std::vector<boost::filesystem::path> all_unique_paths;

boost::filesystem::path scendere::working_path (scendere::networks network)
{
	auto result (scendere::app_path ());
	switch (network)
	{
		case scendere::networks::invalid:
			release_assert (false);
			break;
		case scendere::networks::scendere_dev_network:
			result /= "ScendereDev";
			break;
		case scendere::networks::scendere_beta_network:
			result /= "ScendereBeta";
			break;
		case scendere::networks::scendere_live_network:
			result /= "Scendere";
			break;
		case scendere::networks::scendere_test_network:
			result /= "ScendereTest";
			break;
	}
	return result;
}

boost::filesystem::path scendere::unique_path (scendere::networks network)
{
	auto result (working_path (network) / boost::filesystem::unique_path ());
	all_unique_paths.push_back (result);
	return result;
}

void scendere::remove_temporary_directories ()
{
	for (auto & path : all_unique_paths)
	{
		boost::system::error_code ec;
		boost::filesystem::remove_all (path, ec);
		if (ec)
		{
			std::cerr << "Could not remove temporary directory: " << ec.message () << std::endl;
		}

		// lmdb creates a -lock suffixed file for its MDB_NOSUBDIR databases
		auto lockfile = path;
		lockfile += "-lock";
		boost::filesystem::remove (lockfile, ec);
		if (ec)
		{
			std::cerr << "Could not remove temporary lock file: " << ec.message () << std::endl;
		}
	}
}

namespace scendere
{
/** A wrapper for handling signals */
std::function<void ()> signal_handler_impl;
void signal_handler (int sig)
{
	if (signal_handler_impl != nullptr)
	{
		signal_handler_impl ();
	}
}
}
