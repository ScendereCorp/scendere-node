#include <scendere/lib/jsonconfig.hpp>
#include <scendere/lib/locks.hpp>
#include <scendere/lib/stats.hpp>
#include <scendere/lib/tomlconfig.hpp>

#include <boost/format.hpp>
#include <boost/property_tree/json_parser.hpp>

#include <ctime>
#include <fstream>
#include <sstream>

scendere::error scendere::stat_config::deserialize_toml (scendere::tomlconfig & toml)
{
	auto sampling_l (toml.get_optional_child ("sampling"));
	if (sampling_l)
	{
		sampling_l->get<bool> ("enable", sampling_enabled);
		sampling_l->get<size_t> ("capacity", capacity);
		sampling_l->get<size_t> ("interval", interval);
	}

	auto log_l (toml.get_optional_child ("log"));
	if (log_l)
	{
		log_l->get<bool> ("headers", log_headers);
		log_l->get<size_t> ("interval_counters", log_interval_counters);
		log_l->get<size_t> ("interval_samples", log_interval_samples);
		log_l->get<size_t> ("rotation_count", log_rotation_count);
		log_l->get<std::string> ("filename_counters", log_counters_filename);
		log_l->get<std::string> ("filename_samples", log_samples_filename);

		// Don't allow specifying the same file name for counter and samples logs
		if (log_counters_filename == log_samples_filename)
		{
			toml.get_error ().set ("The statistics counter and samples config values must be different");
		}
	}

	return toml.get_error ();
}

scendere::error scendere::stat_config::serialize_toml (scendere::tomlconfig & toml) const
{
	scendere::tomlconfig sampling_l;
	sampling_l.put ("enable", sampling_enabled, "Enable or disable sampling.\ntype:bool");
	sampling_l.put ("capacity", capacity, "How many sample intervals to keep in the ring buffer.\ntype:uint64");
	sampling_l.put ("interval", interval, "Sample interval.\ntype:milliseconds");
	toml.put_child ("sampling", sampling_l);

	scendere::tomlconfig log_l;
	log_l.put ("headers", log_headers, "If true, write headers on each counter or samples writeout.\nThe header contains log type and the current wall time.\ntype:bool");
	log_l.put ("interval_counters", log_interval_counters, "How often to log counters. 0 disables logging.\ntype:milliseconds");
	log_l.put ("interval_samples", log_interval_samples, "How often to log samples. 0 disables logging.\ntype:milliseconds");
	log_l.put ("rotation_count", log_rotation_count, "Maximum number of log outputs before rotating the file.\ntype:uint64");
	log_l.put ("filename_counters", log_counters_filename, "Log file name for counters.\ntype:string");
	log_l.put ("filename_samples", log_samples_filename, "Log file name for samples.\ntype:string");
	toml.put_child ("log", log_l);
	return toml.get_error ();
}

std::string scendere::stat_log_sink::tm_to_string (tm & tm)
{
	return (boost::format ("%04d.%02d.%02d %02d:%02d:%02d") % (1900 + tm.tm_year) % (tm.tm_mon + 1) % tm.tm_mday % tm.tm_hour % tm.tm_min % tm.tm_sec).str ();
}

/** JSON sink. The resulting JSON object is provided as both a property_tree::ptree (to_object) and a string (to_string) */
class json_writer : public scendere::stat_log_sink
{
	boost::property_tree::ptree tree;
	boost::property_tree::ptree entries;

public:
	std::ostream & out () override
	{
		return sstr;
	}

	void begin () override
	{
		tree.clear ();
	}

	void write_header (std::string const & header, std::chrono::system_clock::time_point & walltime) override
	{
		std::time_t now = std::chrono::system_clock::to_time_t (walltime);
		tm tm = *localtime (&now);
		tree.put ("type", header);
		tree.put ("created", tm_to_string (tm));
	}

	void write_entry (tm & tm, std::string const & type, std::string const & detail, std::string const & dir, uint64_t value, scendere::stat_histogram * histogram) override
	{
		boost::property_tree::ptree entry;
		entry.put ("time", boost::format ("%02d:%02d:%02d") % tm.tm_hour % tm.tm_min % tm.tm_sec);
		entry.put ("type", type);
		entry.put ("detail", detail);
		entry.put ("dir", dir);
		entry.put ("value", value);
		if (histogram != nullptr)
		{
			boost::property_tree::ptree histogram_node;
			for (auto const & bin : histogram->get_bins ())
			{
				boost::property_tree::ptree bin_node;
				bin_node.put ("start_inclusive", bin.start_inclusive);
				bin_node.put ("end_exclusive", bin.end_exclusive);
				bin_node.put ("value", bin.value);

				std::time_t time = std::chrono::system_clock::to_time_t (bin.timestamp);
				struct tm local_tm = *localtime (&time);
				bin_node.put ("time", boost::format ("%02d:%02d:%02d") % local_tm.tm_hour % local_tm.tm_min % local_tm.tm_sec);
				histogram_node.push_back (std::make_pair ("", bin_node));
			}
			entry.put_child ("histogram", histogram_node);
		}
		entries.push_back (std::make_pair ("", entry));
	}

	void finalize () override
	{
		tree.add_child ("entries", entries);
	}

	void * to_object () override
	{
		return &tree;
	}

	std::string to_string () override
	{
		boost::property_tree::write_json (sstr, tree);
		return sstr.str ();
	}

private:
	std::ostringstream sstr;
};

/** File sink with rotation support. This writes one counter per line and does not include histogram values. */
class file_writer : public scendere::stat_log_sink
{
public:
	std::ofstream log;
	std::string filename;

	explicit file_writer (std::string const & filename) :
		filename (filename)
	{
		log.open (filename.c_str (), std::ofstream::out);
	}
	virtual ~file_writer ()
	{
		log.close ();
	}
	std::ostream & out () override
	{
		return log;
	}

	void write_header (std::string const & header, std::chrono::system_clock::time_point & walltime) override
	{
		std::time_t now = std::chrono::system_clock::to_time_t (walltime);
		tm tm = *localtime (&now);
		log << header << "," << boost::format ("%04d.%02d.%02d %02d:%02d:%02d") % (1900 + tm.tm_year) % (tm.tm_mon + 1) % tm.tm_mday % tm.tm_hour % tm.tm_min % tm.tm_sec << std::endl;
	}

	void write_entry (tm & tm, std::string const & type, std::string const & detail, std::string const & dir, uint64_t value, scendere::stat_histogram *) override
	{
		log << boost::format ("%02d:%02d:%02d") % tm.tm_hour % tm.tm_min % tm.tm_sec << "," << type << "," << detail << "," << dir << "," << value << std::endl;
	}

	void rotate () override
	{
		log.close ();
		log.open (filename.c_str (), std::ofstream::out);
		log_entries = 0;
	}
};

scendere::stat_histogram::stat_histogram (std::initializer_list<uint64_t> intervals_a, size_t bin_count_a)
{
	if (bin_count_a == 0)
	{
		debug_assert (intervals_a.size () > 1);
		uint64_t start_inclusive_l = *intervals_a.begin ();
		for (auto it = std::next (intervals_a.begin ()); it != intervals_a.end (); ++it)
		{
			uint64_t end_exclusive_l = *it;
			bins.emplace_back (start_inclusive_l, end_exclusive_l);
			start_inclusive_l = end_exclusive_l;
		}
	}
	else
	{
		debug_assert (intervals_a.size () == 2);
		uint64_t min_inclusive_l = *intervals_a.begin ();
		uint64_t max_exclusive_l = *std::next (intervals_a.begin ());

		auto domain_l = (max_exclusive_l - min_inclusive_l);
		auto bin_size_l = (domain_l + bin_count_a - 1) / bin_count_a;
		auto last_bin_size_l = (domain_l % bin_size_l);
		auto next_start_l = min_inclusive_l;

		for (size_t i = 0; i < bin_count_a; i++, next_start_l += bin_size_l)
		{
			bins.emplace_back (next_start_l, next_start_l + bin_size_l);
		}
		if (last_bin_size_l > 0)
		{
			bins.emplace_back (next_start_l, next_start_l + last_bin_size_l);
		}
	}
}

void scendere::stat_histogram::add (uint64_t index_a, uint64_t addend_a)
{
	scendere::lock_guard<scendere::mutex> lk (histogram_mutex);
	debug_assert (!bins.empty ());

	// The search for a bin is linear, but we're searching just a few
	// contiguous items which are likely to be in cache.
	bool found_l = false;
	for (auto & bin : bins)
	{
		if (index_a >= bin.start_inclusive && index_a < bin.end_exclusive)
		{
			bin.value += addend_a;
			bin.timestamp = std::chrono::system_clock::now ();
			found_l = true;
			break;
		}
	}

	// Clamp into first or last bin if no suitable bin was found
	if (!found_l)
	{
		if (index_a < bins.front ().start_inclusive)
		{
			bins.front ().value += addend_a;
		}
		else
		{
			bins.back ().value += addend_a;
		}
	}
}

std::vector<scendere::stat_histogram::bin> scendere::stat_histogram::get_bins () const
{
	scendere::lock_guard<scendere::mutex> lk (histogram_mutex);
	return bins;
}

scendere::stat::stat (scendere::stat_config config) :
	config (config)
{
}

std::shared_ptr<scendere::stat_entry> scendere::stat::get_entry (uint32_t key)
{
	return get_entry (key, config.interval, config.capacity);
}

std::shared_ptr<scendere::stat_entry> scendere::stat::get_entry (uint32_t key, size_t interval, size_t capacity)
{
	scendere::unique_lock<scendere::mutex> lock (stat_mutex);
	return get_entry_impl (key, interval, capacity);
}

std::shared_ptr<scendere::stat_entry> scendere::stat::get_entry_impl (uint32_t key, size_t interval, size_t capacity)
{
	std::shared_ptr<scendere::stat_entry> res;
	auto entry = entries.find (key);
	if (entry == entries.end ())
	{
		res = entries.emplace (key, std::make_shared<scendere::stat_entry> (capacity, interval)).first->second;
	}
	else
	{
		res = entry->second;
	}

	return res;
}

std::unique_ptr<scendere::stat_log_sink> scendere::stat::log_sink_json () const
{
	return std::make_unique<json_writer> ();
}

void scendere::stat::log_counters (stat_log_sink & sink)
{
	scendere::unique_lock<scendere::mutex> lock (stat_mutex);
	log_counters_impl (sink);
}

void scendere::stat::log_counters_impl (stat_log_sink & sink)
{
	sink.begin ();
	if (sink.entries () >= config.log_rotation_count)
	{
		sink.rotate ();
	}

	if (config.log_headers)
	{
		auto walltime (std::chrono::system_clock::now ());
		sink.write_header ("counters", walltime);
	}

	for (auto & it : entries)
	{
		std::time_t time = std::chrono::system_clock::to_time_t (it.second->counter.get_timestamp ());
		tm local_tm = *localtime (&time);

		auto key = it.first;
		std::string type = type_to_string (key);
		std::string detail = detail_to_string (key);
		std::string dir = dir_to_string (key);
		sink.write_entry (local_tm, type, detail, dir, it.second->counter.get_value (), it.second->histogram.get ());
	}
	sink.entries ()++;
	sink.finalize ();
}

void scendere::stat::log_samples (stat_log_sink & sink)
{
	scendere::unique_lock<scendere::mutex> lock (stat_mutex);
	log_samples_impl (sink);
}

void scendere::stat::log_samples_impl (stat_log_sink & sink)
{
	sink.begin ();
	if (sink.entries () >= config.log_rotation_count)
	{
		sink.rotate ();
	}

	if (config.log_headers)
	{
		auto walltime (std::chrono::system_clock::now ());
		sink.write_header ("samples", walltime);
	}

	for (auto & it : entries)
	{
		auto key = it.first;
		std::string type = type_to_string (key);
		std::string detail = detail_to_string (key);
		std::string dir = dir_to_string (key);

		for (auto & datapoint : it.second->samples)
		{
			std::time_t time = std::chrono::system_clock::to_time_t (datapoint.get_timestamp ());
			tm local_tm = *localtime (&time);
			sink.write_entry (local_tm, type, detail, dir, datapoint.get_value (), nullptr);
		}
	}
	sink.entries ()++;
	sink.finalize ();
}

void scendere::stat::define_histogram (stat::type type, stat::detail detail, stat::dir dir, std::initializer_list<uint64_t> intervals_a, size_t bin_count_a /*=0*/)
{
	auto entry (get_entry (key_of (type, detail, dir)));
	entry->histogram = std::make_unique<scendere::stat_histogram> (intervals_a, bin_count_a);
}

void scendere::stat::update_histogram (stat::type type, stat::detail detail, stat::dir dir, uint64_t index_a, uint64_t addend_a)
{
	auto entry (get_entry (key_of (type, detail, dir)));
	debug_assert (entry->histogram != nullptr);
	entry->histogram->add (index_a, addend_a);
}

scendere::stat_histogram * scendere::stat::get_histogram (stat::type type, stat::detail detail, stat::dir dir)
{
	auto entry (get_entry (key_of (type, detail, dir)));
	debug_assert (entry->histogram != nullptr);
	return entry->histogram.get ();
}

void scendere::stat::update (uint32_t key_a, uint64_t value)
{
	static file_writer log_count (config.log_counters_filename);
	static file_writer log_sample (config.log_samples_filename);

	auto now (std::chrono::steady_clock::now ());

	scendere::unique_lock<scendere::mutex> lock (stat_mutex);
	if (!stopped)
	{
		auto entry (get_entry_impl (key_a, config.interval, config.capacity));

		// Counters
		auto old (entry->counter.get_value ());
		entry->counter.add (value);
		entry->count_observers.notify (old, entry->counter.get_value ());

		std::chrono::duration<double, std::milli> duration = now - log_last_count_writeout;
		if (config.log_interval_counters > 0 && duration.count () > config.log_interval_counters)
		{
			log_counters_impl (log_count);
			log_last_count_writeout = now;
		}

		// Samples
		if (config.sampling_enabled && entry->sample_interval > 0)
		{
			entry->sample_current.add (value, false);

			std::chrono::duration<double, std::milli> duration = now - entry->sample_start_time;
			if (duration.count () > entry->sample_interval)
			{
				entry->sample_start_time = now;

				// Make a snapshot of samples for thread safety and to get a stable container
				entry->sample_current.set_timestamp (std::chrono::system_clock::now ());
				entry->samples.push_back (entry->sample_current);
				entry->sample_current.set_value (0);

				if (!entry->sample_observers.observers.empty ())
				{
					auto snapshot (entry->samples);
					entry->sample_observers.notify (snapshot);
				}

				// Log sink
				duration = now - log_last_sample_writeout;
				if (config.log_interval_samples > 0 && duration.count () > config.log_interval_samples)
				{
					log_samples_impl (log_sample);
					log_last_sample_writeout = now;
				}
			}
		}
	}
}

std::chrono::seconds scendere::stat::last_reset ()
{
	scendere::unique_lock<scendere::mutex> lock (stat_mutex);
	auto now (std::chrono::steady_clock::now ());
	return std::chrono::duration_cast<std::chrono::seconds> (now - timestamp);
}

void scendere::stat::stop ()
{
	scendere::lock_guard<scendere::mutex> guard (stat_mutex);
	stopped = true;
}

void scendere::stat::clear ()
{
	scendere::unique_lock<scendere::mutex> lock (stat_mutex);
	entries.clear ();
	timestamp = std::chrono::steady_clock::now ();
}

std::string scendere::stat::type_to_string (uint32_t key)
{
	auto type = static_cast<stat::type> (key >> 16 & 0x000000ff);
	std::string res;
	switch (type)
	{
		case scendere::stat::type::ipc:
			res = "ipc";
			break;
		case scendere::stat::type::block:
			res = "block";
			break;
		case scendere::stat::type::bootstrap:
			res = "bootstrap";
			break;
		case scendere::stat::type::error:
			res = "error";
			break;
		case scendere::stat::type::http_callback:
			res = "http_callback";
			break;
		case scendere::stat::type::ledger:
			res = "ledger";
			break;
		case scendere::stat::type::tcp:
			res = "tcp";
			break;
		case scendere::stat::type::udp:
			res = "udp";
			break;
		case scendere::stat::type::peering:
			res = "peering";
			break;
		case scendere::stat::type::rollback:
			res = "rollback";
			break;
		case scendere::stat::type::traffic_udp:
			res = "traffic_udp";
			break;
		case scendere::stat::type::traffic_tcp:
			res = "traffic_tcp";
			break;
		case scendere::stat::type::vote:
			res = "vote";
			break;
		case scendere::stat::type::election:
			res = "election";
			break;
		case scendere::stat::type::message:
			res = "message";
			break;
		case scendere::stat::type::confirmation_observer:
			res = "observer";
			break;
		case scendere::stat::type::confirmation_height:
			res = "confirmation_height";
			break;
		case scendere::stat::type::drop:
			res = "drop";
			break;
		case scendere::stat::type::aggregator:
			res = "aggregator";
			break;
		case scendere::stat::type::requests:
			res = "requests";
			break;
		case scendere::stat::type::filter:
			res = "filter";
			break;
		case scendere::stat::type::telemetry:
			res = "telemetry";
			break;
		case scendere::stat::type::vote_generator:
			res = "vote_generator";
			break;
	}
	return res;
}

std::string scendere::stat::detail_to_string (stat::detail detail)
{
	std::string res;
	switch (detail)
	{
		case scendere::stat::detail::all:
			res = "all";
			break;
		case scendere::stat::detail::bad_sender:
			res = "bad_sender";
			break;
		case scendere::stat::detail::bulk_pull:
			res = "bulk_pull";
			break;
		case scendere::stat::detail::bulk_pull_account:
			res = "bulk_pull_account";
			break;
		case scendere::stat::detail::bulk_pull_deserialize_receive_block:
			res = "bulk_pull_deserialize_receive_block";
			break;
		case scendere::stat::detail::bulk_pull_error_starting_request:
			res = "bulk_pull_error_starting_request";
			break;
		case scendere::stat::detail::bulk_pull_failed_account:
			res = "bulk_pull_failed_account";
			break;
		case scendere::stat::detail::bulk_pull_receive_block_failure:
			res = "bulk_pull_receive_block_failure";
			break;
		case scendere::stat::detail::bulk_pull_request_failure:
			res = "bulk_pull_request_failure";
			break;
		case scendere::stat::detail::bulk_push:
			res = "bulk_push";
			break;
		case scendere::stat::detail::active_quorum:
			res = "observer_confirmation_active_quorum";
			break;
		case scendere::stat::detail::active_conf_height:
			res = "observer_confirmation_active_conf_height";
			break;
		case scendere::stat::detail::inactive_conf_height:
			res = "observer_confirmation_inactive";
			break;
		case scendere::stat::detail::error_socket_close:
			res = "error_socket_close";
			break;
		case scendere::stat::detail::request_underflow:
			res = "request_underflow";
			break;
		case scendere::stat::detail::change:
			res = "change";
			break;
		case scendere::stat::detail::confirm_ack:
			res = "confirm_ack";
			break;
		case scendere::stat::detail::node_id_handshake:
			res = "node_id_handshake";
			break;
		case scendere::stat::detail::confirm_req:
			res = "confirm_req";
			break;
		case scendere::stat::detail::fork:
			res = "fork";
			break;
		case scendere::stat::detail::old:
			res = "old";
			break;
		case scendere::stat::detail::gap_previous:
			res = "gap_previous";
			break;
		case scendere::stat::detail::gap_source:
			res = "gap_source";
			break;
		case scendere::stat::detail::rollback_failed:
			res = "rollback_failed";
			break;
		case scendere::stat::detail::frontier_confirmation_failed:
			res = "frontier_confirmation_failed";
			break;
		case scendere::stat::detail::frontier_confirmation_successful:
			res = "frontier_confirmation_successful";
			break;
		case scendere::stat::detail::frontier_req:
			res = "frontier_req";
			break;
		case scendere::stat::detail::handshake:
			res = "handshake";
			break;
		case scendere::stat::detail::http_callback:
			res = "http_callback";
			break;
		case scendere::stat::detail::initiate:
			res = "initiate";
			break;
		case scendere::stat::detail::initiate_legacy_age:
			res = "initiate_legacy_age";
			break;
		case scendere::stat::detail::initiate_lazy:
			res = "initiate_lazy";
			break;
		case scendere::stat::detail::initiate_wallet_lazy:
			res = "initiate_wallet_lazy";
			break;
		case scendere::stat::detail::insufficient_work:
			res = "insufficient_work";
			break;
		case scendere::stat::detail::invocations:
			res = "invocations";
			break;
		case scendere::stat::detail::keepalive:
			res = "keepalive";
			break;
		case scendere::stat::detail::open:
			res = "open";
			break;
		case scendere::stat::detail::publish:
			res = "publish";
			break;
		case scendere::stat::detail::receive:
			res = "receive";
			break;
		case scendere::stat::detail::republish_vote:
			res = "republish_vote";
			break;
		case scendere::stat::detail::send:
			res = "send";
			break;
		case scendere::stat::detail::telemetry_req:
			res = "telemetry_req";
			break;
		case scendere::stat::detail::telemetry_ack:
			res = "telemetry_ack";
			break;
		case scendere::stat::detail::state_block:
			res = "state_block";
			break;
		case scendere::stat::detail::epoch_block:
			res = "epoch_block";
			break;
		case scendere::stat::detail::vote_valid:
			res = "vote_valid";
			break;
		case scendere::stat::detail::vote_replay:
			res = "vote_replay";
			break;
		case scendere::stat::detail::vote_indeterminate:
			res = "vote_indeterminate";
			break;
		case scendere::stat::detail::vote_invalid:
			res = "vote_invalid";
			break;
		case scendere::stat::detail::vote_overflow:
			res = "vote_overflow";
			break;
		case scendere::stat::detail::vote_new:
			res = "vote_new";
			break;
		case scendere::stat::detail::vote_cached:
			res = "vote_cached";
			break;
		case scendere::stat::detail::late_block:
			res = "late_block";
			break;
		case scendere::stat::detail::late_block_seconds:
			res = "late_block_seconds";
			break;
		case scendere::stat::detail::election_start:
			res = "election_start";
			break;
		case scendere::stat::detail::election_block_conflict:
			res = "election_block_conflict";
			break;
		case scendere::stat::detail::election_difficulty_update:
			res = "election_difficulty_update";
			break;
		case scendere::stat::detail::election_drop_expired:
			res = "election_drop_expired";
			break;
		case scendere::stat::detail::election_drop_overflow:
			res = "election_drop_overflow";
			break;
		case scendere::stat::detail::election_drop_all:
			res = "election_drop_all";
			break;
		case scendere::stat::detail::election_restart:
			res = "election_restart";
			break;
		case scendere::stat::detail::election_confirmed:
			res = "election_confirmed";
			break;
		case scendere::stat::detail::election_not_confirmed:
			res = "election_not_confirmed";
			break;
		case scendere::stat::detail::blocking:
			res = "blocking";
			break;
		case scendere::stat::detail::overflow:
			res = "overflow";
			break;
		case scendere::stat::detail::tcp_accept_success:
			res = "accept_success";
			break;
		case scendere::stat::detail::tcp_accept_failure:
			res = "accept_failure";
			break;
		case scendere::stat::detail::tcp_write_drop:
			res = "tcp_write_drop";
			break;
		case scendere::stat::detail::tcp_write_no_socket_drop:
			res = "tcp_write_no_socket_drop";
			break;
		case scendere::stat::detail::tcp_excluded:
			res = "tcp_excluded";
			break;
		case scendere::stat::detail::tcp_max_per_ip:
			res = "tcp_max_per_ip";
			break;
		case scendere::stat::detail::tcp_max_per_subnetwork:
			res = "tcp_max_per_subnetwork";
			break;
		case scendere::stat::detail::tcp_silent_connection_drop:
			res = "tcp_silent_connection_drop";
			break;
		case scendere::stat::detail::tcp_io_timeout_drop:
			res = "tcp_io_timeout_drop";
			break;
		case scendere::stat::detail::tcp_connect_error:
			res = "tcp_connect_error";
			break;
		case scendere::stat::detail::tcp_read_error:
			res = "tcp_read_error";
			break;
		case scendere::stat::detail::tcp_write_error:
			res = "tcp_write_error";
			break;
		case scendere::stat::detail::unreachable_host:
			res = "unreachable_host";
			break;
		case scendere::stat::detail::invalid_header:
			res = "invalid_header";
			break;
		case scendere::stat::detail::invalid_message_type:
			res = "invalid_message_type";
			break;
		case scendere::stat::detail::invalid_keepalive_message:
			res = "invalid_keepalive_message";
			break;
		case scendere::stat::detail::invalid_publish_message:
			res = "invalid_publish_message";
			break;
		case scendere::stat::detail::invalid_confirm_req_message:
			res = "invalid_confirm_req_message";
			break;
		case scendere::stat::detail::invalid_confirm_ack_message:
			res = "invalid_confirm_ack_message";
			break;
		case scendere::stat::detail::invalid_node_id_handshake_message:
			res = "invalid_node_id_handshake_message";
			break;
		case scendere::stat::detail::invalid_telemetry_req_message:
			res = "invalid_telemetry_req_message";
			break;
		case scendere::stat::detail::invalid_telemetry_ack_message:
			res = "invalid_telemetry_ack_message";
			break;
		case scendere::stat::detail::outdated_version:
			res = "outdated_version";
			break;
		case scendere::stat::detail::udp_max_per_ip:
			res = "udp_max_per_ip";
			break;
		case scendere::stat::detail::udp_max_per_subnetwork:
			res = "udp_max_per_subnetwork";
			break;
		case scendere::stat::detail::blocks_confirmed:
			res = "blocks_confirmed";
			break;
		case scendere::stat::detail::blocks_confirmed_unbounded:
			res = "blocks_confirmed_unbounded";
			break;
		case scendere::stat::detail::blocks_confirmed_bounded:
			res = "blocks_confirmed_bounded";
			break;
		case scendere::stat::detail::aggregator_accepted:
			res = "aggregator_accepted";
			break;
		case scendere::stat::detail::aggregator_dropped:
			res = "aggregator_dropped";
			break;
		case scendere::stat::detail::requests_cached_hashes:
			res = "requests_cached_hashes";
			break;
		case scendere::stat::detail::requests_generated_hashes:
			res = "requests_generated_hashes";
			break;
		case scendere::stat::detail::requests_cached_votes:
			res = "requests_cached_votes";
			break;
		case scendere::stat::detail::requests_generated_votes:
			res = "requests_generated_votes";
			break;
		case scendere::stat::detail::requests_cached_late_hashes:
			res = "requests_cached_late_hashes";
			break;
		case scendere::stat::detail::requests_cached_late_votes:
			res = "requests_cached_late_votes";
			break;
		case scendere::stat::detail::requests_cannot_vote:
			res = "requests_cannot_vote";
			break;
		case scendere::stat::detail::requests_unknown:
			res = "requests_unknown";
			break;
		case scendere::stat::detail::duplicate_publish:
			res = "duplicate_publish";
			break;
		case scendere::stat::detail::different_genesis_hash:
			res = "different_genesis_hash";
			break;
		case scendere::stat::detail::invalid_signature:
			res = "invalid_signature";
			break;
		case scendere::stat::detail::node_id_mismatch:
			res = "node_id_mismatch";
			break;
		case scendere::stat::detail::request_within_protection_cache_zone:
			res = "request_within_protection_cache_zone";
			break;
		case scendere::stat::detail::no_response_received:
			res = "no_response_received";
			break;
		case scendere::stat::detail::unsolicited_telemetry_ack:
			res = "unsolicited_telemetry_ack";
			break;
		case scendere::stat::detail::failed_send_telemetry_req:
			res = "failed_send_telemetry_req";
			break;
		case scendere::stat::detail::generator_broadcasts:
			res = "generator_broadcasts";
			break;
		case scendere::stat::detail::generator_replies:
			res = "generator_replies";
			break;
		case scendere::stat::detail::generator_replies_discarded:
			res = "generator_replies_discarded";
			break;
		case scendere::stat::detail::generator_spacing:
			res = "generator_spacing";
			break;
		case scendere::stat::detail::invalid_network:
			res = "invalid_network";
			break;
	}
	return res;
}

std::string scendere::stat::detail_to_string (uint32_t key)
{
	auto detail = static_cast<stat::detail> (key >> 8 & 0x000000ff);
	return detail_to_string (detail);
}

std::string scendere::stat::dir_to_string (uint32_t key)
{
	auto dir = static_cast<stat::dir> (key & 0x000000ff);
	std::string res;
	switch (dir)
	{
		case scendere::stat::dir::in:
			res = "in";
			break;
		case scendere::stat::dir::out:
			res = "out";
			break;
	}
	return res;
}

scendere::stat_datapoint::stat_datapoint (stat_datapoint const & other_a)
{
	scendere::lock_guard<scendere::mutex> lock (other_a.datapoint_mutex);
	value = other_a.value;
	timestamp = other_a.timestamp;
}

scendere::stat_datapoint & scendere::stat_datapoint::operator= (stat_datapoint const & other_a)
{
	scendere::lock_guard<scendere::mutex> lock (other_a.datapoint_mutex);
	value = other_a.value;
	timestamp = other_a.timestamp;
	return *this;
}

uint64_t scendere::stat_datapoint::get_value () const
{
	scendere::lock_guard<scendere::mutex> lock (datapoint_mutex);
	return value;
}

void scendere::stat_datapoint::set_value (uint64_t value_a)
{
	scendere::lock_guard<scendere::mutex> lock (datapoint_mutex);
	value = value_a;
}

std::chrono::system_clock::time_point scendere::stat_datapoint::get_timestamp () const
{
	scendere::lock_guard<scendere::mutex> lock (datapoint_mutex);
	return timestamp;
}

void scendere::stat_datapoint::set_timestamp (std::chrono::system_clock::time_point timestamp_a)
{
	scendere::lock_guard<scendere::mutex> lock (datapoint_mutex);
	timestamp = timestamp_a;
}

/** Add \addend to the current value and optionally update the timestamp */
void scendere::stat_datapoint::add (uint64_t addend, bool update_timestamp)
{
	scendere::lock_guard<scendere::mutex> lock (datapoint_mutex);
	value += addend;
	if (update_timestamp)
	{
		timestamp = std::chrono::system_clock::now ();
	}
}
