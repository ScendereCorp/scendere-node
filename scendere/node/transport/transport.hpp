#pragma once

#include <scendere/lib/locks.hpp>
#include <scendere/lib/rate_limiting.hpp>
#include <scendere/lib/stats.hpp>
#include <scendere/node/common.hpp>
#include <scendere/node/socket.hpp>

#include <boost/asio/ip/network_v6.hpp>

namespace scendere
{
class bandwidth_limiter final
{
public:
	// initialize with limit 0 = unbounded
	bandwidth_limiter (double, std::size_t);
	bool should_drop (std::size_t const &);
	void reset (double, std::size_t);

private:
	scendere::rate::token_bucket bucket;
};

namespace transport
{
	scendere::endpoint map_endpoint_to_v6 (scendere::endpoint const &);
	scendere::endpoint map_tcp_to_endpoint (scendere::tcp_endpoint const &);
	scendere::tcp_endpoint map_endpoint_to_tcp (scendere::endpoint const &);
	boost::asio::ip::address map_address_to_subnetwork (boost::asio::ip::address const &);
	boost::asio::ip::address ipv4_address_or_ipv6_subnet (boost::asio::ip::address const &);
	boost::asio::ip::address_v6 mapped_from_v4_bytes (unsigned long);
	boost::asio::ip::address_v6 mapped_from_v4_or_v6 (boost::asio::ip::address const &);
	bool is_ipv4_or_v4_mapped_address (boost::asio::ip::address const &);

	// Unassigned, reserved, self
	bool reserved_address (scendere::endpoint const &, bool = false);
	static std::chrono::seconds constexpr syn_cookie_cutoff = std::chrono::seconds (5);
	enum class transport_type : uint8_t
	{
		undefined = 0,
		udp = 1,
		tcp = 2,
		loopback = 3
	};
	class channel
	{
	public:
		explicit channel (scendere::node &);
		virtual ~channel () = default;
		virtual std::size_t hash_code () const = 0;
		virtual bool operator== (scendere::transport::channel const &) const = 0;
		void send (scendere::message & message_a, std::function<void (boost::system::error_code const &, std::size_t)> const & callback_a = nullptr, scendere::buffer_drop_policy policy_a = scendere::buffer_drop_policy::limiter);
		// TODO: investigate clang-tidy warning about default parameters on virtual/override functions
		//
		virtual void send_buffer (scendere::shared_const_buffer const &, std::function<void (boost::system::error_code const &, std::size_t)> const & = nullptr, scendere::buffer_drop_policy = scendere::buffer_drop_policy::limiter) = 0;
		virtual std::string to_string () const = 0;
		virtual scendere::endpoint get_endpoint () const = 0;
		virtual scendere::tcp_endpoint get_tcp_endpoint () const = 0;
		virtual scendere::transport::transport_type get_type () const = 0;

		std::chrono::steady_clock::time_point get_last_bootstrap_attempt () const
		{
			scendere::lock_guard<scendere::mutex> lk (channel_mutex);
			return last_bootstrap_attempt;
		}

		void set_last_bootstrap_attempt (std::chrono::steady_clock::time_point const time_a)
		{
			scendere::lock_guard<scendere::mutex> lk (channel_mutex);
			last_bootstrap_attempt = time_a;
		}

		std::chrono::steady_clock::time_point get_last_packet_received () const
		{
			scendere::lock_guard<scendere::mutex> lk (channel_mutex);
			return last_packet_received;
		}

		void set_last_packet_received (std::chrono::steady_clock::time_point const time_a)
		{
			scendere::lock_guard<scendere::mutex> lk (channel_mutex);
			last_packet_received = time_a;
		}

		std::chrono::steady_clock::time_point get_last_packet_sent () const
		{
			scendere::lock_guard<scendere::mutex> lk (channel_mutex);
			return last_packet_sent;
		}

		void set_last_packet_sent (std::chrono::steady_clock::time_point const time_a)
		{
			scendere::lock_guard<scendere::mutex> lk (channel_mutex);
			last_packet_sent = time_a;
		}

		boost::optional<scendere::account> get_node_id_optional () const
		{
			scendere::lock_guard<scendere::mutex> lk (channel_mutex);
			return node_id;
		}

		scendere::account get_node_id () const
		{
			scendere::lock_guard<scendere::mutex> lk (channel_mutex);
			if (node_id.is_initialized ())
			{
				return node_id.get ();
			}
			else
			{
				return 0;
			}
		}

		void set_node_id (scendere::account node_id_a)
		{
			scendere::lock_guard<scendere::mutex> lk (channel_mutex);
			node_id = node_id_a;
		}

		uint8_t get_network_version () const
		{
			return network_version;
		}

		void set_network_version (uint8_t network_version_a)
		{
			network_version = network_version_a;
		}

		mutable scendere::mutex channel_mutex;

	private:
		std::chrono::steady_clock::time_point last_bootstrap_attempt{ std::chrono::steady_clock::time_point () };
		std::chrono::steady_clock::time_point last_packet_received{ std::chrono::steady_clock::now () };
		std::chrono::steady_clock::time_point last_packet_sent{ std::chrono::steady_clock::now () };
		boost::optional<scendere::account> node_id{ boost::none };
		std::atomic<uint8_t> network_version{ 0 };

	protected:
		scendere::node & node;
	};

	class channel_loopback final : public scendere::transport::channel
	{
	public:
		explicit channel_loopback (scendere::node &);
		std::size_t hash_code () const override;
		bool operator== (scendere::transport::channel const &) const override;
		// TODO: investigate clang-tidy warning about default parameters on virtual/override functions
		//
		void send_buffer (scendere::shared_const_buffer const &, std::function<void (boost::system::error_code const &, std::size_t)> const & = nullptr, scendere::buffer_drop_policy = scendere::buffer_drop_policy::limiter) override;
		std::string to_string () const override;
		bool operator== (scendere::transport::channel_loopback const & other_a) const
		{
			return endpoint == other_a.get_endpoint ();
		}

		scendere::endpoint get_endpoint () const override
		{
			return endpoint;
		}

		scendere::tcp_endpoint get_tcp_endpoint () const override
		{
			return scendere::transport::map_endpoint_to_tcp (endpoint);
		}

		scendere::transport::transport_type get_type () const override
		{
			return scendere::transport::transport_type::loopback;
		}

	private:
		scendere::endpoint const endpoint;
	};
} // namespace transport
} // namespace scendere

namespace std
{
template <>
struct hash<::scendere::transport::channel>
{
	std::size_t operator() (::scendere::transport::channel const & channel_a) const
	{
		return channel_a.hash_code ();
	}
};
template <>
struct equal_to<std::reference_wrapper<::scendere::transport::channel const>>
{
	bool operator() (std::reference_wrapper<::scendere::transport::channel const> const & lhs, std::reference_wrapper<::scendere::transport::channel const> const & rhs) const
	{
		return lhs.get () == rhs.get ();
	}
};
}

namespace boost
{
template <>
struct hash<::scendere::transport::channel>
{
	std::size_t operator() (::scendere::transport::channel const & channel_a) const
	{
		std::hash<::scendere::transport::channel> hash;
		return hash (channel_a);
	}
};
template <>
struct hash<std::reference_wrapper<::scendere::transport::channel const>>
{
	std::size_t operator() (std::reference_wrapper<::scendere::transport::channel const> const & channel_a) const
	{
		std::hash<::scendere::transport::channel> hash;
		return hash (channel_a.get ());
	}
};
}
