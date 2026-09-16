Tailscale 1.102.4 for reMarkable 2 / firmware 3.27
Guide updated September 14, 2026

This walkthrough connects Inkline to another computer over Tailscale, then
uses the tablet's ordinary SSH client for a shell or Git repository. The
remote computer still needs an SSH server and an account you can log into.
Signing in to Tailscale and authorizing an SSH key are separate steps.

1. INSTALL ON BOTH DEVICES

On your remote computer, install Tailscale and sign in:
  https://tailscale.com/download
Enable its ordinary SSH server (Remote Login on macOS, sshd on Linux), and
note your remote account name. Run "whoami" there if you are unsure.

On the tablet, install the Tailscale utility using:
  https://inkline.goblinreactor.com/install.html?utility=tailscale#utilities
Choose Git there too if you want to clone repositories. No compiler or extra
tablet package manager is needed. The installer checks compatibility before
changing the installation. For a manually downloaded bundle, verify its
SHA-256, extract it in /tmp, and run these from its tailscale/ directory:

  ./install-device.sh --check
  ./install-device.sh

Installation enables inkline-tailscale.service at boot. The preflight uses a
separate daemon in RAM without signing in. Commands are installed under
/home/root/.local/bin, which is already on Inkline's PATH.

2. SIGN IN ON THE TABLET

Connect the tablet to Wi-Fi, open Inkline with Ctrl+Opt+Alt+T, and run:

  tailscale up --accept-dns=false --accept-routes=false --hostname=remarkable2

The command prints "To authenticate, visit:" followed by an HTTPS URL.
Leave it running. Open that entire URL in a browser on your phone or computer;
the tablet does not need a browser. Sign in to the same Tailscale account as
the remote computer and approve the tablet. If your tailnet requires device
approval, its administrator must also approve it in the admin console:
  https://login.tailscale.com/admin/machines

Use the link printed by YOUR tablet. Sign-in links are specific to a device
and expire; there is no shared enrollment link in this guide or package.
Once approved, the command returns to the shell. Then run:

  tailscale status
  tailscale ip -4

Find the remote computer in the status output. In the examples below,
"workstation" means its actual tailnet name; its 100.x.y.z Tailscale address
also works. "alice" means your SSH account ON THAT COMPUTER, which may differ
from your Tailscale account. Substitute both before running the examples.

  tailscale ping --c 3 workstation

A pong confirms tailnet connectivity. A relay/DERP response is also valid.
This check does not prove that the remote SSH server is listening or that
your tailnet's access policy permits its TCP port.

3. PREPARE A DROPBEAR SSH KEY

The tablet's stock SSH client is Dropbear. Its default private key is
~/.ssh/id_dropbear; an OpenSSH-format private key cannot be used directly.
If you already have a working id_dropbear, keep it and skip to step 4.
Otherwise, choose ONE of the following options inside Inkline.

Option A: reuse your existing, unencrypted ~/.ssh/id_ed25519 key.
This writes a converted copy, keeps the original, and preserves the same
public identity. A public key already authorized on a server or GitHub
does not need to be registered again. The converter cannot read encrypted
OpenSSH private keys; use option B if you do not have a convertible key.

  (
    set -eu
    umask 077
    mkdir -p ~/.ssh
    chmod 700 ~/.ssh
    if test -e ~/.ssh/id_dropbear || test -L ~/.ssh/id_dropbear; then
      printf '%s\n' 'Existing id_dropbear preserved.'
    else
      /usr/sbin/dropbearmulti dropbearconvert openssh dropbear ~/.ssh/id_ed25519 ~/.ssh/id_dropbear
      chmod 600 ~/.ssh/id_dropbear
    fi
  )

Option B: create a new native Ed25519 key on the tablet.
This also preserves any existing id_dropbear. Register the new public key
with each destination you want to access, as described in step 4.

  (
    set -eu
    umask 077
    mkdir -p ~/.ssh
    chmod 700 ~/.ssh
    if test -e ~/.ssh/id_dropbear || test -L ~/.ssh/id_dropbear; then
      printf '%s\n' 'Existing id_dropbear preserved.'
    else
      /usr/sbin/dropbearmulti dropbearkey -t ed25519 -f ~/.ssh/id_dropbear
      chmod 600 ~/.ssh/id_dropbear
    fi
  )

4. AUTHORIZE THE PUBLIC KEY

In Inkline, display only the public key line:

  /usr/sbin/dropbearmulti dropbearkey -y -f ~/.ssh/id_dropbear | awk '$1 ~ /^(ssh-|ecdsa-)/ { print }'

Copy that whole line, beginning with ssh-ed25519 for the keys above. It is
safe to give this PUBLIC line to the destination. Keep id_dropbear itself
on the tablet: it is the private key, not the file to upload. You can also
run the public-key command in the tablet's USB SSH session from your computer,
then use that computer's terminal to copy the public line into its clipboard.

For your own Linux or macOS computer, open a terminal ON THAT COMPUTER as
the account you will SSH into, and run:

  umask 077
  mkdir -p ~/.ssh
  chmod 700 ~/.ssh
  printf '\n' >> ~/.ssh/authorized_keys
  cat >> ~/.ssh/authorized_keys

Paste the public key as one line, press Enter, then Ctrl+D to finish cat.
This appends to authorized_keys, preserving its existing entries. Then run:

  chmod 600 ~/.ssh/authorized_keys

For GitHub, open https://github.com/settings/ssh/new in a browser while signed
in. Give the key a recognizable title, choose "Authentication Key", paste the
same public line, and save it. GitHub's instructions are here:
  https://docs.github.com/en/authentication/connecting-to-github-with-ssh/adding-a-new-ssh-key-to-your-github-account

5. CONNECT OVER SSH

Back inside Inkline (replace alice and workstation):

  tailscale-ssh alice@workstation

On the first connection, compare the displayed host-key fingerprint with the
remote computer's SSH host key before accepting it. Type "exit" to return
to the tablet. For a server listening on another port, put -p before the host:

  tailscale-ssh -p 2222 alice@workstation

tailscale-ssh uses the tablet's stock SSH client and "tailscale nc HOST PORT".
It handles Dropbear's proxy syntax and accepts tailnet names and IP addresses.
It does not enable the separate Tailscale SSH server feature. With the stock
SSH command directly, the equivalent is:

  ssh -o 'ProxyCommand=/home/root/.local/bin/tailscale nc workstation 22' alice@workstation

Supply the ACTUAL host and port in ProxyCommand. Dropbear does not expand
OpenSSH's %h and %p placeholders. Ordinary "ssh alice@workstation" does not
automatically use this userspace Tailscale connection.

6. USE GIT

To clone an existing repository on your tailnet computer, replace the account,
host, and repository path below. "git/notes.git" is relative to that remote
account's home directory. Git must be installed on both devices.

  mkdir -p ~/dev
  GIT_SSH_COMMAND=tailscale-ssh git clone alice@workstation:git/notes.git ~/dev/notes
  git -C ~/dev/notes config core.sshCommand tailscale-ssh
  git -C ~/dev/notes pull

GIT_SSH_COMMAND applies only to that clone. The repository setting saves the
helper for future pulls, fetches, and pushes. To use it in an existing
repository whose SSH remote is on your tailnet, run the same
"git -C PATH config core.sshCommand tailscale-ssh" with that repository's path.

GitHub's public SSH endpoint is reached directly over the tablet's normal
internet connection. After authorizing the key on GitHub, substitute OWNER
and REPO in this command; use a COLON after github.com, not a slash:

  mkdir -p ~/dev
  git clone git@github.com:OWNER/REPO.git ~/dev/REPO

Do not set core.sshCommand to tailscale-ssh for this GitHub example. The key
must belong to a GitHub account with access to the repository. Check GitHub's
published host-key fingerprints when accepting its host key:
  https://docs.github.com/en/authentication/keeping-your-account-and-data-secure/githubs-ssh-key-fingerprints

IF SOMETHING DOES NOT WORK

* "tailscale: not found": open Inkline after installing the utility. From a
  different shell, run /home/root/.local/bin/tailscale explicitly, or add
  /home/root/.local/bin to that shell's PATH.
* Cannot connect to tailscaled: run
    systemctl start inkline-tailscale.service
    systemctl status inkline-tailscale.service
* NeedsLogin, or a sign-in link expired: in Inkline on the tablet, run
    tailscale login --accept-dns=false --accept-routes=false --hostname=remarkable2
  Open the new link and complete step 2. Run this locally, since signing in
  again can interrupt a connection that already depends on Tailscale.
* Awaiting device approval: ask your tailnet administrator to approve the
  tablet in the admin console; repeating the login will not bypass approval.
* No pong: check Wi-Fi, that both devices are online in the same tailnet, and
  that the name/IP is correct. Use "tailscale ping", not ordinary "ping".
* Pong but SSH times out or is refused: check the remote SSH server, port,
  firewall, and tailnet access policy. A pong does not check SSH access.
* SSH authentication fails: check the remote account name and its authorized
  public key. On the tablet, check ~/.ssh/id_dropbear with the public-key
  command in step 4. "String too long" while reading a private key can mean
  an OpenSSH key was passed to Dropbear without conversion.
* A host-key warning: verify why the remote host's key changed before updating
  known_hosts. Do not disable host-key checking to make the warning disappear.
* A later git pull fails: check "git -C ~/dev/notes config core.sshCommand";
  a tailnet repository should report tailscale-ssh. Also check "git remote -v"
  from inside it to confirm that the host/path refer to your tailnet server.
* Git prints "Ignoring unknown configuration option SendEnv=GIT_PROTOCOL":
  this is a harmless Git/Dropbear compatibility message, not an auth failure.

USERSPACE NETWORKING AND STORAGE

No kernel TUN device is needed. The local SOCKS5 and HTTP proxies share
127.0.0.1:1055. Applications must explicitly use a proxy or the TCP dialer;
ordinary system traffic and DNS do not automatically use Tailscale. This
SSH/Git recipe carries TCP, not Mosh's UDP data channel. Goblin Mosh is
unchanged. An HTTPS example, if curl is installed, is:

  curl --proxy socks5h://127.0.0.1:1055 https://workstation/

The root-only daemon socket is /run/inkline-tailscale/tailscaled.sock.
Taildrop is disabled. Routine daemon logs are discarded, log uploads are
disabled, and temporary/log buffers live in RAM. Essential identity and
preferences persist under:

  /home/root/.local/share/inkline-utilities/tailscale/state

Sign-in, preference changes, and key renewal can write state. Routine network
traffic is not spooled to flash. The service uses CPU, RAM, and battery while
enabled, including when Inkline is closed. Disabling log uploads reduces the
diagnostics available to Tailscale support.

MANAGE OR REMOVE

Check, stop, or start the service:

  systemctl status inkline-tailscale.service
  systemctl stop inkline-tailscale.service
  systemctl start inkline-tailscale.service

Stop now and disable at boot, or start now and re-enable at boot:

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
Current text guide: https://inkline.goblinreactor.com/docs/tailscale.txt
Downloads and storage: https://inkline.goblinreactor.com/utilities.html#tailscale
Sources and licenses: https://inkline.goblinreactor.com/source.html
Tailscale CLI reference: https://tailscale.com/docs/reference/tailscale-cli
Userspace networking: https://tailscale.com/docs/concepts/userspace-networking
