#!/bin/bash

# Doc link checker
# Validates and tries to fix all links that will cause issues either in the repo, or in the html site

dbg () {
	if [ $VERBOSE -eq 1 ] ; then echo $1 ; fi
}

printhelp () {
	echo "Usage: htmldoc/checklinks.sh [-r OR -f <fname>] [OPTIONS]
	-r Recursively check all mds in all child directories, except htmldoc and node_modules (which is generated by netlify)
	-f Just check the passed md file
	Options:
	 -x Execute commands. By default the script runs in test mode with no files changed by the script (results and fixes are just shown). Use -x to have it apply the changes.
	 -u tests all absolute URLs
	 -v Outputs debugging messages
	"
}

fix () {
	if [ $EXECUTE -eq 0 ] ; then
		echo "  SHOULD EXECUTE: $1"
	else
		echo "  EXECUTING: $1"
		eval "$1"
	fi
}

replace_wikilink () {
	f=$1
	wikilnk=$2
	
	# The list for this case statement was created by executing in nedata.wiki repo the following:
	# grep 'Moved to ' * | sed 's/\.md.*http/ http/g' | sed 's/).*//g' | awk '{printf("*%s* ) newlnk=\"%s\" ;;\n",$1,$2);}'
 
	case "$wikilnk" in
		*Add-more-charts-to-netdata* ) newlnk="https://github.com/netdata/netdata/blob/master/doc/Add-more-charts-to-netdata.md#add-more-charts-to-netdata" ;;
		*alarm-notifications* ) newlnk="https://github.com/netdata/netdata/tree/master/health/notifications" ;;
		*Alerta-monitoring-system* ) newlnk="https://github.com/netdata/netdata/tree/master/health/notifications/alerta" ;;
		*Amazon-SNS-Notifications* ) newlnk="https://github.com/netdata/netdata/tree/master/health/notifications/awssns" ;;
		*apache.chart.sh* ) newlnk="https://github.com/netdata/netdata/tree/master/collectors/charts.d.plugin/apache" ;;
		*ap.chart.sh* ) newlnk="https://github.com/netdata/netdata/tree/master/collectors/charts.d.plugin/ap" ;;
		*Apps-Plugin* ) newlnk="https://github.com/netdata/netdata/tree/master/collectors/apps.plugin" ;;
		*Article:-Introducing-netdata* ) newlnk="https://github.com/netdata/netdata/tree/master/README.md" ;;
		*Chart-Libraries* ) newlnk="https://github.com/netdata/netdata/tree/master/web/gui/#netdata-agent-web-gui" ;;
		*Command-Line-Options* ) newlnk="https://github.com/netdata/netdata/tree/master/daemon#command-line-options" ;;
		*Configuration* ) newlnk="https://github.com/netdata/netdata/tree/master/daemon/config#configuration-guide" ;;
		*cpufreq.chart.sh* ) newlnk="https://github.com/netdata/netdata/tree/master/collectors/charts.d.plugin/cpufreq" ;;
		*Custom-Dashboards* ) newlnk="https://github.com/netdata/netdata/tree/master/web/gui/custom#custom-dashboards" ;;
		*Custom-Dashboard-with-Confluence* ) newlnk="https://github.com/netdata/netdata/tree/master/web/gui/confluence#atlassian-confluence-dashboards" ;;
		*discord-notifications* ) newlnk="https://github.com/netdata/netdata/tree/master/health/notifications/discord" ;;
		*Donations-netdata-has-received* ) newlnk="https://github.com/netdata/netdata/blob/master/doc/Donations-netdata-has-received.md#donations-received" ;;
		*Dygraph* ) newlnk="https://github.com/netdata/netdata/blob/master/web/gui/README.md#Dygraph" ;;
		*EasyPieChart* ) newlnk="https://github.com/netdata/netdata/blob/master/web/gui/README.md#EasyPieChart" ;;
		*email-notifications* ) newlnk="https://github.com/netdata/netdata/tree/master/health/notifications/email" ;;
		*example.chart.sh* ) newlnk="https://github.com/netdata/netdata/tree/master/collectors/charts.d.plugin/example" ;;
		*External-Plugins* ) newlnk="https://github.com/netdata/netdata/tree/master/collectors/plugins.d" ;;
		*flock-notifications* ) newlnk="https://github.com/netdata/netdata/tree/master/health/notifications/flock" ;;
		*fping-Plugin* ) newlnk="https://github.com/netdata/netdata/tree/master/collectors/fping.plugin" ;;
		*General-Info---charts.d* ) newlnk="https://github.com/netdata/netdata/tree/master/collectors/charts.d.plugin" ;;
		*General-Info---node.d* ) newlnk="https://github.com/netdata/netdata/tree/master/collectors/node.d.plugin" ;;
		*Generating-Badges* ) newlnk="https://github.com/netdata/netdata/tree/master/web/api/badges" ;;
		*health-API-calls* ) newlnk="https://github.com/netdata/netdata/tree/master/web/api/health#health-api-calls" ;;
		*health-configuration-examples* ) newlnk="https://github.com/netdata/netdata/tree/master/health#examples" ;;
		*health-configuration-reference* ) newlnk="https://github.com/netdata/netdata/tree/master/health" ;;
		*health-monitoring* ) newlnk="https://github.com/netdata/netdata/tree/master/health" ;;
		*high-performance-netdata* ) newlnk="https://github.com/netdata/netdata/blob/master/doc/high-performance-netdata.md#high-performance-netdata" ;;
		*Installation* ) newlnk="https://github.com/netdata/netdata/tree/master/installer#installation" ;;
		*Install-netdata-with-Docker* ) newlnk="https://github.com/netdata/netdata/tree/master/docker#install-netdata-with-docker" ;;
		*Internal-Plugins* ) newlnk="https://github.com/netdata/netdata/tree/master/collectors" ;;
		*IRC-notifications* ) newlnk="https://github.com/netdata/netdata/tree/master/health/notifications/irc" ;;
		*kavenegar-notifications* ) newlnk="https://github.com/netdata/netdata/tree/master/health/notifications/kavenegar" ;;
		*Linux-console-tools,-fail-to-report-per-process-CPU-usage-properly* ) newlnk="https://github.com/netdata/netdata/tree/master/collectors/apps.plugin#comparison-with-console-tools" ;;
		*Log-Files* ) newlnk="https://github.com/netdata/netdata/tree/master/daemon#log-files" ;;
		*Memory-Deduplication---Kernel-Same-Page-Merging---KSM* ) newlnk="https://github.com/netdata/netdata/tree/master/database#ksm" ;;
		*Memory-Requirements* ) newlnk="https://github.com/netdata/netdata/tree/master/database" ;;
		*messagebird-notifications* ) newlnk="https://github.com/netdata/netdata/tree/master/health/notifications/messagebird" ;;
		*Monitor-application-bandwidth-with-Linux-QoS* ) newlnk="https://github.com/netdata/netdata/tree/master/collectors/tc.plugin" ;;
		*monitoring-cgroups* ) newlnk="https://github.com/netdata/netdata/tree/master/collectors/cgroups.plugin" ;;
		*Monitoring-disks* ) newlnk="https://github.com/netdata/netdata/tree/master/collectors/proc.plugin#monitoring-disks" ;;
		*monitoring-ephemeral-containers* ) newlnk="https://github.com/netdata/netdata/tree/master/collectors/cgroups.plugin#monitoring-ephemeral-containers" ;;
		*Monitoring-ephemeral-nodes* ) newlnk="https://github.com/netdata/netdata/tree/master/streaming#monitoring-ephemeral-nodes" ;;
		*Monitoring-Go-Applications* ) newlnk="https://github.com/netdata/netdata/tree/master/collectors/python.d.plugin/go_expvar" ;;
		*monitoring-IPMI* ) newlnk="https://github.com/netdata/netdata/tree/master/collectors/freeipmi.plugin" ;;
		*Monitoring-Java-Spring-Boot-Applications* ) newlnk="https://github.com/netdata/netdata/tree/master/collectors/python.d.plugin/springboot#monitoring-java-spring-boot-applications" ;;
		*Monitoring-SYNPROXY* ) newlnk="https://github.com/netdata/netdata/blob/master/collectors/proc.plugin/README.md#linux-anti-ddos" ;;
		*monitoring-systemd-services* ) newlnk="https://github.com/netdata/netdata/tree/master/collectors/cgroups.plugin#monitoring-systemd-services" ;;
		*mynetdata-menu-item* ) newlnk="https://github.com/netdata/netdata/tree/master/registry" ;;
		*mysql.chart.sh* ) newlnk="https://github.com/netdata/netdata/tree/master/collectors/charts.d.plugin/mysql" ;;
		*named.node.js* ) newlnk="https://github.com/netdata/netdata/tree/master/collectors/node.d.plugin/named" ;;
		*netdata-backends* ) newlnk="https://github.com/netdata/netdata/tree/master/backends" ;;
		*netdata-for-IoT* ) newlnk="https://github.com/netdata/netdata/blob/master/doc/netdata-for-IoT.md#netdata-for-iot" ;;
		*netdata-OOMScore* ) newlnk="https://github.com/netdata/netdata/tree/master/daemon#oom-score" ;;
		*netdata-package-maintainers* ) newlnk="https://github.com/netdata/netdata/tree/master/packaging/maintainers#package-maintainers" ;;
		*netdata-process-priority* ) newlnk="https://github.com/netdata/netdata/tree/master/daemon#netdata-process-scheduling-policy" ;;
		*Netdata,-Prometheus,-and-Grafana-Stack* ) newlnk="https://github.com/netdata/netdata/blob/master/backends/WALKTHROUGH.md#netdata-prometheus-grafana-stack" ;;
		*netdata-proxies* ) newlnk="https://github.com/netdata/netdata/tree/master/streaming#proxies" ;;
		*netdata-security* ) newlnk="https://github.com/netdata/netdata/blob/master/doc/netdata-security.md#netdata-security" ;;
		*netdata-virtual-memory-size* ) newlnk="https://github.com/netdata/netdata/tree/master/daemon#virtual-memory" ;;
		*Overview* ) newlnk="https://github.com/netdata/netdata/tree/master/web#web-dashboards-overview" ;;
		*pagerduty-notifications* ) newlnk="https://github.com/netdata/netdata/tree/master/health/notifications/pagerduty" ;;
		*Performance* ) newlnk="https://github.com/netdata/netdata/blob/master/doc/Performance.md#netdata-performance" ;;
		*phpfpm.chart.sh* ) newlnk="https://github.com/netdata/netdata/tree/master/collectors/charts.d.plugin/phpfpm" ;;
		*pushbullet-notifications* ) newlnk="https://github.com/netdata/netdata/tree/master/health/notifications/pushbullet" ;;
		*pushover-notifications* ) newlnk="https://github.com/netdata/netdata/tree/master/health/notifications/pushover" ;;
		*receiving-netdata-metrics-from-shell-scripts* ) newlnk="https://github.com/netdata/netdata/tree/master/web/api#using-the-api-from-shell-scripts" ;;
		*Replication-Overview* ) newlnk="https://github.com/netdata/netdata/tree/master/streaming" ;;
		*REST-API-v1* ) newlnk="https://github.com/netdata/netdata/tree/master/web/api" ;;
		*RocketChat-notifications* ) newlnk="https://github.com/netdata/netdata/tree/master/health/notifications/rocketchat" ;;
		*Running-behind-apache* ) newlnk="https://github.com/netdata/netdata/blob/master/doc/Running-behind-apache.md#netdata-via-apaches-mod_proxy" ;;
		*Running-behind-caddy* ) newlnk="https://github.com/netdata/netdata/blob/master/doc/Running-behind-caddy.md#netdata-via-caddy" ;;
		*Running-behind-lighttpd* ) newlnk="httpd-v14x" ;;
		*Running-behind-nginx* ) newlnk="https://github.com/netdata/netdata/blob/master/doc/Running-behind-nginx.md#netdata-via-nginx" ;;
		*slack-notifications* ) newlnk="https://github.com/netdata/netdata/tree/master/health/notifications/slack" ;;
		*sma_webbox.node.js* ) newlnk="https://github.com/netdata/netdata/tree/master/collectors/node.d.plugin/sma_webbox" ;;
		*snmp.node.js* ) newlnk="https://github.com/netdata/netdata/tree/master/collectors/node.d.plugin/snmp" ;;
		*statsd* ) newlnk="https://github.com/netdata/netdata/tree/master/collectors/statsd.plugin" ;;
		*Syslog-Notifications* ) newlnk="https://github.com/netdata/netdata/tree/master/health/notifications/syslog" ;;
		*telegram-notifications* ) newlnk="https://github.com/netdata/netdata/tree/master/health/notifications/telegram" ;;
		*The-spectacles-of-a-web-server-log-file* ) newlnk="https://github.com/netdata/netdata/blob/master/collectors/python.d.plugin/web_log/README.md#web_log" ;;
		*Third-Party-Plugins* ) newlnk="https://github.com/netdata/netdata/blob/master/doc/Third-Party-Plugins.md#third-party-plugins" ;;
		*tomcat.chart.sh* ) newlnk="https://github.com/netdata/netdata/tree/master/collectors/charts.d.plugin/tomcat" ;;
		*Tracing-Options* ) newlnk="https://github.com/netdata/netdata/tree/master/daemon#debugging" ;;
		*troubleshooting-alarms* ) newlnk="https://github.com/netdata/netdata/tree/master/health#troubleshooting" ;;
		*twilio-notifications* ) newlnk="https://github.com/netdata/netdata/tree/master/health/notifications/twilio" ;;
		*Using-Netdata-with-Prometheus* ) newlnk="https://github.com/netdata/netdata/tree/master/backends/prometheus" ;;
		*web-browser-notifications* ) newlnk="https://github.com/netdata/netdata/tree/master/health/notifications/web" ;;
		*Why-netdata* ) newlnk="https://github.com/netdata/netdata/tree/master/doc/Why-Netdata.md" ;;
		*Writing-Plugins* ) newlnk="https://github.com/netdata/netdata/tree/master/collectors/plugins.d" ;;
		*You-should-install-QoS-on-all-your-servers* ) newlnk="https://github.com/netdata/netdata/tree/master/collectors/tc.plugin#tcplugin" ;;
		* ) 
			echo "  WARNING: Couldn't map $wikilnk to a known replacement. Please replace by hand in $f" 
			return
		;;
	esac
	ck_netdata_absolute $f $newlnk $wikilnk
}

ck_netdata_absolute () {
	f=$1
	alnk=$2
	lnkinfile=$3
	testURL $alnk
	if [ $? -eq 0 ] ; then 
		rlnk=$(echo $alnk | sed 's/https:\/\/github.com\/netdata\/netdata\/....\/master\///g')
		case $rlnk in
			\#* ) dbg "  (#somelink)" ;;
			*/ ) dbg "  # (path/)" ;;
			*/#* ) dbg "  # (path/#somelink)" ;;
			*/*.md ) dbg "  # (path/filename.md)" ;;
			*/*.md#* ) dbg "  # (path/filename.md#somelink)" ;;
			*#* ) 
				dbg "  # (path#somelink) -> (path/#somelink)" 
				if [[ $rlnk =~ ^(.*)#(.*)$ ]] ; then
					dbg " $rlnk -> ${BASH_REMATCH[1]}/#${BASH_REMATCH[2]}"
					rlnk="${BASH_REMATCH[1]}/#${BASH_REMATCH[2]}" 
				fi
				;;		
			* )	
				if [[ $rlnk =~ *\.[^/]* ]] ; then
					dbg "  # (path/someotherfile) ${BASH_REMATCH[1]}"
				else
					dbg "  # (path) -> (path/)"
					rlnk="$rlnk/"
				fi
				;;
		esac

		if [[ $f =~ ^(.*)/([^/]*)$ ]] ; then
			fpath="${BASH_REMATCH[1]}"
			dbg "  Current file is at $fpath"
		fi
		if [[ $rlnk =~ ^(.*)/([^/]*)$ ]] ; then
			abspath="${BASH_REMATCH[1]}"
			rest="${BASH_REMATCH[2]}"
			dbg "  Target file is at $abspath"
		fi
		relativelink=$(realpath --relative-to=$fpath $abspath)
		
		srch=$(echo $lnkinfile | sed 's/\//\\\//g')
		rplc=$(echo $relativelink/$rest | sed 's/\//\\\//g')
		fix "sed -i 's/($srch)/($rplc)/g' $f"
	fi
}
testURL () {
	if [ $TESTURLS -eq 0 ] ; then return 0 ; fi
	dbg " Testing URL $1"
	curl -sS $1 > /dev/null
	if [ $? -gt 0 ] ; then
		echo "  ERROR: $1 is a broken link"
		return 1
	fi
	return 0
}

testinternal () {
	# Check if the header referred to by the internal link exists in the same file
	ifile=${1}
	ilnk=${2}
	header=$(echo $ilnk | sed 's/#/#-/g')
	dbg "  Searching for \"$header\" in $ifile"
	found=$(cat $ifile | tr -d ',_.:?' | tr ' ' '-' | grep -i "^\#*$header\$")
	if [ $? -eq 0 ] ; then
		dbg "  $ilnk found in $ifile"
		return 0
	else
		echo "  ERROR: $ilnk header not found in $ifile"
		return 1
	fi
}

testf () {
	if [ -f "$1" ] ; then 
		dbg "  $1 exists"
		return 0
	else
		echo "  ERROR: $1 not found"
		return 1
	fi	
}

ck_netdata_relative () {
	f=${1}
	rlnk=${2}
	dbg " Checking relative link $rlnk"
	# First ensure that the link works in the repo, then try to fix it in htmldocs
	if [[ $f =~ ^(.*)/([^/]*)$ ]] ; then
		fpath="${BASH_REMATCH[1]}"
		fname="${BASH_REMATCH[2]}"
		dbg "  Current file is at $fpath"
	fi
	# Cases to handle:
	# (#somelink)
	# (path/)
	# (path/#somelink)
	# (path/filename.md) -> htmldoc (path/filename/)
	# (path/filename.md#somelink) -> htmldoc (path/filename/#somelink)
	# (path#somelink) -> htmldoc (path/#somelink)
	# (path/someotherfile) -> htmldoc (absolutelink) 
	# (path) -> htmldoc (path/)

	TRGT=""
	s=""

	case "$rlnk" in
		\#* ) 
			dbg "  # (#somelink)"
			testinternal $f $rlnk
			;;
		*/ ) 
			dbg "  # (path/)"
			TRGT="$fpath/$rlnk/README.md"
			testf $TRGT
			if [ $? -eq 0 ] ; then
				if [ $fname != "README.md" ] ; then s="../$rlnk"; fi
			fi
			;;
		*/#* )
			dbg "  # (path/#somelink)"
			if [[ $rlnk =~ ^(.*)/#(.*)$ ]] ; then
				TRGT="$fpath/${BASH_REMATCH[1]}/README.md"
				LNK="#${BASH_REMATCH[2]}"
				dbg " Look for $LNK in $TRGT"
				testf $TRGT
				if [ $? -eq 0 ] ; then
					testinternal $TRGT $LNK
					if [ $? -eq 0 ] ; then
						if [ $fname != "README.md" ] ; then s="../$lnk"; fi
					fi
				fi
			fi
			;;
		*.md )
			dbg "  # (path/filename.md) -> htmldoc (path/filename/)"
			testf "$fpath/$rlnk"
			if [ $? -eq 0 ] ; then
				if [[ $rlnk =~ ^(.*)/(.*).md$ ]] ; then
					if [ "${BASH_REMATCH[2]}" = "README" ] ; then
						s="../${BASH_REMATCH[1]}/"
					else
						s="../${BASH_REMATCH[1]}/${BASH_REMATCH[2]}/"
					fi
					if [ $fname != "README.md" ] ; then s="../$s"; fi
				fi
			fi
			;;
		*/*.md#* )
			dbg "  # (path/filename.md#somelink) -> htmldoc (path/filename/#somelink)"
			if [[ $rlnk =~ ^(.*)#(.*)$ ]] ; then
				TRGT="$fpath/${BASH_REMATCH[1]}"
				LNK="#${BASH_REMATCH[2]}"
				testf $TRGT
				if [ $? -eq 0 ] ; then
					testinternal $TRGT $LNK
					if [ $? -eq 0 ] ; then
						if [[ $lnk =~ ^(.*)/(.*).md#(.*)$ ]] ; then
							if [ "${BASH_REMATCH[2]}" = "README" ] ; then
								s="../${BASH_REMATCH[1]}/#${BASH_REMATCH[3]}"
							else
								s="../${BASH_REMATCH[1]}/${BASH_REMATCH[2]}/#${BASH_REMATCH[3]}"
							fi
							if [ $fname != "README.md" ] ; then s="../$s"; fi
						fi			
					fi
				fi
			fi
			;;
		*#* )
			dbg "  # (path#somelink) -> (path/#somelink)"
			if [[ $rlnk =~ ^(.*)#(.*)$ ]] ; then
				TRGT="$fpath/${BASH_REMATCH[1]}/README.md"
				LNK="#${BASH_REMATCH[2]}"
				testf $TRGT
				if [ $? -eq 0 ] ; then
					testinternal $TRGT $LNK
					if [ $? -eq 0 ] ; then
						if [[ $rlnk =~ ^(.*)#(.*)$ ]] ; then
							s="${BASH_REMATCH[1]}/#${BASH_REMATCH[2]}"
							if [ $fname != "README.md" ] ; then s="../$s"; fi
						fi			
					fi
				fi
			fi
			;;
		* )
			if [[ $rlnk =~ *\.[^/]* ]] ; then
				dbg "  # (path/someotherfile) ${BASH_REMATCH[1]}"
				testf "$fpath/$rlnk"
				if [ $? -eq 0 ] ; then
					s="https://github.com/netdata/netdata/tree/master/$fpath/$rlnk"
				fi
			else
				dbg "  # (path) -> htmldoc (path/)"
				testf "$fpath/$rlnk/README.md"
				if [ $? -eq 0 ] ; then
					s="$rlnk/"
					if [ $fname != "README.md" ] ; then s="../$s"; fi
				fi
			fi
			;;
		esac
		
		if [[ ! -z $s ]] ; then
			dbg "  WARNING: Need to replace $rlnk with $s in htmldoc/src/$f"
			srch=$(echo $rlnk | sed 's/\//\\\//g')
			rplc=$(echo $s | sed 's/\//\\\//g')
			fix "sed -i 's/($srch)/($rplc)/g' htmldoc/src/$f"
		fi
}


checklinks () {
	f=$1
	dbg "Checking $f"
	while read l ; do
		for word in $l ; do
			if [[ $word =~ .*\]\(([^\(\) ]*)\).* ]] ; then
				lnk="${BASH_REMATCH[1]}"
				dbg " $lnk"
				case "$lnk" in
					https://github.com/netdata/netdata/wiki* ) 
						dbg "  $lnk points to the wiki" 
						replace_wikilink $f $lnk
					;;
					https://github.com/netdata/netdata/* ) 
						if [ $WIKIONLY -eq 0 ] ; then ck_netdata_absolute $f $lnk $lnk ; fi
					;;
					http* ) testURL $lnk ;;
					* ) if [ $WIKIONLY -eq 0 ] ; then ck_netdata_relative $f $lnk ; fi ;;
				esac
			fi
		done
	done < $f
}

REPLACE=0
TESTURLS=0
VERBOSE=0
RECURSIVE=0
EXECUTE=0
WIKIONLY=0
while getopts :f:rxuvw option
do
    case "$option" in
    f)
         file=$OPTARG
         ;;
	r)
		RECURSIVE=1
		;;
	x)
		EXECUTE=1
		;;
	u) 
		TESTURLS=1
		;;
	v)
		VERBOSE=1
		;;
	w)
		WIKIONLY=1
		;;
	*)
		printhelp
		exit
		;;
	esac
done

if [ -z ${file} ] ; then 
	if [ $RECURSIVE -eq 0 ] ; then 
		printhelp
		exit 1
	fi
	for f in $(find . -type d \( -path ./htmldoc -o -path ./node_modules \) -prune -o -name "*.md" -print); do
		checklinks $f
	done
else
	if [ $RECURSIVE -eq 1 ] ; then 
		printhelp
		exit 1
	fi	
	checklinks $file
fi

exit 0
