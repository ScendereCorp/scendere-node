#!/usr/bin/env sh

set -euf

usage() {
	printf "Usage:\n"
	printf "  $0 scendere_node [daemon] [cli_options] [-l] [-v size]\n"
	printf "    daemon\n"
	printf "      start as daemon\n\n"
	printf "    cli_options\n"
	printf "      scendere_node cli options <see scendere_node --help>\n\n"
	printf "    -l\n"
	printf "      log to console <use docker logs {container}>\n\n"
	printf "    -v<size>\n"
	printf "      vacuum database if over size GB on startup\n\n"
	printf "  $0 sh [other]\n"
	printf "    other\n"
	printf "      sh pass through\n"
	printf "  $0 [*]\n"
	printf "    *\n"
	printf "      usage\n\n"
	printf "default:\n"
	printf "  $0 scendere_node daemon -l\n"
}

OPTIND=1
command=""
passthrough=""
db_size=0
log_to_cerr=0

if [ $# -lt 2 ]; then
	usage
	exit 1
fi

if [ "$1" = 'scendere_node' ]; then
	command="${command}scendere_node"
	shift
	for i in $@; do
		case $i in
		"daemon")
			command="${command} --daemon"
			;;
		*)
			if [ ${#passthrough} -ge 0 ]; then
				passthrough="${passthrough} $i"
			else
				passthrough="$i"
			fi
			;;
		esac
	done
	for i in $passthrough; do
		case $i in
		*"-v"*)
			db_size=$(echo $i | tr -d -c 0-9)
			echo "Vacuum DB if over $db_size GB on startup"
			;;
		"-l")
			echo "log_to_cerr = true"
			command="${command} --config"
			command="${command} node.logging.log_to_cerr=true"
			;;
		*)
			command="${command} $i"
			;;
		esac
	done
elif [ "$1" = 'sh' ]; then
	shift
	command=""
	for i in $@; do
		if [ "$command" = "" ]; then
			command="$i"
		else
			command="${command} $i"
		fi
	done
	printf "EXECUTING: ${command}\n"
	exec $command
else
	usage
	exit 1
fi

network="$(cat /etc/scendere-network)"
case "${network}" in
live | '')
	network='live'
	dirSuffix=''
	;;
beta)
	dirSuffix='Beta'
	;;
dev)
	dirSuffix='Dev'
	;;
test)
	dirSuffix='Test'
	;;
esac

raidir="${HOME}/RaiBlocks${dirSuffix}"
scenderedir="${HOME}/Scendere${dirSuffix}"
dbFile="${scenderedir}/data.ldb"

if [ -d "${raidir}" ]; then
	echo "Moving ${raidir} to ${scenderedir}"
	mv "$raidir" "$scenderedir"
else
	mkdir -p "${scenderedir}"
fi

if [ ! -f "${scenderedir}/config-node.toml" ] && [ ! -f "${scenderedir}/config.json" ]; then
	echo "Config file not found, adding default."
	cp "/usr/share/scendere/config/config-node.toml" "${scenderedir}/config-node.toml"
	cp "/usr/share/scendere/config/config-rpc.toml" "${scenderedir}/config-rpc.toml"
fi

case $command in
*"--daemon"*)
	if [ $db_size -ne 0 ]; then
		if [ -f "${dbFile}" ]; then
			dbFileSize="$(stat -c %s "${dbFile}" 2>/dev/null)"
			if [ "${dbFileSize}" -gt $((1024 * 1024 * 1024 * db_size)) ]; then
				echo "ERROR: Database size grew above ${db_size}GB (size = ${dbFileSize})" >&2
				scendere_node --vacuum
			fi
		fi
	fi
	;;
esac

printf "EXECUTING: ${command}\n"
exec $command
