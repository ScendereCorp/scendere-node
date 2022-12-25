#include <scendere/lib/ipc.hpp>
#include <scendere/lib/utility.hpp>

scendere::ipc::socket_base::socket_base (boost::asio::io_context & io_ctx_a) :
	io_timer (io_ctx_a)
{
}

void scendere::ipc::socket_base::timer_start (std::chrono::seconds timeout_a)
{
	if (timeout_a < std::chrono::seconds::max ())
	{
		io_timer.expires_from_now (boost::posix_time::seconds (static_cast<long> (timeout_a.count ())));
		io_timer.async_wait ([this] (boost::system::error_code const & ec) {
			if (!ec)
			{
				this->timer_expired ();
			}
		});
	}
}

void scendere::ipc::socket_base::timer_expired ()
{
	close ();
}

void scendere::ipc::socket_base::timer_cancel ()
{
	boost::system::error_code ec;
	io_timer.cancel (ec);
	debug_assert (!ec);
}

scendere::ipc::dsock_file_remover::dsock_file_remover (std::string const & file_a) :
	filename (file_a)
{
	std::remove (filename.c_str ());
}

scendere::ipc::dsock_file_remover::~dsock_file_remover ()
{
	std::remove (filename.c_str ());
}
