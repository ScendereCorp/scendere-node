#include <scendere/crypto_lib/random_pool.hpp>
#include <scendere/lib/locks.hpp>
#include <scendere/secure/buffer.hpp>
#include <scendere/secure/common.hpp>
#include <scendere/secure/network_filter.hpp>

scendere::network_filter::network_filter (size_t size_a) :
	items (size_a, scendere::uint128_t{ 0 })
{
	scendere::random_pool::generate_block (key, key.size ());
}

bool scendere::network_filter::apply (uint8_t const * bytes_a, size_t count_a, scendere::uint128_t * digest_a)
{
	// Get hash before locking
	auto digest (hash (bytes_a, count_a));

	scendere::lock_guard<scendere::mutex> lock (mutex);
	auto & element (get_element (digest));
	bool existed (element == digest);
	if (!existed)
	{
		// Replace likely old element with a new one
		element = digest;
	}
	if (digest_a)
	{
		*digest_a = digest;
	}
	return existed;
}

void scendere::network_filter::clear (scendere::uint128_t const & digest_a)
{
	scendere::lock_guard<scendere::mutex> lock (mutex);
	auto & element (get_element (digest_a));
	if (element == digest_a)
	{
		element = scendere::uint128_t{ 0 };
	}
}

void scendere::network_filter::clear (std::vector<scendere::uint128_t> const & digests_a)
{
	scendere::lock_guard<scendere::mutex> lock (mutex);
	for (auto const & digest : digests_a)
	{
		auto & element (get_element (digest));
		if (element == digest)
		{
			element = scendere::uint128_t{ 0 };
		}
	}
}

void scendere::network_filter::clear (uint8_t const * bytes_a, size_t count_a)
{
	clear (hash (bytes_a, count_a));
}

template <typename OBJECT>
void scendere::network_filter::clear (OBJECT const & object_a)
{
	clear (hash (object_a));
}

void scendere::network_filter::clear ()
{
	scendere::lock_guard<scendere::mutex> lock (mutex);
	items.assign (items.size (), scendere::uint128_t{ 0 });
}

template <typename OBJECT>
scendere::uint128_t scendere::network_filter::hash (OBJECT const & object_a) const
{
	std::vector<uint8_t> bytes;
	{
		scendere::vectorstream stream (bytes);
		object_a->serialize (stream);
	}
	return hash (bytes.data (), bytes.size ());
}

scendere::uint128_t & scendere::network_filter::get_element (scendere::uint128_t const & hash_a)
{
	debug_assert (!mutex.try_lock ());
	debug_assert (items.size () > 0);
	size_t index (hash_a % items.size ());
	return items[index];
}

scendere::uint128_t scendere::network_filter::hash (uint8_t const * bytes_a, size_t count_a) const
{
	scendere::uint128_union digest{ 0 };
	siphash_t siphash (key, static_cast<unsigned int> (key.size ()));
	siphash.CalculateDigest (digest.bytes.data (), bytes_a, count_a);
	return digest.number ();
}

// Explicitly instantiate
template scendere::uint128_t scendere::network_filter::hash (std::shared_ptr<scendere::block> const &) const;
template void scendere::network_filter::clear (std::shared_ptr<scendere::block> const &);
