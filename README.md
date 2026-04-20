# scout

DHCP client, DNS resolver configuration daemon, and basic network tools for BlueyOS.

`scout` follows the existing BlueyOS packaging pattern:

- Autotools-managed configuration and feature detection
- foreground service supervised by `claw`
- dimsim `.dpk` packaging
- BlueyOS-focused musl build flow by default, with configurable libc selection

## What it ships

| Path | Purpose |
|---|---|
| `/sbin/scoutd` | DHCP/DNS client daemon |
| `/usr/bin/nslookup` | resolver lookup tool |
| `/usr/bin/ping` | ICMP echo tool when the target supports raw sockets |
| `/usr/bin/tracert` | traceroute-style diagnostic when the target supports raw sockets |
| `/etc/scout/scout.conf` | daemon configuration |
| `/etc/claw/services.d/scout.service.yml` | claw service definition |

## Important BlueyOS caveats

BlueyOS already exposes enough IPv4 UDP support for DHCP and DNS transactions, and both musl-blueyos and glibc-blueyos ship resolver headers that use `/etc/resolv.conf`.

The current BlueyOS kernel still has two gaps that limit what `scout` can do live on-target:

1. `NETCTL_MSG_ADDR_*` and `NETCTL_MSG_ROUTE_*` are stubbed in the kernel control plane, so `scoutd` cannot yet apply IPv4 addresses and routes live through netctl.
2. Current userland socket support does not expose the raw ICMP path required for fully native `ping` and `tracert`.

`scoutd` therefore does the useful pieces immediately:

- acquires and renews DHCP leases
- writes `/etc/resolv.conf`
- persists the lease under `/var/lib/scout/lease`
- writes a static `/etc/interfaces` snapshot so the lease information is preserved for future integration
- brings the link up when the platform supports that operation

On Linux hosts used for development, `ping` and `tracert` use standard raw-socket implementations. On current BlueyOS builds they exit with a clear unsupported message instead of silently faking success.

## Building

### Quick start: BlueyOS musl

```bash
./autogen.sh
mkdir -p build/musl
cd build/musl
../../tools/configure-blueyos.sh --libc=musl
make
make check
make package
```

If the musl sysroot is missing, build one locally first:

```bash
./tools/build-musl.sh --prefix="$PWD/build/musl-sysroot"
./autogen.sh
mkdir -p build/musl
cd build/musl
../../tools/configure-blueyos.sh --libc=musl --sysroot="$PWD/../musl-sysroot"
make
```

### BlueyOS glibc

```bash
./autogen.sh
mkdir -p build/glibc
cd build/glibc
CC=i686-pc-blueyos-gcc ../../tools/configure-blueyos.sh --libc=glibc --sysroot=/opt/blueyos-sysroot
make
```

For glibc builds, provide a BlueyOS-aware compiler via `CC=` if there is not already one on your `PATH`.

### Native Linux developer build

```bash
./autogen.sh
mkdir -p build/native
cd build/native
../../configure --prefix=/usr --sysconfdir=/etc --localstatedir=/var
make
make check
```

## Packaging

`make package` stages the install tree under `package-stage/`, copies the dimsim lifecycle scripts and manifest, and runs `dpkbuild build package-stage`.

The package version follows the BlueyOS convention:

- base release from `configure.ac`: `0.1.0`
- package build number from `PACKAGE_BUILD_NUMBER` (default `1`)
- final dimsim version: `0.1.0-1`

Override the build number at configure or make time:

```bash
PACKAGE_BUILD_NUMBER=3 make package
```

## Installing into a sysroot

```bash
make install-sysroot SYSROOT=/mnt/blueyos
```

This copies:

- `/sbin/scoutd`
- `/usr/bin/nslookup`
- `/usr/bin/ping`
- `/usr/bin/tracert`
- `/etc/scout/scout.conf`
- `/etc/claw/services.d/scout.service.yml`

## Runtime configuration

Default `/etc/scout/scout.conf`:

```ini
interface=eth0
hostname=
client_id=
lease_file=/var/lib/scout/lease
resolv_conf=/etc/resolv.conf
interfaces_file=/etc/interfaces
request_timeout=5
retry_interval=10
renew_margin=60
max_retries=0
persist_interfaces=yes
```

## Service behavior

`scoutd` is intended to run in the foreground under claw:

```bash
/sbin/scoutd -n -c /etc/scout/scout.conf
```

It logs to stderr so claw can capture and supervise it directly.
