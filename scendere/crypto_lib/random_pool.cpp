#include <scendere/crypto_lib/random_pool.hpp>

#include <crypto/cryptopp/osrng.h>

void scendere::random_pool::generate_block (unsigned char * output, size_t size)
{
	auto & pool = get_pool ();
	pool.GenerateBlock (output, size);
}

unsigned scendere::random_pool::generate_word32 (unsigned min, unsigned max)
{
	auto & pool = get_pool ();
	return pool.GenerateWord32 (min, max);
}

unsigned char scendere::random_pool::generate_byte ()
{
	auto & pool = get_pool ();
	return pool.GenerateByte ();
}

CryptoPP::AutoSeededRandomPool & scendere::random_pool::get_pool ()
{
	static thread_local CryptoPP::AutoSeededRandomPool pool;
	return pool;
}
