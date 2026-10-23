# khdays-server

The meeting point of the native port's Mission Mode multiplayer over the internet. Players on one PC or one local network need no server: the
game finds their rooms by itself. Players in different places need one machine everyone can reach
running this program:

- a host's game registers its room here, a guest's game asks for the rooms and for one of them;
- the server tells both sides the other's address as the internet sees it, so that their games
  can talk to each other directly;
- when their networks do not let them (some routers, some providers), the server passes their
  datagrams between them (`--relay off` turns that off).

Nobody opens a port on their router: the players' games only connect outward. Only the machine
the server runs on must be reachable on its UDP port -- and on a home PC the server asks the
router to open it by itself (NAT-PMP, else UPnP; most home routers have one of them on), and its
window says the address the players write. When the router will not, or the provider shares one
internet address among many customers (carrier-grade NAT, as some fibre and mobile connections
do) and nothing can be opened, the window says so: then run the server on a machine with an
internet address of its own.

The server keeps everything in memory and writes no address anywhere -- its window and its log
say how many rooms and relayed sessions there are, never who.

It is open to anyone who can reach it, so it bounds what anyone can make it do:

- **Memory.** Every room and relayed session it holds counts against a budget -- by default an
  eighth of the machine's memory, `--memory MB` to set it. A new one past the budget pushes out
  the one heard from least recently: real rooms register every 2 seconds and real sessions send
  dozens of datagrams a second, so whatever a flood made goes first.
- **Time.** Rooms and sessions are found by hash, and a list is answered from the rooms of that
  game and build only: each datagram costs the same however much is held.
- **Relayed traffic.** Up to 392 datagrams a second each way per session -- four times the
  busiest second measured (98, at 150 ms and 10% lost) -- the rest dropped and counted.
- **No amplification.** No answer is longer than the datagram that asked; a guest already
  introduced to a room is not introduced again, and a room's host is told of new guests no faster
  than every 250 ms.

Every minute, when something changed, it says what it holds against its budget and what it did
since the last time: rooms opened, datagrams relayed, entries evicted, relayed datagrams dropped
(`--status S` for another interval).

## On Windows

`khdays-server.exe` sits next to `khdays-native.exe`. Open it (a double click is enough); its
window says the port it listens on (UDP 35821), this machine's addresses, and what the router
answered (`--router off`: it is not asked). Leave it open while
people play. The players write this machine's public address -- the one the internet sees -- as
the server in the game's settings.

```
khdays-server [--port N] [--relay on|off] [--router on|off] [--memory MB] [--status S] [--log FILE]
```

Its first line says its version -- the commit it was built from -- and the next one its memory
budget.

## On a Linux server (Ubuntu)

Build it from the port's sources (`port/server/` -- `server.c`, `server_core.c`,
`server_core.h`, `portmap.c`, `portmap.h`, `version.h` -- and `port/host/net/net.h`,
`port/host/net/net_posix.c`, keeping that layout). Taken with `git archive`, `version.h` holds the
commit they come from, and the server says it:

```bash
git archive --format=tar.gz -o khdays-server-src.tar.gz HEAD port/server port/host/net/net.h port/host/net/net_posix.c
```

On the server:

```bash
sudo apt install -y build-essential
mkdir -p khdays-server-src && tar -xzf khdays-server-src.tar.gz -C khdays-server-src && cd khdays-server-src
cc -O2 -std=gnu11 -Wall -Wno-unknown-pragmas -o khdays-server port/server/server.c port/server/server_core.c port/server/portmap.c port/host/net/net_posix.c
```

(`-Wno-unknown-pragmas`: the sources carry MSVC's `#pragma warning` lines for its analysis, which
other compilers ignore.)

Run it as a service that starts with the machine and restarts if it stops, as a user of its own:

```bash
sudo install -m 755 khdays-server /usr/local/bin/khdays-server
sudo tee /etc/systemd/system/khdays-server.service > /dev/null <<'EOF'
[Unit]
Description=khdays-server (Kingdom Hearts 358/2 Days native port multiplayer)
After=network-online.target
Wants=network-online.target

[Service]
ExecStart=/usr/local/bin/khdays-server
Restart=always
DynamicUser=yes

[Install]
WantedBy=multi-user.target
EOF
sudo systemctl daemon-reload
sudo systemctl enable --now khdays-server
```

Let UDP 35821 through the machine's firewall (`sudo ufw allow 35821/udp` if ufw is on) and
through the provider's, if it has one in its control panel. A rented server has an internet
address of its own: the server sees it and asks no router. What the server says:
`journalctl -u khdays-server -f`.

To update it: build the new sources as above, then

```bash
sudo install -m 755 khdays-server /usr/local/bin/khdays-server
sudo systemctl restart khdays-server
journalctl -u khdays-server -n 5
```

and the first line says the new version. A restart forgets what the server held: the rooms
register again within 2 seconds, but a session relayed through it at that moment hears nothing
for 5 seconds and ends (its players see the connection error) -- restart it when its last status line
says "0 sessions through the server".
