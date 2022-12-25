#pragma once

#include <scendere/lib/numbers.hpp>
#include <scendere/lib/utility.hpp>

#include <memory>
#include <mutex>
#include <unordered_map>

namespace scendere
{
class store;
class transaction;

class rep_weights
{
public:
	void representation_add (scendere::account const & source_rep_a, scendere::uint128_t const & amount_a);
	void representation_add_dual (scendere::account const & source_rep_1, scendere::uint128_t const & amount_1, scendere::account const & source_rep_2, scendere::uint128_t const & amount_2);
	scendere::uint128_t representation_get (scendere::account const & account_a) const;
	void representation_put (scendere::account const & account_a, scendere::uint128_union const & representation_a);
	std::unordered_map<scendere::account, scendere::uint128_t> get_rep_amounts () const;
	void copy_from (rep_weights & other_a);

private:
	mutable scendere::mutex mutex;
	std::unordered_map<scendere::account, scendere::uint128_t> rep_amounts;
	void put (scendere::account const & account_a, scendere::uint128_union const & representation_a);
	scendere::uint128_t get (scendere::account const & account_a) const;

	friend std::unique_ptr<container_info_component> collect_container_info (rep_weights const &, std::string const &);
};

std::unique_ptr<container_info_component> collect_container_info (rep_weights const &, std::string const &);
}
