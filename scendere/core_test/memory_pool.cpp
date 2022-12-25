#include <scendere/lib/memory.hpp>
#include <scendere/node/active_transactions.hpp>
#include <scendere/secure/common.hpp>

#include <gtest/gtest.h>

#include <memory>
#include <vector>

namespace
{
/** This allocator records the size of all allocations that happen */
template <class T>
class record_allocations_new_delete_allocator
{
public:
	using value_type = T;

	explicit record_allocations_new_delete_allocator (std::vector<size_t> * allocated) :
		allocated (allocated)
	{
	}

	template <typename U>
	record_allocations_new_delete_allocator (const record_allocations_new_delete_allocator<U> & a)
	{
		allocated = a.allocated;
	}

	template <typename U>
	record_allocations_new_delete_allocator & operator= (const record_allocations_new_delete_allocator<U> &) = delete;

	T * allocate (size_t num_to_allocate)
	{
		auto size_allocated = (sizeof (T) * num_to_allocate);
		allocated->push_back (size_allocated);
		return static_cast<T *> (::operator new (size_allocated));
	}

	void deallocate (T * p, size_t num_to_deallocate)
	{
		::operator delete (p);
	}

	std::vector<size_t> * allocated;
};

template <typename T>
size_t get_allocated_size ()
{
	std::vector<size_t> allocated;
	record_allocations_new_delete_allocator<T> alloc (&allocated);
	(void)std::allocate_shared<T, record_allocations_new_delete_allocator<T>> (alloc);
	debug_assert (allocated.size () == 1);
	return allocated.front ();
}
}

TEST (memory_pool, validate_cleanup)
{
	// This might be turned off, e.g on Mac for instance, so don't do this test
	if (!scendere::get_use_memory_pools ())
	{
		return;
	}

	scendere::make_shared<scendere::open_block> ();
	scendere::make_shared<scendere::receive_block> ();
	scendere::make_shared<scendere::send_block> ();
	scendere::make_shared<scendere::change_block> ();
	scendere::make_shared<scendere::state_block> ();
	scendere::make_shared<scendere::vote> ();

	ASSERT_TRUE (scendere::purge_shared_ptr_singleton_pool_memory<scendere::open_block> ());
	ASSERT_TRUE (scendere::purge_shared_ptr_singleton_pool_memory<scendere::receive_block> ());
	ASSERT_TRUE (scendere::purge_shared_ptr_singleton_pool_memory<scendere::send_block> ());
	ASSERT_TRUE (scendere::purge_shared_ptr_singleton_pool_memory<scendere::state_block> ());
	ASSERT_TRUE (scendere::purge_shared_ptr_singleton_pool_memory<scendere::vote> ());

	// Change blocks have the same size as open_block so won't deallocate any memory
	ASSERT_FALSE (scendere::purge_shared_ptr_singleton_pool_memory<scendere::change_block> ());

	ASSERT_EQ (scendere::determine_shared_ptr_pool_size<scendere::open_block> (), get_allocated_size<scendere::open_block> () - sizeof (size_t));
	ASSERT_EQ (scendere::determine_shared_ptr_pool_size<scendere::receive_block> (), get_allocated_size<scendere::receive_block> () - sizeof (size_t));
	ASSERT_EQ (scendere::determine_shared_ptr_pool_size<scendere::send_block> (), get_allocated_size<scendere::send_block> () - sizeof (size_t));
	ASSERT_EQ (scendere::determine_shared_ptr_pool_size<scendere::change_block> (), get_allocated_size<scendere::change_block> () - sizeof (size_t));
	ASSERT_EQ (scendere::determine_shared_ptr_pool_size<scendere::state_block> (), get_allocated_size<scendere::state_block> () - sizeof (size_t));
	ASSERT_EQ (scendere::determine_shared_ptr_pool_size<scendere::vote> (), get_allocated_size<scendere::vote> () - sizeof (size_t));

	{
		scendere::active_transactions::ordered_cache inactive_votes_cache;
		scendere::account representative{ 1 };
		scendere::block_hash hash{ 1 };
		uint64_t timestamp{ 1 };
		scendere::inactive_cache_status default_status{};
		inactive_votes_cache.emplace (std::chrono::steady_clock::now (), hash, representative, timestamp, default_status);
	}

	ASSERT_TRUE (scendere::purge_singleton_inactive_votes_cache_pool_memory ());
}
