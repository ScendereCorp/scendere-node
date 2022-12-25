#!/usr/bin/env bash

###################################################################################################

code_inspect()
{
    local SOURCE_ROOT_PATH=$1
    if [[ $SOURCE_ROOT_PATH == "" ]]; then
        echo "Missing the source code path" >&2
        return 1
    fi

    # This is to prevent out of scope access in async_write from asio which is not picked up by static analysers
    if [[ $(grep -rl --exclude="*asio.hpp" "asio::async_write" $SOURCE_ROOT_PATH/scendere) ]]; then
        echo "Using boost::asio::async_write directly is not permitted (except in scendere/lib/asio.hpp). Use scendere::async_write instead" >&2
        return 1
    fi

    # prevent unsolicited use of std::lock_guard, std::unique_lock, std::condition_variable & std::mutex outside of allowed areas
    if [[ $(grep -rl --exclude={"*random_pool.cpp","*random_pool.hpp","*random_pool_shuffle.hpp","*locks.hpp","*locks.cpp"} "std::unique_lock\|std::lock_guard\|std::condition_variable\|std::mutex" $SOURCE_ROOT_PATH/scendere) ]]; then
        echo "Using std::unique_lock, std::lock_guard, std::condition_variable or std::mutex is not permitted (except in scendere/lib/locks.hpp and non-scendere dependent libraries). Use the scendere::* versions instead" >&2
        return 1
    fi

    if [[ $(grep -rlP "^\s*assert \(" $SOURCE_ROOT_PATH/scendere) ]]; then
        echo "Using assert is not permitted. Use debug_assert instead." >&2
        return 1
    fi

    return 0
}

###################################################################################################
