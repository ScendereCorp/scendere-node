#include <scendere/lib/locks.hpp>
#include <scendere/lib/threading.hpp>
#include <scendere/lib/timer.hpp>
#include <scendere/node/unchecked_map.hpp>
#include <scendere/secure/store.hpp>

#include <boost/range/join.hpp>

scendere::unchecked_map::unchecked_map (scendere::store & store, bool const & disable_delete) :
	store{ store },
	disable_delete{ disable_delete },
	thread{ [this] () { run (); } }
{
}

scendere::unchecked_map::~unchecked_map ()
{
	stop ();
	thread.join ();
}

void scendere::unchecked_map::put (scendere::hash_or_account const & dependency, scendere::unchecked_info const & info)
{
	scendere::unique_lock<scendere::mutex> lock{ mutex };
	buffer.push_back (std::make_pair (dependency, info));
	lock.unlock ();
	condition.notify_all (); // Notify run ()
}

auto scendere::unchecked_map::equal_range (scendere::transaction const & transaction, scendere::block_hash const & dependency) -> std::pair<iterator, iterator>
{
	return store.unchecked.equal_range (transaction, dependency);
}

auto scendere::unchecked_map::full_range (scendere::transaction const & transaction) -> std::pair<iterator, iterator>
{
	return store.unchecked.full_range (transaction);
}

std::vector<scendere::unchecked_info> scendere::unchecked_map::get (scendere::transaction const & transaction, scendere::block_hash const & hash)
{
	return store.unchecked.get (transaction, hash);
}

bool scendere::unchecked_map::exists (scendere::transaction const & transaction, scendere::unchecked_key const & key) const
{
	return store.unchecked.exists (transaction, key);
}

void scendere::unchecked_map::del (scendere::write_transaction const & transaction, scendere::unchecked_key const & key)
{
	store.unchecked.del (transaction, key);
}

void scendere::unchecked_map::clear (scendere::write_transaction const & transaction)
{
	store.unchecked.clear (transaction);
}

size_t scendere::unchecked_map::count (scendere::transaction const & transaction) const
{
	return store.unchecked.count (transaction);
}

void scendere::unchecked_map::stop ()
{
	scendere::unique_lock<scendere::mutex> lock{ mutex };
	if (!stopped)
	{
		stopped = true;
		condition.notify_all (); // Notify flush (), run ()
	}
}

void scendere::unchecked_map::flush ()
{
	scendere::unique_lock<scendere::mutex> lock{ mutex };
	condition.wait (lock, [this] () {
		return stopped || (buffer.empty () && back_buffer.empty () && !writing_back_buffer);
	});
}

void scendere::unchecked_map::trigger (scendere::hash_or_account const & dependency)
{
	scendere::unique_lock<scendere::mutex> lock{ mutex };
	buffer.push_back (dependency);
	debug_assert (buffer.back ().which () == 1); // which stands for "query".
	lock.unlock ();
	condition.notify_all (); // Notify run ()
}

scendere::unchecked_map::item_visitor::item_visitor (unchecked_map & unchecked, scendere::write_transaction const & transaction) :
	unchecked{ unchecked },
	transaction{ transaction }
{
}
void scendere::unchecked_map::item_visitor::operator() (insert const & item)
{
	auto const & [dependency, info] = item;
	unchecked.store.unchecked.put (transaction, dependency, { info.block, info.account, info.verified });
}

void scendere::unchecked_map::item_visitor::operator() (query const & item)
{
	auto [i, n] = unchecked.store.unchecked.equal_range (transaction, item.hash);
	std::deque<scendere::unchecked_key> delete_queue;
	for (; i != n; ++i)
	{
		auto const & key = i->first;
		auto const & info = i->second;
		delete_queue.push_back (key);
		unchecked.satisfied (info);
	}
	if (!unchecked.disable_delete)
	{
		for (auto const & key : delete_queue)
		{
			unchecked.del (transaction, key);
		}
	}
}

void scendere::unchecked_map::write_buffer (decltype (buffer) const & back_buffer)
{
	auto transaction = store.tx_begin_write ();
	item_visitor visitor{ *this, transaction };
	for (auto const & item : back_buffer)
	{
		boost::apply_visitor (visitor, item);
	}
}

void scendere::unchecked_map::run ()
{
	scendere::thread_role::set (scendere::thread_role::name::unchecked);
	scendere::unique_lock<scendere::mutex> lock{ mutex };
	while (!stopped)
	{
		if (!buffer.empty ())
		{
			back_buffer.swap (buffer);
			writing_back_buffer = true;
			lock.unlock ();
			write_buffer (back_buffer);
			lock.lock ();
			writing_back_buffer = false;
			back_buffer.clear ();
		}
		else
		{
			condition.notify_all (); // Notify flush ()
			condition.wait (lock, [this] () {
				return stopped || !buffer.empty ();
			});
		}
	}
}
