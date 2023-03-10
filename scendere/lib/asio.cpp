#include <scendere/lib/asio.hpp>

scendere::shared_const_buffer::shared_const_buffer (std::vector<uint8_t> const & data) :
	m_data (std::make_shared<std::vector<uint8_t>> (data)),
	m_buffer (boost::asio::buffer (*m_data))
{
}

scendere::shared_const_buffer::shared_const_buffer (std::vector<uint8_t> && data) :
	m_data (std::make_shared<std::vector<uint8_t>> (std::move (data))),
	m_buffer (boost::asio::buffer (*m_data))
{
}

scendere::shared_const_buffer::shared_const_buffer (uint8_t data) :
	shared_const_buffer (std::vector<uint8_t>{ data })
{
}

scendere::shared_const_buffer::shared_const_buffer (std::string const & data) :
	m_data (std::make_shared<std::vector<uint8_t>> (data.begin (), data.end ())),
	m_buffer (boost::asio::buffer (*m_data))
{
}

scendere::shared_const_buffer::shared_const_buffer (std::shared_ptr<std::vector<uint8_t>> const & data) :
	m_data (data),
	m_buffer (boost::asio::buffer (*m_data))
{
}

boost::asio::const_buffer const * scendere::shared_const_buffer::begin () const
{
	return &m_buffer;
}

boost::asio::const_buffer const * scendere::shared_const_buffer::end () const
{
	return &m_buffer + 1;
}

std::size_t scendere::shared_const_buffer::size () const
{
	return m_buffer.size ();
}
