#pragma once

#include <scendere/lib/config.hpp>
#include <scendere/lib/locks.hpp>
#include <scendere/lib/numbers.hpp>
#include <scendere/lib/utility.hpp>

#include <boost/optional.hpp>
#include <boost/thread/thread.hpp>

#include <atomic>
#include <memory>

namespace scendere
{
std::string to_string (scendere::work_version const version_a);

class block;
class block_details;
enum class block_type : uint8_t;

class opencl_work;
class work_item final
{
public:
	work_item (scendere::work_version const version_a, scendere::root const & item_a, uint64_t difficulty_a, std::function<void (boost::optional<uint64_t> const &)> const & callback_a) :
		version (version_a), item (item_a), difficulty (difficulty_a), callback (callback_a)
	{
	}
	scendere::work_version const version;
	scendere::root const item;
	uint64_t const difficulty;
	std::function<void (boost::optional<uint64_t> const &)> const callback;
};
class work_pool final
{
public:
	work_pool (scendere::network_constants & network_constants, unsigned, std::chrono::nanoseconds = std::chrono::nanoseconds (0), std::function<boost::optional<uint64_t> (scendere::work_version const, scendere::root const &, uint64_t, std::atomic<int> &)> = nullptr);
	~work_pool ();
	void loop (uint64_t);
	void stop ();
	void cancel (scendere::root const &);
	void generate (scendere::work_version const, scendere::root const &, uint64_t, std::function<void (boost::optional<uint64_t> const &)>);
	boost::optional<uint64_t> generate (scendere::work_version const, scendere::root const &, uint64_t);
	// For tests only
	boost::optional<uint64_t> generate (scendere::root const &);
	boost::optional<uint64_t> generate (scendere::root const &, uint64_t);
	size_t size ();
	scendere::network_constants & network_constants;
	std::atomic<int> ticket;
	bool done;
	std::vector<boost::thread> threads;
	std::list<scendere::work_item> pending;
	scendere::mutex mutex{ mutex_identifier (mutexes::work_pool) };
	scendere::condition_variable producer_condition;
	std::chrono::nanoseconds pow_rate_limiter;
	std::function<boost::optional<uint64_t> (scendere::work_version const, scendere::root const &, uint64_t, std::atomic<int> &)> opencl;
	scendere::observer_set<bool> work_observers;
};

std::unique_ptr<container_info_component> collect_container_info (work_pool & work_pool, std::string const & name);
}
