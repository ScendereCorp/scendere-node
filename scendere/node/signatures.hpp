#pragma once

#include <scendere/lib/threading.hpp>
#include <scendere/lib/utility.hpp>

#include <atomic>
#include <future>
#include <mutex>

namespace scendere
{
class signature_check_set final
{
public:
	signature_check_set (std::size_t size, unsigned char const ** messages, std::size_t * message_lengths, unsigned char const ** pub_keys, unsigned char const ** signatures, int * verifications) :
		size (size), messages (messages), message_lengths (message_lengths), pub_keys (pub_keys), signatures (signatures), verifications (verifications)
	{
	}

	std::size_t size;
	unsigned char const ** messages;
	std::size_t * message_lengths;
	unsigned char const ** pub_keys;
	unsigned char const ** signatures;
	int * verifications;
};

/** Multi-threaded signature checker */
class signature_checker final
{
public:
	signature_checker (unsigned num_threads);
	~signature_checker ();
	void verify (signature_check_set &);
	void stop ();
	void flush ();

	static std::size_t constexpr batch_size = 256;

private:
	std::atomic<int> tasks_remaining{ 0 };
	std::atomic<bool> stopped{ false };
	scendere::thread_pool thread_pool;

	struct Task final
	{
		Task (scendere::signature_check_set & check, std::size_t pending) :
			check (check), pending (pending)
		{
		}
		~Task ()
		{
			release_assert (pending == 0);
		}
		scendere::signature_check_set & check;
		std::atomic<std::size_t> pending;
	};

	bool verify_batch (scendere::signature_check_set const & check_a, std::size_t index, std::size_t size);
	void verify_async (scendere::signature_check_set & check_a, std::size_t num_batches, std::promise<void> & promise);
	bool single_threaded () const;
};
}
