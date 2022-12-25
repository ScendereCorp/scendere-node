#include <scendere/node/inactive_cache_status.hpp>

bool scendere::inactive_cache_status::operator!= (inactive_cache_status const other) const
{
	return bootstrap_started != other.bootstrap_started
	|| election_started != other.election_started
	|| confirmed != other.confirmed
	|| tally != other.tally;
}

std::string scendere::inactive_cache_status::to_string () const
{
	std::stringstream ss;
	ss << "bootstrap_started=" << bootstrap_started;
	ss << ", election_started=" << election_started;
	ss << ", confirmed=" << confirmed;
	ss << ", tally=" << scendere::uint128_union (tally).to_string ();
	return ss.str ();
}
