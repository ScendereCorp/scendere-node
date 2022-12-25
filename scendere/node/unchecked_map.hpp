#pragma once

#include <scendere/lib/locks.hpp>
#include <scendere/lib/numbers.hpp>
#include <scendere/secure/store.hpp>

#include <atomic>
#include <thread>
#include <unordered_map>

namespace scendere
{
class store;
class transaction;
class unchecked_info;
class unchecked_key;
class write_transaction;
class unchecked_map
{
public:
	using iterator = scendere::unchecked_store::iterator;

public:
	unchecked_map (scendere::store & store, bool const & do_delete);
	~unchecked_map ();
	void put (scendere::hash_or_account const & dependency, scendere::unchecked_info const & info);
	std::pair<iterator, iterator> equal_range (scendere::transaction const & transaction, scendere::block_hash const & dependency);
	std::pair<iterator, iterator> full_range (scendere::transaction const & transaction);
	std::vector<scendere::unchecked_info> get (scendere::transaction const &, scendere::block_hash const &);
	bool exists (scendere::transaction const & transaction, scendere::unchecked_key const & key) const;
	void del (scendere::write_transaction const & transaction, scendere::unchecked_key const & key);
	void clear (scendere::write_transaction const & transaction);
	size_t count (scendere::transaction const & transaction) const;
	void stop ();
	void flush ();

public: // Trigger requested dependencies
	void trigger (scendere::hash_or_account const & dependency);
	std::function<void (scendere::unchecked_info const &)> satisfied{ [] (scendere::unchecked_info const &) {} };

private:
	using insert = std::pair<scendere::hash_or_account, scendere::unchecked_info>;
	using query = scendere::hash_or_account;
	class item_visitor : boost::static_visitor<>
	{
	public:
		item_visitor (unchecked_map & unchecked, scendere::write_transaction const & transaction);
		void operator() (insert const & item);
		void operator() (query const & item);
		unchecked_map & unchecked;
		scendere::write_transaction const & transaction;
	};
	void run ();
	scendere::store & store;
	bool const & disable_delete;
	std::deque<boost::variant<insert, query>> buffer;
	std::deque<boost::variant<insert, query>> back_buffer;
	bool writing_back_buffer{ false };
	bool stopped{ false };
	scendere::condition_variable condition;
	scendere::mutex mutex;
	std::thread thread;
	void write_buffer (decltype (buffer) const & back_buffer);
};
}
