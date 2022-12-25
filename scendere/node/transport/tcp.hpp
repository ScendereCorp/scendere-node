#pragma once

#include <scendere/node/common.hpp>
#include <scendere/node/transport/transport.hpp>

#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/mem_fun.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/random_access_index.hpp>
#include <boost/multi_index_container.hpp>

#include <unordered_set>

namespace mi = boost::multi_index;

namespace scendere
{
class bootstrap_server;
class tcp_message_item final
{
public:
	std::shared_ptr<scendere::message> message;
	scendere::tcp_endpoint endpoint;
	scendere::account node_id;
	std::shared_ptr<scendere::socket> socket;
};
namespace transport
{
	class tcp_channels;
	class channel_tcp : public scendere::transport::channel
	{
		friend class scendere::transport::tcp_channels;

	public:
		channel_tcp (scendere::node &, std::weak_ptr<scendere::socket>);
		~channel_tcp () override;
		std::size_t hash_code () const override;
		bool operator== (scendere::transport::channel const &) const override;
		// TODO: investigate clang-tidy warning about default parameters on virtual/override functions
		//
		void send_buffer (scendere::shared_const_buffer const &, std::function<void (boost::system::error_code const &, std::size_t)> const & = nullptr, scendere::buffer_drop_policy = scendere::buffer_drop_policy::limiter) override;
		std::string to_string () const override;
		bool operator== (scendere::transport::channel_tcp const & other_a) const
		{
			return &node == &other_a.node && socket.lock () == other_a.socket.lock ();
		}
		std::weak_ptr<scendere::socket> socket;
		/* Mark for temporary channels. Usually remote ports of these channels are ephemeral and received from incoming connections to server.
		If remote part has open listening port, temporary channel will be replaced with direct connection to listening port soon. But if other side is behing NAT or firewall this connection can be pemanent. */
		std::atomic<bool> temporary{ false };

		void set_endpoint ();

		scendere::endpoint get_endpoint () const override
		{
			return scendere::transport::map_tcp_to_endpoint (get_tcp_endpoint ());
		}

		scendere::tcp_endpoint get_tcp_endpoint () const override
		{
			scendere::lock_guard<scendere::mutex> lk (channel_mutex);
			return endpoint;
		}

		scendere::transport::transport_type get_type () const override
		{
			return scendere::transport::transport_type::tcp;
		}

	private:
		scendere::tcp_endpoint endpoint{ boost::asio::ip::address_v6::any (), 0 };
	};
	class tcp_channels final
	{
		friend class scendere::transport::channel_tcp;
		friend class telemetry_simultaneous_requests_Test;

	public:
		explicit tcp_channels (scendere::node &, std::function<void (scendere::message const &, std::shared_ptr<scendere::transport::channel> const &)> = nullptr);
		bool insert (std::shared_ptr<scendere::transport::channel_tcp> const &, std::shared_ptr<scendere::socket> const &, std::shared_ptr<scendere::bootstrap_server> const &);
		void erase (scendere::tcp_endpoint const &);
		std::size_t size () const;
		std::shared_ptr<scendere::transport::channel_tcp> find_channel (scendere::tcp_endpoint const &) const;
		void random_fill (std::array<scendere::endpoint, 8> &) const;
		std::unordered_set<std::shared_ptr<scendere::transport::channel>> random_set (std::size_t, uint8_t = 0, bool = false) const;
		bool store_all (bool = true);
		std::shared_ptr<scendere::transport::channel_tcp> find_node_id (scendere::account const &);
		// Get the next peer for attempting a tcp connection
		scendere::tcp_endpoint bootstrap_peer (uint8_t connection_protocol_version_min);
		void receive ();
		void start ();
		void stop ();
		void process_messages ();
		void process_message (scendere::message const &, scendere::tcp_endpoint const &, scendere::account const &, std::shared_ptr<scendere::socket> const &);
		bool max_ip_connections (scendere::tcp_endpoint const & endpoint_a);
		bool max_subnetwork_connections (scendere::tcp_endpoint const & endpoint_a);
		bool max_ip_or_subnetwork_connections (scendere::tcp_endpoint const & endpoint_a);
		// Should we reach out to this endpoint with a keepalive message
		bool reachout (scendere::endpoint const &);
		std::unique_ptr<container_info_component> collect_container_info (std::string const &);
		void purge (std::chrono::steady_clock::time_point const &);
		void ongoing_keepalive ();
		void list (std::deque<std::shared_ptr<scendere::transport::channel>> &, uint8_t = 0, bool = true);
		void modify (std::shared_ptr<scendere::transport::channel_tcp> const &, std::function<void (std::shared_ptr<scendere::transport::channel_tcp> const &)>);
		void update (scendere::tcp_endpoint const &);
		// Connection start
		void start_tcp (scendere::endpoint const &);
		void start_tcp_receive_node_id (std::shared_ptr<scendere::transport::channel_tcp> const &, scendere::endpoint const &, std::shared_ptr<std::vector<uint8_t>> const &);
		void udp_fallback (scendere::endpoint const &);
		scendere::node & node;

	private:
		std::function<void (scendere::message const &, std::shared_ptr<scendere::transport::channel> const &)> sink;
		class endpoint_tag
		{
		};
		class ip_address_tag
		{
		};
		class subnetwork_tag
		{
		};
		class random_access_tag
		{
		};
		class last_packet_sent_tag
		{
		};
		class last_bootstrap_attempt_tag
		{
		};
		class last_attempt_tag
		{
		};
		class node_id_tag
		{
		};
		class version_tag
		{
		};

		class channel_tcp_wrapper final
		{
		public:
			std::shared_ptr<scendere::transport::channel_tcp> channel;
			std::shared_ptr<scendere::socket> socket;
			std::shared_ptr<scendere::bootstrap_server> response_server;
			channel_tcp_wrapper (std::shared_ptr<scendere::transport::channel_tcp> channel_a, std::shared_ptr<scendere::socket> socket_a, std::shared_ptr<scendere::bootstrap_server> server_a) :
				channel (std::move (channel_a)), socket (std::move (socket_a)), response_server (std::move (server_a))
			{
			}
			scendere::tcp_endpoint endpoint () const
			{
				return channel->get_tcp_endpoint ();
			}
			std::chrono::steady_clock::time_point last_packet_sent () const
			{
				return channel->get_last_packet_sent ();
			}
			std::chrono::steady_clock::time_point last_bootstrap_attempt () const
			{
				return channel->get_last_bootstrap_attempt ();
			}
			boost::asio::ip::address ip_address () const
			{
				return scendere::transport::ipv4_address_or_ipv6_subnet (endpoint ().address ());
			}
			boost::asio::ip::address subnetwork () const
			{
				return scendere::transport::map_address_to_subnetwork (endpoint ().address ());
			}
			scendere::account node_id () const
			{
				auto node_id (channel->get_node_id ());
				debug_assert (!node_id.is_zero ());
				return node_id;
			}
			uint8_t network_version () const
			{
				return channel->get_network_version ();
			}
		};
		class tcp_endpoint_attempt final
		{
		public:
			scendere::tcp_endpoint endpoint;
			boost::asio::ip::address address;
			boost::asio::ip::address subnetwork;
			std::chrono::steady_clock::time_point last_attempt{ std::chrono::steady_clock::now () };

			explicit tcp_endpoint_attempt (scendere::tcp_endpoint const & endpoint_a) :
				endpoint (endpoint_a),
				address (scendere::transport::ipv4_address_or_ipv6_subnet (endpoint_a.address ())),
				subnetwork (scendere::transport::map_address_to_subnetwork (endpoint_a.address ()))
			{
			}
		};
		mutable scendere::mutex mutex;
		// clang-format off
		boost::multi_index_container<channel_tcp_wrapper,
		mi::indexed_by<
			mi::random_access<mi::tag<random_access_tag>>,
			mi::ordered_non_unique<mi::tag<last_bootstrap_attempt_tag>,
				mi::const_mem_fun<channel_tcp_wrapper, std::chrono::steady_clock::time_point, &channel_tcp_wrapper::last_bootstrap_attempt>>,
			mi::hashed_unique<mi::tag<endpoint_tag>,
				mi::const_mem_fun<channel_tcp_wrapper, scendere::tcp_endpoint, &channel_tcp_wrapper::endpoint>>,
			mi::hashed_non_unique<mi::tag<node_id_tag>,
				mi::const_mem_fun<channel_tcp_wrapper, scendere::account, &channel_tcp_wrapper::node_id>>,
			mi::ordered_non_unique<mi::tag<last_packet_sent_tag>,
				mi::const_mem_fun<channel_tcp_wrapper, std::chrono::steady_clock::time_point, &channel_tcp_wrapper::last_packet_sent>>,
			mi::ordered_non_unique<mi::tag<version_tag>,
				mi::const_mem_fun<channel_tcp_wrapper, uint8_t, &channel_tcp_wrapper::network_version>>,
			mi::hashed_non_unique<mi::tag<ip_address_tag>,
				mi::const_mem_fun<channel_tcp_wrapper, boost::asio::ip::address, &channel_tcp_wrapper::ip_address>>,
			mi::hashed_non_unique<mi::tag<subnetwork_tag>,
				mi::const_mem_fun<channel_tcp_wrapper, boost::asio::ip::address, &channel_tcp_wrapper::subnetwork>>>>
		channels;
		boost::multi_index_container<tcp_endpoint_attempt,
		mi::indexed_by<
			mi::hashed_unique<mi::tag<endpoint_tag>,
				mi::member<tcp_endpoint_attempt, scendere::tcp_endpoint, &tcp_endpoint_attempt::endpoint>>,
			mi::hashed_non_unique<mi::tag<ip_address_tag>,
				mi::member<tcp_endpoint_attempt, boost::asio::ip::address, &tcp_endpoint_attempt::address>>,
			mi::hashed_non_unique<mi::tag<subnetwork_tag>,
				mi::member<tcp_endpoint_attempt, boost::asio::ip::address, &tcp_endpoint_attempt::subnetwork>>,
			mi::ordered_non_unique<mi::tag<last_attempt_tag>,
				mi::member<tcp_endpoint_attempt, std::chrono::steady_clock::time_point, &tcp_endpoint_attempt::last_attempt>>>>
		attempts;
		// clang-format on
		std::atomic<bool> stopped{ false };

		friend class network_peer_max_tcp_attempts_subnetwork_Test;
	};
} // namespace transport
} // namespace scendere
