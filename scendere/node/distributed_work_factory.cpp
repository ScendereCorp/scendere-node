#include <scendere/node/distributed_work.hpp>
#include <scendere/node/distributed_work_factory.hpp>
#include <scendere/node/node.hpp>

scendere::distributed_work_factory::distributed_work_factory (scendere::node & node_a) :
	node (node_a)
{
}

scendere::distributed_work_factory::~distributed_work_factory ()
{
	stop ();
}

bool scendere::distributed_work_factory::make (scendere::work_version const version_a, scendere::root const & root_a, std::vector<std::pair<std::string, uint16_t>> const & peers_a, uint64_t difficulty_a, std::function<void (boost::optional<uint64_t>)> const & callback_a, boost::optional<scendere::account> const & account_a)
{
	return make (std::chrono::seconds (1), scendere::work_request{ version_a, root_a, difficulty_a, account_a, callback_a, peers_a });
}

bool scendere::distributed_work_factory::make (std::chrono::seconds const & backoff_a, scendere::work_request const & request_a)
{
	bool error_l{ true };
	if (!stopped)
	{
		cleanup_finished ();
		if (node.work_generation_enabled (request_a.peers))
		{
			auto distributed (std::make_shared<scendere::distributed_work> (node, request_a, backoff_a));
			{
				scendere::lock_guard<scendere::mutex> guard (mutex);
				items.emplace (request_a.root, distributed);
			}
			distributed->start ();
			error_l = false;
		}
	}
	return error_l;
}

void scendere::distributed_work_factory::cancel (scendere::root const & root_a)
{
	scendere::lock_guard<scendere::mutex> guard_l (mutex);
	auto root_items_l = items.equal_range (root_a);
	std::for_each (root_items_l.first, root_items_l.second, [] (auto item_l) {
		if (auto distributed_l = item_l.second.lock ())
		{
			// Send work_cancel to work peers and stop local work generation
			distributed_l->cancel ();
		}
	});
	items.erase (root_items_l.first, root_items_l.second);
}

void scendere::distributed_work_factory::cleanup_finished ()
{
	scendere::lock_guard<scendere::mutex> guard (mutex);
	// std::erase_if in c++20
	auto erase_if = [] (decltype (items) & container, auto pred) {
		for (auto it = container.begin (), end = container.end (); it != end;)
		{
			if (pred (*it))
			{
				it = container.erase (it);
			}
			else
			{
				++it;
			}
		}
	};
	erase_if (items, [] (decltype (items)::value_type item) { return item.second.expired (); });
}

void scendere::distributed_work_factory::stop ()
{
	if (!stopped.exchange (true))
	{
		// Cancel any ongoing work
		scendere::lock_guard<scendere::mutex> guard (mutex);
		for (auto & item_l : items)
		{
			if (auto distributed_l = item_l.second.lock ())
			{
				distributed_l->cancel ();
			}
		}
		items.clear ();
	}
}

std::size_t scendere::distributed_work_factory::size () const
{
	scendere::lock_guard<scendere::mutex> guard_l (mutex);
	return items.size ();
}

std::unique_ptr<scendere::container_info_component> scendere::collect_container_info (distributed_work_factory & distributed_work, std::string const & name)
{
	auto item_count = distributed_work.size ();
	auto sizeof_item_element = sizeof (decltype (scendere::distributed_work_factory::items)::value_type);
	auto composite = std::make_unique<container_info_composite> (name);
	composite->add_component (std::make_unique<container_info_leaf> (container_info{ "items", item_count, sizeof_item_element }));
	return composite;
}
