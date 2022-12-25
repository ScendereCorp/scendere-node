#include <scendere/lib/rep_weights.hpp>
#include <scendere/secure/store.hpp>

void scendere::rep_weights::representation_add (scendere::account const & source_rep_a, scendere::uint128_t const & amount_a)
{
	scendere::lock_guard<scendere::mutex> guard (mutex);
	auto source_previous (get (source_rep_a));
	put (source_rep_a, source_previous + amount_a);
}

void scendere::rep_weights::representation_add_dual (scendere::account const & source_rep_1, scendere::uint128_t const & amount_1, scendere::account const & source_rep_2, scendere::uint128_t const & amount_2)
{
	if (source_rep_1 != source_rep_2)
	{
		scendere::lock_guard<scendere::mutex> guard (mutex);
		auto source_previous_1 (get (source_rep_1));
		put (source_rep_1, source_previous_1 + amount_1);
		auto source_previous_2 (get (source_rep_2));
		put (source_rep_2, source_previous_2 + amount_2);
	}
	else
	{
		representation_add (source_rep_1, amount_1 + amount_2);
	}
}

void scendere::rep_weights::representation_put (scendere::account const & account_a, scendere::uint128_union const & representation_a)
{
	scendere::lock_guard<scendere::mutex> guard (mutex);
	put (account_a, representation_a);
}

scendere::uint128_t scendere::rep_weights::representation_get (scendere::account const & account_a) const
{
	scendere::lock_guard<scendere::mutex> lk (mutex);
	return get (account_a);
}

/** Makes a copy */
std::unordered_map<scendere::account, scendere::uint128_t> scendere::rep_weights::get_rep_amounts () const
{
	scendere::lock_guard<scendere::mutex> guard (mutex);
	return rep_amounts;
}

void scendere::rep_weights::copy_from (scendere::rep_weights & other_a)
{
	scendere::lock_guard<scendere::mutex> guard_this (mutex);
	scendere::lock_guard<scendere::mutex> guard_other (other_a.mutex);
	for (auto const & entry : other_a.rep_amounts)
	{
		auto prev_amount (get (entry.first));
		put (entry.first, prev_amount + entry.second);
	}
}

void scendere::rep_weights::put (scendere::account const & account_a, scendere::uint128_union const & representation_a)
{
	auto it = rep_amounts.find (account_a);
	auto amount = representation_a.number ();
	if (it != rep_amounts.end ())
	{
		it->second = amount;
	}
	else
	{
		rep_amounts.emplace (account_a, amount);
	}
}

scendere::uint128_t scendere::rep_weights::get (scendere::account const & account_a) const
{
	auto it = rep_amounts.find (account_a);
	if (it != rep_amounts.end ())
	{
		return it->second;
	}
	else
	{
		return scendere::uint128_t{ 0 };
	}
}

std::unique_ptr<scendere::container_info_component> scendere::collect_container_info (scendere::rep_weights const & rep_weights, std::string const & name)
{
	size_t rep_amounts_count;

	{
		scendere::lock_guard<scendere::mutex> guard (rep_weights.mutex);
		rep_amounts_count = rep_weights.rep_amounts.size ();
	}
	auto sizeof_element = sizeof (decltype (rep_weights.rep_amounts)::value_type);
	auto composite = std::make_unique<scendere::container_info_composite> (name);
	composite->add_component (std::make_unique<scendere::container_info_leaf> (container_info{ "rep_amounts", rep_amounts_count, sizeof_element }));
	return composite;
}
