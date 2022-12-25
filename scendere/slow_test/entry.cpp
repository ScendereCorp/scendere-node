#include <scendere/node/common.hpp>

#include <gtest/gtest.h>
namespace scendere
{
void cleanup_dev_directories_on_exit ();
void force_scendere_dev_network ();
}

int main (int argc, char ** argv)
{
	scendere::force_scendere_dev_network ();
	scendere::node_singleton_memory_pool_purge_guard memory_pool_cleanup_guard;
	testing::InitGoogleTest (&argc, argv);
	auto res = RUN_ALL_TESTS ();
	scendere::cleanup_dev_directories_on_exit ();
	return res;
}
