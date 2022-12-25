#include <scendere/node/election_scheduler.hpp>
#include <scendere/node/node.hpp>

scendere::election_scheduler::election_scheduler (scendere::node & node) :
	node{ node },
	stopped{ false },
	thread{ [this] () { run (); } }
{
}

scendere::election_scheduler::~election_scheduler ()
{
	stop ();
	thread.join ();
}

void scendere::election_scheduler::manual (std::shared_ptr<scendere::block> const & block_a, boost::optional<scendere::uint128_t> const & previous_balance_a, scendere::election_behavior election_behavior_a, std::function<void (std::shared_ptr<scendere::block> const &)> const & confirmation_action_a)
{
	scendere::lock_guard<scendere::mutex> lock{ mutex };
	manual_queue.push_back (std::make_tuple (block_a, previous_balance_a, election_behavior_a, confirmation_action_a));
	notify ();
}

void scendere::election_scheduler::activate (scendere::account const & account_a, scendere::transaction const & transaction)
{
	debug_assert (!account_a.is_zero ());
	scendere::account_info account_info;
	if (!node.store.account.get (transaction, account_a, account_info))
	{
		scendere::confirmation_height_info conf_info;
		node.store.confirmation_height.get (transaction, account_a, conf_info);
		if (conf_info.height < account_info.block_count)
		{
			debug_assert (conf_info.frontier != account_info.head);
			auto hash = conf_info.height == 0 ? account_info.open_block : node.store.block.successor (transaction, conf_info.frontier);
			auto block = node.store.block.get (transaction, hash);
			debug_assert (block != nullptr);
			if (node.ledger.dependents_confirmed (transaction, *block))
			{
				scendere::lock_guard<scendere::mutex> lock{ mutex };
				priority.push (account_info.modified, block);
				notify ();
			}
		}
	}
}

void scendere::election_scheduler::stop ()
{
	scendere::unique_lock<scendere::mutex> lock{ mutex };
	stopped = true;
	notify ();
}

void scendere::election_scheduler::flush ()
{
	scendere::unique_lock<scendere::mutex> lock{ mutex };
	condition.wait (lock, [this] () {
		return stopped || empty_locked () || node.active.vacancy () <= 0;
	});
}

void scendere::election_scheduler::notify ()
{
	condition.notify_all ();
}

std::size_t scendere::election_scheduler::size () const
{
	scendere::lock_guard<scendere::mutex> lock{ mutex };
	return priority.size () + manual_queue.size ();
}

bool scendere::election_scheduler::empty_locked () const
{
	return priority.empty () && manual_queue.empty ();
}

bool scendere::election_scheduler::empty () const
{
	scendere::lock_guard<scendere::mutex> lock{ mutex };
	return empty_locked ();
}

std::size_t scendere::election_scheduler::priority_queue_size () const
{
	return priority.size ();
}

bool scendere::election_scheduler::priority_queue_predicate () const
{
	return node.active.vacancy () > 0 && !priority.empty ();
}

bool scendere::election_scheduler::manual_queue_predicate () const
{
	return !manual_queue.empty ();
}

bool scendere::election_scheduler::overfill_predicate () const
{
	return node.active.vacancy () < 0;
}

void scendere::election_scheduler::run ()
{
	scendere::thread_role::set (scendere::thread_role::name::election_scheduler);
	scendere::unique_lock<scendere::mutex> lock{ mutex };
	while (!stopped)
	{
		condition.wait (lock, [this] () {
			return stopped || priority_queue_predicate () || manual_queue_predicate () || overfill_predicate ();
		});
		debug_assert ((std::this_thread::yield (), true)); // Introduce some random delay in debug builds
		if (!stopped)
		{
			if (overfill_predicate ())
			{
				lock.unlock ();
				node.active.erase_oldest ();
			}
			else if (manual_queue_predicate ())
			{
				auto const [block, previous_balance, election_behavior, confirmation_action] = manual_queue.front ();
				manual_queue.pop_front ();
				lock.unlock ();
				scendere::unique_lock<scendere::mutex> lock2 (node.active.mutex);
				node.active.insert_impl (lock2, block, previous_balance, election_behavior, confirmation_action);
			}
			else if (priority_queue_predicate ())
			{
				auto block = priority.top ();
				priority.pop ();
				lock.unlock ();
				std::shared_ptr<scendere::election> election;
				scendere::unique_lock<scendere::mutex> lock2 (node.active.mutex);
				election = node.active.insert_impl (lock2, block).election;
				if (election != nullptr)
				{
					election->transition_active ();
				}
			}
			else
			{
				lock.unlock ();
			}
			notify ();
			lock.lock ();
		}
	}
}

std::unique_ptr<scendere::container_info_component> scendere::election_scheduler::collect_container_info (std::string const & name)
{
	scendere::unique_lock<scendere::mutex> lock{ mutex };

	auto composite = std::make_unique<container_info_composite> (name);
	composite->add_component (std::make_unique<container_info_leaf> (container_info{ "manual_queue", manual_queue.size (), sizeof (decltype (manual_queue)::value_type) }));
	composite->add_component (priority.collect_container_info ("priority"));
	return composite;
}