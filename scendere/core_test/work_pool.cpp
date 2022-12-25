#include <scendere/crypto_lib/random_pool.hpp>
#include <scendere/lib/blocks.hpp>
#include <scendere/lib/logger_mt.hpp>
#include <scendere/lib/timer.hpp>
#include <scendere/lib/work.hpp>
#include <scendere/node/logging.hpp>
#include <scendere/node/openclconfig.hpp>
#include <scendere/node/openclwork.hpp>
#include <scendere/secure/common.hpp>
#include <scendere/secure/utility.hpp>

#include <gtest/gtest.h>

#include <future>

TEST (work, one)
{
	scendere::work_pool pool{ scendere::dev::network_params.network, std::numeric_limits<unsigned>::max () };
	scendere::change_block block (1, 1, scendere::keypair ().prv, 3, 4);
	block.block_work_set (*pool.generate (block.root ()));
	ASSERT_LT (scendere::dev::network_params.work.threshold_base (block.work_version ()), scendere::dev::network_params.work.difficulty (block));
}

TEST (work, disabled)
{
	scendere::work_pool pool{ scendere::dev::network_params.network, 0 };
	auto result (pool.generate (scendere::block_hash ()));
	ASSERT_FALSE (result.is_initialized ());
}

TEST (work, validate)
{
	scendere::work_pool pool{ scendere::dev::network_params.network, std::numeric_limits<unsigned>::max () };
	scendere::send_block send_block (1, 1, 2, scendere::keypair ().prv, 4, 6);
	ASSERT_LT (scendere::dev::network_params.work.difficulty (send_block), scendere::dev::network_params.work.threshold_base (send_block.work_version ()));
	send_block.block_work_set (*pool.generate (send_block.root ()));
	ASSERT_LT (scendere::dev::network_params.work.threshold_base (send_block.work_version ()), scendere::dev::network_params.work.difficulty (send_block));
}

TEST (work, cancel)
{
	scendere::work_pool pool{ scendere::dev::network_params.network, std::numeric_limits<unsigned>::max () };
	auto iterations (0);
	auto done (false);
	while (!done)
	{
		scendere::root key (1);
		pool.generate (
		scendere::work_version::work_1, key, scendere::dev::network_params.work.base, [&done] (boost::optional<uint64_t> work_a) {
			done = !work_a;
		});
		pool.cancel (key);
		++iterations;
		ASSERT_LT (iterations, 200);
	}
}

TEST (work, cancel_many)
{
	scendere::work_pool pool{ scendere::dev::network_params.network, std::numeric_limits<unsigned>::max () };
	scendere::root key1 (1);
	scendere::root key2 (2);
	scendere::root key3 (1);
	scendere::root key4 (1);
	scendere::root key5 (3);
	scendere::root key6 (1);
	pool.generate (scendere::work_version::work_1, key1, scendere::dev::network_params.work.base, [] (boost::optional<uint64_t>) {});
	pool.generate (scendere::work_version::work_1, key2, scendere::dev::network_params.work.base, [] (boost::optional<uint64_t>) {});
	pool.generate (scendere::work_version::work_1, key3, scendere::dev::network_params.work.base, [] (boost::optional<uint64_t>) {});
	pool.generate (scendere::work_version::work_1, key4, scendere::dev::network_params.work.base, [] (boost::optional<uint64_t>) {});
	pool.generate (scendere::work_version::work_1, key5, scendere::dev::network_params.work.base, [] (boost::optional<uint64_t>) {});
	pool.generate (scendere::work_version::work_1, key6, scendere::dev::network_params.work.base, [] (boost::optional<uint64_t>) {});
	pool.cancel (key1);
}

TEST (work, opencl)
{
	scendere::logging logging;
	logging.init (scendere::unique_path ());
	scendere::logger_mt logger;
	bool error (false);
	scendere::opencl_environment environment (error);
	ASSERT_TRUE (!error || !scendere::opencl_loaded);
	if (!environment.platforms.empty () && !environment.platforms.begin ()->devices.empty ())
	{
		scendere::opencl_config config (0, 0, 16 * 1024);
		auto opencl (scendere::opencl_work::create (true, config, logger, scendere::dev::network_params.work));
		if (opencl != nullptr)
		{
			// 0 threads, should add 1 for managing OpenCL
			scendere::work_pool pool{ scendere::dev::network_params.network, 0, std::chrono::nanoseconds (0), [&opencl] (scendere::work_version const version_a, scendere::root const & root_a, uint64_t difficulty_a, std::atomic<int> & ticket_a) {
									 return opencl->generate_work (version_a, root_a, difficulty_a);
								 } };
			ASSERT_NE (nullptr, pool.opencl);
			scendere::root root;
			uint64_t difficulty (0xff00000000000000);
			uint64_t difficulty_add (0x000f000000000000);
			for (auto i (0); i < 16; ++i)
			{
				scendere::random_pool::generate_block (root.bytes.data (), root.bytes.size ());
				auto result (*pool.generate (scendere::work_version::work_1, root, difficulty));
				ASSERT_GE (scendere::dev::network_params.work.difficulty (scendere::work_version::work_1, root, result), difficulty);
				difficulty += difficulty_add;
			}
		}
		else
		{
			std::cerr << "Error starting OpenCL test" << std::endl;
		}
	}
	else
	{
		std::cout << "Device with OpenCL support not found. Skipping OpenCL test" << std::endl;
	}
}

TEST (work, difficulty)
{
	scendere::work_pool pool{ scendere::dev::network_params.network, std::numeric_limits<unsigned>::max () };
	scendere::root root (1);
	uint64_t difficulty1 (0xff00000000000000);
	uint64_t difficulty2 (0xfff0000000000000);
	uint64_t difficulty3 (0xffff000000000000);
	uint64_t result_difficulty1 (0);
	do
	{
		auto work1 = *pool.generate (scendere::work_version::work_1, root, difficulty1);
		result_difficulty1 = scendere::dev::network_params.work.difficulty (scendere::work_version::work_1, root, work1);
	} while (result_difficulty1 > difficulty2);
	ASSERT_GT (result_difficulty1, difficulty1);
	uint64_t result_difficulty2 (0);
	do
	{
		auto work2 = *pool.generate (scendere::work_version::work_1, root, difficulty2);
		result_difficulty2 = scendere::dev::network_params.work.difficulty (scendere::work_version::work_1, root, work2);
	} while (result_difficulty2 > difficulty3);
	ASSERT_GT (result_difficulty2, difficulty2);
}

TEST (work, eco_pow)
{
	auto work_func = [] (std::promise<std::chrono::nanoseconds> & promise, std::chrono::nanoseconds interval) {
		scendere::work_pool pool{ scendere::dev::network_params.network, 1, interval };
		constexpr auto num_iterations = 5;

		scendere::timer<std::chrono::nanoseconds> timer;
		timer.start ();
		for (int i = 0; i < num_iterations; ++i)
		{
			scendere::root root (1);
			uint64_t difficulty1 (0xff00000000000000);
			uint64_t difficulty2 (0xfff0000000000000);
			uint64_t result_difficulty (0);
			do
			{
				auto work = *pool.generate (scendere::work_version::work_1, root, difficulty1);
				result_difficulty = scendere::dev::network_params.work.difficulty (scendere::work_version::work_1, root, work);
			} while (result_difficulty > difficulty2);
			ASSERT_GT (result_difficulty, difficulty1);
		}

		promise.set_value_at_thread_exit (timer.stop ());
	};

	std::promise<std::chrono::nanoseconds> promise1;
	std::future<std::chrono::nanoseconds> future1 = promise1.get_future ();
	std::promise<std::chrono::nanoseconds> promise2;
	std::future<std::chrono::nanoseconds> future2 = promise2.get_future ();

	std::thread thread1 (work_func, std::ref (promise1), std::chrono::nanoseconds (0));
	std::thread thread2 (work_func, std::ref (promise2), std::chrono::milliseconds (10));

	thread1.join ();
	thread2.join ();

	// Confirm that the eco pow rate limiter is working.
	// It's possible under some unlucky circumstances that this fails to the random nature of valid work generation.
	ASSERT_LT (future1.get (), future2.get ());
}
