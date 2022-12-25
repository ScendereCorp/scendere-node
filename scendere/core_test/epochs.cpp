#include <scendere/lib/epoch.hpp>
#include <scendere/secure/common.hpp>

#include <gtest/gtest.h>

TEST (epochs, is_epoch_link)
{
	scendere::epochs epochs;
	// Test epoch 1
	scendere::keypair key1;
	auto link1 = 42;
	auto link2 = 43;
	ASSERT_FALSE (epochs.is_epoch_link (link1));
	ASSERT_FALSE (epochs.is_epoch_link (link2));
	epochs.add (scendere::epoch::epoch_1, key1.pub, link1);
	ASSERT_TRUE (epochs.is_epoch_link (link1));
	ASSERT_FALSE (epochs.is_epoch_link (link2));
	ASSERT_EQ (key1.pub, epochs.signer (scendere::epoch::epoch_1));
	ASSERT_EQ (epochs.epoch (link1), scendere::epoch::epoch_1);

	// Test epoch 2
	scendere::keypair key2;
	epochs.add (scendere::epoch::epoch_2, key2.pub, link2);
	ASSERT_TRUE (epochs.is_epoch_link (link2));
	ASSERT_EQ (key2.pub, epochs.signer (scendere::epoch::epoch_2));
	ASSERT_EQ (scendere::uint256_union (link1), epochs.link (scendere::epoch::epoch_1));
	ASSERT_EQ (scendere::uint256_union (link2), epochs.link (scendere::epoch::epoch_2));
	ASSERT_EQ (epochs.epoch (link2), scendere::epoch::epoch_2);
}

TEST (epochs, is_sequential)
{
	ASSERT_TRUE (scendere::epochs::is_sequential (scendere::epoch::epoch_0, scendere::epoch::epoch_1));
	ASSERT_TRUE (scendere::epochs::is_sequential (scendere::epoch::epoch_1, scendere::epoch::epoch_2));

	ASSERT_FALSE (scendere::epochs::is_sequential (scendere::epoch::epoch_0, scendere::epoch::epoch_2));
	ASSERT_FALSE (scendere::epochs::is_sequential (scendere::epoch::epoch_0, scendere::epoch::invalid));
	ASSERT_FALSE (scendere::epochs::is_sequential (scendere::epoch::unspecified, scendere::epoch::epoch_1));
	ASSERT_FALSE (scendere::epochs::is_sequential (scendere::epoch::epoch_1, scendere::epoch::epoch_0));
	ASSERT_FALSE (scendere::epochs::is_sequential (scendere::epoch::epoch_2, scendere::epoch::epoch_0));
	ASSERT_FALSE (scendere::epochs::is_sequential (scendere::epoch::epoch_2, scendere::epoch::epoch_2));
}
