namespace boost
{
namespace filesystem
{
	class path;
}
}

namespace scendere
{
class node_flags;
}
namespace scendere_daemon
{
class daemon
{
public:
	void run (boost::filesystem::path const &, scendere::node_flags const & flags);
};
}
