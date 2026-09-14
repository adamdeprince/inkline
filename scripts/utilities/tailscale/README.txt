Tailscale 1.102.4 for reMarkable 2 / firmware 3.27

Installable package; no compiler or additional package manager is needed on
the tablet. Verify the outer archive's SHA-256, extract it in /tmp, and run:

  ./install-device.sh --check
  ./install-device.sh

The preflight starts a separate userspace daemon in RAM without signing in.
Installation enables inkline-tailscale.service at boot. It does not change
Inkline sessions, the stock SSH server, system routes, or DNS settings.
Commands are installed in /home/root/.local/bin, already on Inkline's PATH.

SIGN IN, THEN CONNECT

  tailscale up --accept-dns=false --accept-routes=false --hostname=remarkable2

Open the printed HTTPS sign-in link in a browser and approve the tablet in
your own Tailscale account. Complete device approval too if your tailnet
requires it. Authentication is never part of a downloadable package.

  tailscale status
  tailscale-ssh name@tailnet-host
  GIT_SSH_COMMAND=tailscale-ssh git clone name@tailnet-host:path/to/repo.git

tailscale-ssh invokes the tablet's stock SSH client with a ProxyCommand using
"tailscale nc HOST PORT". Host-key verification, SSH keys, and server accounts
work as usual. The wrapper supplies the actual host and port because Dropbear
does not expand OpenSSH's %h/%p placeholders. This wrapper does not enable the
Tailscale SSH server feature.
MagicDNS names and tailnet IP addresses can be used through this TCP dialer.
For an existing SSH command, the equivalent option is:

  ssh -o 'ProxyCommand=/home/root/.local/bin/tailscale nc tailnet-host 22' name@tailnet-host

The firmware ships Dropbear, whose default private key is ~/.ssh/id_dropbear.
It cannot read an OpenSSH-format key directly. To reuse an existing unencrypted
Ed25519 key, convert a copy on the tablet (keep the original and never overwrite
an existing id_dropbear):

  test ! -e ~/.ssh/id_dropbear && test ! -L ~/.ssh/id_dropbear && (
    umask 077
    /usr/sbin/dropbearmulti dropbearconvert openssh dropbear ~/.ssh/id_ed25519 ~/.ssh/id_dropbear
  )

The public key identity stays the same; only its private-file format changes.
Never publish private keys. If you need a new key instead, use the stock
"/usr/sbin/dropbearmulti dropbearkey" and register only its public key with
the destination service. Tailscale enrollment does not configure SSH accounts.

USERSPACE NETWORKING

No kernel TUN device is needed. The local SOCKS5 and HTTP proxies share
127.0.0.1:1055. Applications must explicitly use a proxy or the TCP dialer;
ordinary system traffic does not automatically use Tailscale. This SSH/Git
recipe carries TCP, not Mosh's UDP data channel. Goblin Mosh is unchanged.
An HTTPS example, if curl is installed, is:

  curl --proxy socks5h://127.0.0.1:1055 https://tailnet-host/

The root-only daemon socket is /run/inkline-tailscale/tailscaled.sock.
Taildrop is disabled. Routine daemon logs are discarded, log uploads are
disabled, and temporary/log buffers live in RAM. Essential identity and
preferences persist under:

  /home/root/.local/share/inkline-utilities/tailscale/state

This is not a zero-write service: sign-in, preference changes, and key renewal
can write state. Routine network traffic is not spooled to flash. The service
uses CPU, RAM, and battery while enabled, including when Inkline is closed.
Disabling log uploads reduces the diagnostics available to Tailscale support.

MANAGE OR REMOVE

  systemctl status inkline-tailscale.service
  systemctl stop inkline-tailscale.service
  systemctl start inkline-tailscale.service
  systemctl disable --now inkline-tailscale.service
  systemctl enable --now inkline-tailscale.service

"tailscale down" disconnects the tailnet but leaves the service running.
"tailscale logout" signs this tablet out. Upgrades preserve sign-in state.
Install a new Inkline utility release to update the binaries; do not use an
upstream installer to overwrite this managed package. Firmware upgrades may
remove the systemd link; rerunning current/install-device.sh restores it
after its firmware compatibility check passes.

Remove the service, binaries, and local sign-in state with:

  /home/root/.local/share/inkline-utilities/tailscale/current/uninstall-device.sh

Remove the old device entry from the Tailscale admin console if desired.
Documents and other utilities are preserved.

Installation guide: https://inkline.goblinreactor.com/install.html#tailscale
Downloads and storage: https://inkline.goblinreactor.com/utilities.html#tailscale
Sources and licenses: https://inkline.goblinreactor.com/source.html
