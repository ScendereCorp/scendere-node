#include <scendere/lib/epoch.hpp>
#include <scendere/lib/utility.hpp>

scendere::link const & scendere::epochs::link (scendere::epoch epoch_a) const
{
	return epochs_m.at (epoch_a).link;
}

bool scendere::epochs::is_epoch_link (scendere::link const & link_a) const
{
	return std::any_of (epochs_m.begin (), epochs_m.end (), [&link_a] (auto const & item_a) { return item_a.second.link == link_a; });
}

scendere::public_key const & scendere::epochs::signer (scendere::epoch epoch_a) const
{
	return epochs_m.at (epoch_a).signer;
}

scendere::epoch scendere::epochs::epoch (scendere::link const & link_a) const
{
	auto existing (std::find_if (epochs_m.begin (), epochs_m.end (), [&link_a] (auto const & item_a) { return item_a.second.link == link_a; }));
	debug_assert (existing != epochs_m.end ());
	return existing->first;
}

void scendere::epochs::add (scendere::epoch epoch_a, scendere::public_key const & signer_a, scendere::link const & link_a)
{
	debug_assert (epochs_m.find (epoch_a) == epochs_m.end ());
	epochs_m[epoch_a] = { signer_a, link_a };
}

bool scendere::epochs::is_sequential (scendere::epoch epoch_a, scendere::epoch new_epoch_a)
{
	auto head_epoch = std::underlying_type_t<scendere::epoch> (epoch_a);
	bool is_valid_epoch (head_epoch >= std::underlying_type_t<scendere::epoch> (scendere::epoch::epoch_0));
	return is_valid_epoch && (std::underlying_type_t<scendere::epoch> (new_epoch_a) == (head_epoch + 1));
}

std::underlying_type_t<scendere::epoch> scendere::normalized_epoch (scendere::epoch epoch_a)
{
	// Currently assumes that the epoch versions in the enum are sequential.
	auto start = std::underlying_type_t<scendere::epoch> (scendere::epoch::epoch_0);
	auto end = std::underlying_type_t<scendere::epoch> (epoch_a);
	debug_assert (end >= start);
	return end - start;
}
