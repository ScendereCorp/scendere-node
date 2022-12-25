#pragma once

#include <scendere/lib/work.hpp>
#include <scendere/node/openclconfig.hpp>
#include <scendere/node/xorshift.hpp>

#include <boost/optional.hpp>

#include <atomic>
#include <mutex>
#include <vector>

#ifdef __APPLE__
#define CL_SILENCE_DEPRECATION
#include <OpenCL/opencl.h>
#else
#define CL_USE_DEPRECATED_OPENCL_1_2_APIS
#include <CL/cl.h>
#endif

namespace scendere
{
extern bool opencl_loaded;
class logger_mt;
class opencl_platform
{
public:
	cl_platform_id platform;
	std::vector<cl_device_id> devices;
};
class opencl_environment
{
public:
	opencl_environment (bool &);
	void dump (std::ostream & stream);
	std::vector<scendere::opencl_platform> platforms;
};
class root;
class work_pool;
class opencl_work
{
public:
	opencl_work (bool &, scendere::opencl_config const &, scendere::opencl_environment &, scendere::logger_mt &, scendere::work_thresholds & work);
	~opencl_work ();
	boost::optional<uint64_t> generate_work (scendere::work_version const, scendere::root const &, uint64_t const);
	boost::optional<uint64_t> generate_work (scendere::work_version const, scendere::root const &, uint64_t const, std::atomic<int> &);
	static std::unique_ptr<opencl_work> create (bool, scendere::opencl_config const &, scendere::logger_mt &, scendere::work_thresholds & work);
	scendere::opencl_config const & config;
	scendere::mutex mutex;
	cl_context context;
	cl_mem attempt_buffer;
	cl_mem result_buffer;
	cl_mem item_buffer;
	cl_mem difficulty_buffer;
	cl_program program;
	cl_kernel kernel;
	cl_command_queue queue;
	scendere::xorshift1024star rand;
	scendere::logger_mt & logger;
	scendere::work_thresholds & work;
};
}
