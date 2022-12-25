#include "gtest/gtest.h"

#include <scendere/node/common.hpp>
#include <scendere/node/logging.hpp>
#include <scendere/secure/utility.hpp>

#include <boost/filesystem/path.hpp>

namespace scendere
{
void cleanup_dev_directories_on_exit ();
void force_scendere_dev_network ();
}

GTEST_API_ int main (int argc, char ** argv)
{
	printf ("Running main() from core_test_main.cc\n");
	scendere::force_scendere_dev_network ();
	scendere::node_singleton_memory_pool_purge_guard memory_pool_cleanup_guard;
	// Setting up logging so that there aren't any piped to standard output.
	scendere::logging logging;
	logging.init (scendere::unique_path ());
	testing::InitGoogleTest (&argc, argv);
	auto res = RUN_ALL_TESTS ();
	scendere::cleanup_dev_directories_on_exit ();
	return res;
}
