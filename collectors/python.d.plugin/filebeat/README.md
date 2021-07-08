<!--
title: "Filebeat monitoring with Netdata"
custom_edit_url: https://github.com/netdata/netdata/edit/master/collectors/python.d.plugin/filebeat/README.md
sidebar_label: "Filebeat"
-->

# Filebeat monitoring with Netdata

Monitors [Filebeat](https://www.elastic.co/beats/filebeat) performance and health metrics.

It produces:

1.  **memory** charts:

    -   Memory used currently by filebeat
    -   Memory allocated in total by filebeat
    -   RSS memory used by filebeat

2. **cpu** chart:

    -   CPU used by filebeat, system time (ms)
    -   CPU used by filebeat, user time (ms)
    -   CPU used by filebeat, total (ms)

3. **uptime** chart:

    -   Filebeat uptime

4. **events** charts:

    -   The global number of events (Active, Added and Done)
    -   Their respective rates, per second

5. **harvester** charts:

    -   Stats on harvested files (Closed, Open, Running, Started, Skipped)
    -   Their respective rates, per second
    -   The numer of errors or truncated files

6. **output** charts:

    -   Output event number (Acked, Active, Batches, Duplicates, Failed)
    -   Their respective rates, per second
    -   Output read in KiB
    -   Output read errors number
    -   Output write in KiB
    -   Output write errors number

7. **pipeline** charts:

    -   Number of pipeline clients
    -   Number of pipeline events
    -   Their respective rates, per second
    -   The number of queue ACK per second

## Configuration

Your filebeat instance must be configured for metricbeat monitoring, meaning that
it must have its http server configured and ready for requests.

You can add the following in your filebeat.yml config file:

```yaml
http.enabled: true
http.host: 127.0.0.1
```

Edit the `python.d/file.conf` configuration file using `edit-config` from the Netdata [config
directory](/docs/configure/nodes.md), which is typically at `/etc/netdata`.

```bash
cd /etc/netdata   # Replace this path with your Netdata config directory, if different
sudo ./edit-config python.d/filebeat.conf
```

Sample:

```yaml
local:
  host               : 'ipaddress'    # Elasticsearch server ip address or hostname.
  port               : 'port'         # Port on which filebeat listens.
  scheme             : 'http'         # URL scheme. Use 'https' if your filebeat uses TLS.
```

If no configuration is given, module will try to connect to `http://127.0.0.1:5066`.

---

[![analytics](https://www.google-analytics.com/collect?v=1&aip=1&t=pageview&_s=1&ds=github&dr=https%3A%2F%2Fgithub.com%2Fnetdata%2Fnetdata&dl=https%3A%2F%2Fmy-netdata.io%2Fgithub%2Fcollectors%2Fpython.d.plugin%2Ffilebeat%2FREADME&_u=MAC~&cid=5792dfd7-8dc4-476b-af31-da2fdb9f93d2&tid=UA-64295674-3)](<>)
