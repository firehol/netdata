# netdata [![Build Status](https://travis-ci.com/netdata/netdata.svg?branch=master)](https://travis-ci.com/netdata/netdata) [![CII Best Practices](https://bestpractices.coreinfrastructure.org/projects/2231/badge)](https://bestpractices.coreinfrastructure.org/projects/2231) [![License: GPL v3+](https://img.shields.io/badge/License-GPL%20v3%2B-blue.svg)](https://www.gnu.org/licenses/gpl-3.0)  
  
[![Code Climate](https://codeclimate.com/github/netdata/netdata/badges/gpa.svg)](https://codeclimate.com/github/netdata/netdata) [![Codacy Badge](https://api.codacy.com/project/badge/Grade/a994873f30d045b9b4b83606c3eb3498)](https://www.codacy.com/app/netdata/netdata?utm_source=github.com&amp;utm_medium=referral&amp;utm_content=netdata/netdata&amp;utm_campaign=Badge_Grade) [![LGTM C](https://img.shields.io/lgtm/grade/cpp/g/netdata/netdata.svg?logo=lgtm)](https://lgtm.com/projects/g/netdata/netdata/context:cpp) [![LGTM JS](https://img.shields.io/lgtm/grade/javascript/g/netdata/netdata.svg?logo=lgtm)](https://lgtm.com/projects/g/netdata/netdata/context:javascript) [![LGTM PYTHON](https://img.shields.io/lgtm/grade/python/g/netdata/netdata.svg?logo=lgtm)](https://lgtm.com/projects/g/netdata/netdata/context:python)

---

### **WARNING**
People get adicted to **netdata**!<br/>
Once you use it on your systems, **there is no going back**! *You have been warned...*

![image](https://user-images.githubusercontent.com/2662304/48305662-9de82980-e537-11e8-9f5b-aa1a60fbb82f.png)

---

**Netdata** is a system for **distributed real-time performance and health monitoring**.  
  
It provides **unparalleled insights**, **in real-time**, of everything happening on the systems it runs (including containers and applications such as web and database servers), using **modern interactive web dashboards**.  

_Netdata is **fast** and **efficient**, designed to permanently run on all systems (**physical** & **virtual** servers, **containers**, **IoT** devices), without disrupting their core function._
  
Netdata currently runs on **Linux**, **FreeBSD**, and **MacOS**.  

[![Twitter Follow](https://img.shields.io/twitter/follow/linuxnetdata.svg?style=social&label=New%20-%20stay%20in%20touch%20-%20follow%20netdata%20on%20twitter)](https://twitter.com/linuxnetdata) [![analytics](http://www.google-analytics.com/collect?v=1&t=pageview&_s=1&ds=github&dr=https%3A%2F%2Fgithub.com%2Fnetdata%2Fnetdata&dl=https%3A%2F%2Fmy-netdata.io%2Fgithub%2Freadme&_u=MAC~&cid=5792dfd7-8dc4-476b-af31-da2fdb9f93d2&tid=UA-64295674-3)]()  


## Contents

1. [How it looks](#how-it-looks) - have a quick look at it
2. [User base](#user-base) - who uses netdata?
3. [Why Netdata](#why-netdata) - why people love netdata, how it compares with other solutions
4. [Quick Start](#quick-start) - try it now on your systems
5. [News](#news) - latest news about netdata
6. [infographic](#infographic) - everything about netdata, in a page
7. [Features](#features) - what features does it have
8. [What does it monitor](#what-does-it-monitor) - which metrics it collects
9. [Installation](#installation) - install it on all your systems
10. [Documentation](#documentation) - read the docs
11. [Community](#community) - disucss with others and get support
12. [License](#license) - check the license of netdata


## How it looks

The following animated image, shows the top part of a typical netdata dashboard.

![peek 2018-11-11 02-40](https://user-images.githubusercontent.com/2662304/48307727-9175c800-e55b-11e8-92d8-a581d60a4889.gif)

*A typical netdata dashboard, in 1:1 timing. Charts can be panned by dragging them, zoomed in/out with `SHIFT` + `mouse wheel`, an area can be selected for zoom-in with `SHIFT` + `mouse selection`. Netdata is highly interactive and **real-time**, optimized to get the work done!*

> *We have a few online demos to check: [http://my-netdata.io](http://my-netdata.io)*  

## User base

![cncf](https://www.cncf.io/wp-content/uploads/2016/09/logo_cncf.png)  

Netdata is in the [Cloud Native Computing Foundation (CNCF) landscape](https://landscape.cncf.io/grouping=no&sort=stars).
Check the [CNCF TOC Netdata presentation](https://docs.google.com/presentation/d/18C8bCTbtgKDWqPa57GXIjB2PbjjpjsUNkLtZEz6YK8s/edit?usp=sharing).  

Netdata is a **robust** application. It has hundreds of thousands of users, all over the world.
Check our [GitHub watchers list](https://github.com/netdata/netdata/watchers).
You will find users working for: **Amazon**, **Atos**, **Baidu**, **Cisco Systems**, **Citrix**, **Deutsche Telekom**, **DigitalOcean**, 
**Elastic**, **EPAM Systems**, **Ericsson**, **Google**, **Groupon**, **Hortonworks**, **HP**, **Huawei**,
**IBM**, **Microsoft**, **NewRelic**, **Nvidia**, **Red Hat**, **SAP**, **Selectel**, **TicketMaster**,
**Vimeo**, and many more!

#### Docker pulls
Docker pulls as reported by docker hub:<br/>[![netdata/netdata (official)](https://img.shields.io/docker/pulls/netdata/netdata.svg?label=netdata/netdata+%28official%29)](https://hub.docker.com/r/netdata/netdata/) [![firehol/netdata (deprecated)](https://img.shields.io/docker/pulls/firehol/netdata.svg?label=firehol/netdata+%28deprecated%29)](https://hub.docker.com/r/firehol/netdata/) [![titpetric/netdata (donated)](https://img.shields.io/docker/pulls/titpetric/netdata.svg?label=titpetric/netdata+%28third+party%29)](https://hub.docker.com/r/titpetric/netdata/)

#### Anonymous global public netdata registry
*Since May 16th 2016 (the date the [global public netdata registry](https://github.com/netdata/netdata/wiki/mynetdata-menu-item) was released):*<br/>[![User Base](https://registry.my-netdata.io/api/v1/badge.svg?chart=netdata.registry_entries&dimensions=persons&label=user%20base&units=null&value_color=blue&precision=0&v42)](https://registry.my-netdata.io/#menu_netdata_submenu_registry) [![Monitored Servers](https://registry.my-netdata.io/api/v1/badge.svg?chart=netdata.registry_entries&dimensions=machines&label=servers%20monitored&units=null&value_color=orange&precision=0&v42)](https://registry.my-netdata.io/#menu_netdata_submenu_registry) [![Sessions Served](https://registry.my-netdata.io/api/v1/badge.svg?chart=netdata.registry_sessions&label=sessions%20served&units=null&value_color=yellowgreen&precision=0&v42)](https://registry.my-netdata.io/#menu_netdata_submenu_registry)  
  
*in the last 24 hours:*<br/> [![New Users Today](http://registry.my-netdata.io/api/v1/badge.svg?chart=netdata.registry_entries&dimensions=persons&after=-86400&options=unaligned&group=incremental-sum&label=new%20users%20today&units=null&value_color=blue&precision=0&v42)](https://registry.my-netdata.io/#menu_netdata_submenu_registry) [![New Machines Today](https://registry.my-netdata.io/api/v1/badge.svg?chart=netdata.registry_entries&dimensions=machines&group=incremental-sum&after=-86400&options=unaligned&label=servers%20added%20today&units=null&value_color=orange&precision=0&v42)](https://registry.my-netdata.io/#menu_netdata_submenu_registry) [![Sessions Today](https://registry.my-netdata.io/api/v1/badge.svg?chart=netdata.registry_sessions&after=-86400&group=incremental-sum&options=unaligned&label=sessions%20served%20today&units=null&value_color=yellowgreen&precision=0&v42)](https://registry.my-netdata.io/#menu_netdata_submenu_registry)  

## Why Netdata

Netdata is a monitoring agent you install on all your systems.

It is:

- a **metrics collector** - for system and application metrics (including web servers, databases, containers, etc)
- a **time-series database** - all stored in memory (does not touch the disks while it runs)
- a **metrics visualizer** - super fast, interactive, modern, optimized for anomaly detection
- an **alarms notification engine** - an advanced watchdog for detecting performance and availability issues

All the above, packaged together in a very flexible, extremely modular, distributed application.

This is how netdata compares to other monitoring solutions:

netdata|others (open-source and commercial)
:---:|:---:
**High resolution metrics** (1s granularity)|Low resolution metrics (10s granularity at best)
Monitors everything, **thousands of metrics per node**|Monitor just a few metrics
UI is super fast, optimized for **anomaly detection**|UI is good for just an abstract view
**Meaningful presentation**, to help you understand the metrics|You have to know the metrics before you start
Install and get results **immediately**|Long preparation is required to get any useful results
Use it for **troubleshooting** performance problems|Use them to get *statistics of past performance*
**Kills the console** for tracing performance issues|The console is always required for troubleshooting
Requires **zero dedicated resources**|Require large dedicated resources

Netdata is **open-source**, **free**, super **fast**, very **easy**, completely **open**, extremely **efficient**,
**flexible** and integrate-able.

It has been designed by **SysAdmins**, **DevOps** and **Developers** for troubleshooting performance problems,
not just visualize metrics.

## Quick Start

You can quickly install netdata on a Linux box (physical, virtual, container, IoT) with the following command:
 
```sh
# make sure you run `bash` for your shell
bash

# install netdata, directly from github sources
bash <(curl -Ss https://my-netdata.io/kickstart.sh)
```
![](http://registry.my-netdata.io/api/v1/badge.svg?chart=web_log_nginx.requests_per_url&options=unaligned&dimensions=kickstart&group=sum&after=-3600&label=last+hour&units=installations&value_color=orange&precision=0) ![](http://registry.my-netdata.io/api/v1/badge.svg?chart=web_log_nginx.requests_per_url&options=unaligned&dimensions=kickstart&group=sum&after=-86400&label=today&units=installations&precision=0)

The above command will:

1. install any required packages on your system (it will ask you to confirm before doing so),
2. download netdata source to `/usr/src/netdata.git`
3. compile it, install it and start it

More installation methods and additional options can be found at the [installation page](https://github.com/netdata/netdata/wiki/Installation).

![image](https://user-images.githubusercontent.com/2662304/48304090-fd384080-e51b-11e8-80ae-eecb03118dda.png)


## News  
  
`Nov 6th, 2018` - **[netdata v1.11.0 released!](https://github.com/netdata/netdata/releases)**  
  
 - New query engine, supporting statistical functions.  
 - Fixed security issues identified by Red4Sec.com and Synacktiv.  
 - New Data Collection Modules: `rethinkdbs`, `proxysql`, `litespeed`, `uwsgi`, `unbound`, `powerdns`, `dockerd`, `puppet`, `logind`, `adaptec_raid`, `megacli`, `spigotmc`, `boinc`, `w1sensor`, `monit`, `linux_power_supplies`.  
 - Improved Data Collection Modules: `statsd.plugin`, `apps.plugin`, `freeipmi.plugin`, `proc.plugin`, `diskspace.plugin`, `freebsd.plugin`, `python.d.plugin`, `web_log`, `nginx_plus`, `ipfs`, `fail2ban`, `ceph`, `elasticsearch`, `nginx_plus`,  `redis`,   
 `beanstalk`, `mysql`, `varnish`, `couchdb`, `phpfpm`, `apache`, `icecast`, `mongodb`, `postgress`, `elasticsearch`, `mdstat`, `openvpn_log`, `snmp`, `nut`.  
  
 - Added alarms for detecting abnormally high load average, `TCP` `SYN` and `TCP` accept queue overflows, network interfaces congestion and alarms for `bcache`, `mdstat`, `apcupsd`, `mysql`.  
 - system alarms are now enabled on FreeBSD.  
 - New notification methods: **rocket.chat**, **Microsoft Teams**, **syslog**, **fleep.io**, **Amazon SNS**.  
  
 - and dozens more improvements, enhancements, features and compatibility fixes  
  
---  
  
`Sep 18, 2018` - **netdata has its own organization**  
  
Netdata used to be a [firehol.org](https://firehol.org) project, accessible as `firehol/netdata`.  
  
Netdata now has its own github organization `netdata`, so all github URLs are now `netdata/netdata`. The old github URLs, repo clones, forks, etc redirect automatically to the new repo.    

  
## Infographic  
  
This is a high level overview of netdata feature set and architecture.  
Click it to to interact with it (it has direct links to documentation).  
  
[![image](https://user-images.githubusercontent.com/2662304/47672043-a47eb480-dbb9-11e8-92a4-fa422d053309.png)](https://my-netdata.io/infographic.html)  


## Features  
<p align="center">  
<img src="https://cloud.githubusercontent.com/assets/2662304/19168687/f6a567be-8c19-11e6-8561-ce8d589e8346.gif"/>  
</p>  

This is what you should expect from Netdata:

### General
- **1s granularity** - the highest possible resolution for all metrics.
- **Unlimited metrics** - collects all the available metrics, the more the better.
- **1% CPU utilization of a single core** - it is super fast, unbelievably optimized.
- **A few MB of RAM** - by default it uses 25MB RAM. [You size it](database).
- **Zero disk I/O** - while it runs, it does not load or save anything (except `error` and `access` logs).
- **Zero configuration** - auto-detects everything, it can collect up to 10000 metrics per server out of the box.
- **Zero maintenance** - You just run it, it does the rest.
- **Zero dependencies** - it is even its own web server, for its static web files and its web API (though its plugins may require additional libraries, depending on the applications monitored).  
- **Scales to infinity** - you can install it on all your servers, containers, VMs and IoTs. Metrics are not centralized by default, so there is no limit.
- **Several operating modes** - Autonomous host monitoring (the default), headless data collector, forwarding proxy, store and forward proxy, central multi-host monitoring, in all possible configurations. Each node may have different metrics retention policy and run with or without health monitoring.

### Visualization
- **Stunning interactive dashboards** - mouse, touchpad and touch-screen friendly in 2 themes: `slate` (dark) and `white`.
- **Amazingly fast visualization** - responds to all queries in less than 1 ms per metric, even on low-end hardware.
- **Visual anomaly detection** - the dashboards are optimized for detecting anomalies visually.
- **Embeddable** - its charts can be embedded on your web pages, wikis and blogs. You can even use [Atlassian's Confluence as a monitoring dashboard](https://github.com/netdata/netdata/wiki/Custom-Dashboard-with-Confluence).
- **Customizable** - custom dashboards can be built using simple HTML (no javascript necessary).

### Health Monitoring & Alarms
- **Sophisticated alerting** - comes with hundreds of alarms, **out of the box**! Supports dynamic thresholds, hysteresis, alarm templates, multiple role-based notification methods.
- **Notifications**: email, slack.com, flock.com, pushover.net, pushbullet.com, telegram.org, twilio.com, messagebird.com.

### Integrations
- **time-series dbs** - can archive its metrics to `graphite`, `opentsdb`, `prometheus`, json document DBs, in the same or lower resolution (lower: to prevent it from congesting these servers due to the amount of data collected).


## What does it monitor  

Netdata data collection is **extensible** - you can monitor anything you can get a metric for.
Its [Plugin API](collectors/plugins.d) supports all programing languages (anything can be a netdata plugin, BASH, python, perl, node.js, java, Go, ruby, etc).

- For better performance, most system related plugins (cpu, memory, disks, filesystems, networking, etc) have been written in `C`.
- For faster development and easier contributions, most application related plugins (databases, web servers, etc) have been written in `python`.

#### APM (Application Performance Monitoring)
- **statsd** - [netdata is a fully featured statsd server](collectors/statsd.plugin).
- **go_expvar** - collects metrics exposed by applications written in the Go programming language using the expvar package.
- **Spring Boot** - monitors running Java Spring Boot applications that expose their metrics with the use of the Spring Boot Actuator included in Spring Boot library.

#### System Resources
- **CPU Utilization** - total and per core CPU usage.
- **Interrupts** - total and per core CPU interrupts.
- **SoftIRQs** - total and per core SoftIRQs.
- **SoftNet** - total and per core SoftIRQs related to network activity.
- **CPU Throttling** - collects per core CPU throttling.
- **CPU Frequency** - collects the current CPU frequency.
- **CPU Idle** - collects the time spent per processor state.
- **IdleJitter** - measures CPU latency.
- **Entropy** - random numbers pool, using in cryptography.
- **Interprocess Communication - IPC** - such as semaphores and semaphores arrays.

#### Memory
- **ram** - collects info about RAM usage.
- **swap** - collects info about swap memory usage.
- **available memory** - collects the amount of RAM available for userspace processes.
- **committed memory** - collects the amount of RAM committed to userspace processes.
- **Page Faults** - collects the system page faults (major and minor).
- **writeback memory** - collects the system dirty memory and writeback activity.
- **huge pages** - collects the amount of RAM used for huge pages.
- **KSM** - collects info about Kernel Same Merging (memory dedupper).
- **Numa** - collects Numa info on systems that support it.
- **slab** - collects info about the Linux kernel memory usage.

#### Disks
- **block devices** - per disk: I/O, operations, backlog, utilization, space, etc.  
- **BCACHE** - detailed performance of SSD caching devices.
- **DiskSpace** - monitors disk space usage.
- **mdstat** - software RAID.
- **hddtemp** - disk temperatures.
- **smartd** - disk S.M.A.R.T. values.
- **device mapper** - naming disks.
- **Veritas Volume Manager** - naming disks.
- **megacli** - adapter, physical drives and battery stats.
- **adaptec_raid** - 

#### Filesystems
- **BTRFS** - detailed disk space allocation and usage.
- **Ceph** - OSD usage, Pool usage, number of objects, etc.
- **NFS file servers and clients** - NFS v2, v3, v4: I/O, cache, read ahead, RPC calls  
- **Samba** - performance metrics of Samba SMB2 file sharing.
- **ZFS** - detailed performance and resource usage.

#### Networking
- **Network Stack** - everything about the networking stack (both IPv4 and IPv6 for all protocols: TCP, UDP, SCTP, UDPLite, ICMP, Multicast, Broadcast, etc), and all network interfaces (per interface: bandwidth, packets, errors, drops).
- **Netfilter** - everything about the netfilter connection tracker.
- **SynProxy** - collects performance data about the linux SYNPROXY (DDoS).
- **NFacct** - collects accounting data from iptables.
- **Network QoS** - the only tool that visualizes network `tc` classes in real-time  
- **FPing** - to measure latency and packet loss between any number of hosts.
- **OpenVPN** - status per tunnel.
- **ISC dhcpd** - pools utilization, leases, etc.
- **AP** - collects Linux access point performance data (`hostapd`).
- **SNMP** - SNMP devices can be monitored too (although you will need to configure these).
- **port_check** - checks TCP ports for availability and response time.
- **IPVS** - collects metrics from the Linux IPVS load balancer.

#### Processes
- **System Processes** - running, blocked, forks, active.
- **Applications** - by grouping the process tree and reporting CPU, memory, disk reads, disk writes, swap, threads, pipes, sockets - per process group.  
- **systemd** - monitors systemd services using CGROUPS.

#### Users
- **Users and User Groups resource usage** - by summarizing the process tree per user and group, reporting: CPU, memory, disk reads, disk writes, swap, threads, pipes, sockets
- **logind** - collects sessions, users and seats connected.

#### Containers and VMs
- **Containers** - all kinds of containers, using CGROUPS (systemd-nspawn, lxc, lxd, docker, kubernetes, etc).
- **libvirt VMs** - all kinds of VMs, using CGROUPS.

#### Web Servers
- **Apache and lighttpd** - `mod-status` (v2.2, v2.4) and cache log statistics, for multiple servers.
- **IPFS** - bandwidth, peers.
- **LiteSpeed** - reads the litespeed rtreport files to collect metrics.
- **Nginx** - `stub-status`, for multiple servers.
- **Nginx+** - connects to multiple nginx_plus servers (local or remote) to collect real-time performance metrics.
- **PHP-FPM** - multiple instances, each reporting connections, requests, performance, etc.
- **Tomcat** - accesses, threads, free memory, volume, etc.
- **web server `access.log` files** - extracting in real-time, web server and proxy performance metrics and applying several health checks, etc.
- **http_check** - checks one or more web servers for HTTP status code and returned content.

#### Proxies, Balancers, Accelerators
- **HAproxy** - bandwidth, sessions, backends, etc.
- **Squid** - multiple servers, each showing: clients bandwidth and requests, servers bandwidth and requests.
- **Traefik** - connects to multiple traefik instances (local or remote) to collect API metrics (response status code, response time, average response time and server uptime).
- **Varnish** - threads, sessions, hits, objects, backends, etc.

#### Database Servers
- **CouchDB** - reads/writes, request methods, status codes, tasks, replication, per-db, etc.
- **MemCached** - multiple servers, each showing: bandwidth, connections, items, etc.
- **MongoDB** - operations, clients, transactions, cursors, connections, asserts, locks, etc.
- **MySQL and mariadb** - multiple servers, each showing: bandwidth, queries/s, handlers, locks, issues, tmp operations, connections, binlog metrics, threads, innodb metrics, and more.
- **PostgreSQL** - multiple servers, each showing: per database statistics (connections, tuples read - written - returned, transactions, locks), backend processes, indexes, tables, write ahead, background writer and more.  
- **Redis** - multiple servers, each showing: operations, hit rate, memory, keys, clients, slaves.  
- **RethinkDB** - connects to multiple rethinkdb servers (local or remote) to collect real-time metrics.

#### Message Brokers
- **beanstalkd** - global and per tube monitoring.
- **rabbitmq** - performance and health metrics.

#### Search and Indexing
- **elasticsearch** - search and index performance, latency, timings, cluster statistics, threads statistics, etc.

#### DNS Servers
- **bind_rndc** - parses `named.stats` dump file to collect real-time performance metrics. All versions of bind after 9.6 are supported.
- **dnsdist** - performance and health metrics.
- **ISC Bind (named)** - multiple servers, each showing: clients, requests, queries, updates, failures and several per view metrics. All versions of bind after 9.9.10 are supported.
- **NSD** - queries, zones, protocols, query types, transfers, etc.
- **PowerDNS** - queries, answers, cache, latency, etc.
- **unbound** - performance and resource usage metrics.
- **dns_query_time** - DNS query time statistics.

#### Time Servers
- **chrony** - uses the `chronyc` command to collect chrony statistics (Frequency, Last offset, RMS offset, Residual freq, Root delay, Root dispersion, Skew, System time).  
- **ntpd** - connects to multiple ntpd servers (local or remote) to provide statistics of system variables and optional also peer variables.

#### Mail Servers
- **Dovecot** - POP3/IMAP servers.
- **Exim** - message queue (emails queued).
- **Postfix** - message queue (entries, size).

#### Hardware Sensors
- **IPMI** - enterprise hardware sensors and events.
- **lm-sensors** - temperature, voltage, fans, power, humidity, etc.
- **RPi** - Raspberry Pi temperature sensors.
- **w1sensor** - collects data from connected 1-Wire sensors.

#### UPSes
- **apcupsd** - load, charge, battery voltage, temperature, utility metrics, output metrics
- **NUT** - load, charge, battery voltage, temperature, utility metrics, output metrics

#### Social Sharing Servers
- **RetroShare** - connects to multiple retroshare servers (local or remote) to collect real-time performance metrics.

#### Security
- **Fail2Ban** - monitors the fail2ban log file to check all bans for all active jails.

#### Authentication, Authorization, Accounting (AAA, RADIUS, LDAP) Servers
- **FreeRadius** - uses the `radclient` command to provide freeradius statistics (authentication, accounting, proxy-authentication, proxy-accounting).

#### Telephony Servers
- **opensips** - connects to an opensips server (localhost only) to collect real-time performance metrics.

#### Provisioning Systems
- **Puppet** - connects to multiple Puppet Server and Puppet DB instances (local or remote) to collect real-time status metrics.

#### Household Appliances
- **SMA webbox** - connects to multiple remote SMA webboxes to collect real-time performance metrics of the photovoltaic (solar) power generation.
- **Fronius** - connects to multiple remote Fronius Symo servers to collect real-time performance metrics of the photovoltaic (solar) power generation.
- **StiebelEltron** - collects the temperatures and other metrics from your Stiebel Eltron heating system using their Internet Service Gateway (ISG web).

#### Game Servers
- **SpigotMC** - monitors Spigot Minecraft server ticks per second and number of online players using the Minecraft remote console.

#### Distributed Computing
- **BOINC** - monitors task states for local and remote BOINC client software using the remote GUI RPC interface. Also provides alarms for a handful of error conditions.

And you can extend it, by writing plugins that collect data from any source, using any computer language.  
  
---

## Installation  
  
Use our **[automatic installer](https://github.com/netdata/netdata/wiki/Installation)** to build and install it on your system.  
  
It should run on **any Linux** system (including IoT). It has been tested on:  
  
- Alpine  
- Arch Linux  
- CentOS  
- Debian  
- Fedora  
- Gentoo  
- openSUSE  
- PLD Linux  
- RedHat Enterprise Linux  
- SUSE  
- Ubuntu  
  
---  
  
## Documentation  
  
Check the **[netdata wiki](https://github.com/netdata/netdata/wiki)**.  


## Community

1. To report bugs, or get help, use [GitHub Issues](https://github.com/netdata/netdata/issues).
2. Netdata has a [Facebook page](https://www.facebook.com/linuxnetdata/).
3. Netdata has a [Twitter account](https://twitter.com/linuxnetdata).
4. Netdata on [OpenHub](https://www.openhub.net/p/netdata).
5. Netdata on [Repology](https://repology.org/metapackage/netdata/versions).
6. Netdata on [StackShare](https://stackshare.io/netdata).

## License  
  
netdata is [GPLv3+](LICENSE).  
  
Netdata re-distributes other open-source tools and libraries. Please check the [third party licenses](https://github.com/netdata/netdata/blob/master/REDISTRIBUTED.md).


![netdata](https://cloud.githubusercontent.com/assets/2662304/14092712/93b039ea-f551-11e5-822c-beadbf2b2a2e.gif)  

   ![sda](https://cloud.githubusercontent.com/assets/2662304/14093195/c882bbf4-f554-11e5-8863-1788d643d2c0.gif)  
  
   ![dsl0](https://cloud.githubusercontent.com/assets/2662304/14093128/4d566494-f554-11e5-8ee4-5392e0ac51f0.gif)  
  
![image](https://cloud.githubusercontent.com/assets/2662304/20464811/9517d2b4-af57-11e6-8361-f6cc57541cd7.png)  

  
  
   ![qos-tc-classes](https://cloud.githubusercontent.com/assets/2662304/14093004/68966020-f553-11e5-98fe-ffee2086fafd.gif)  
  
   ![apps](https://cloud.githubusercontent.com/assets/2662304/14093565/67c4002c-f557-11e5-86bd-0154f5135def.gif)  
