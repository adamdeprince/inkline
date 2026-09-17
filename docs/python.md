# Python 3.15 on reMarkable 2

The Python utility uses unmodified upstream CPython **3.15.0rc2** from the
pinned September 1, 2026 ARM build. It is a release candidate. The previous
Inkline startup, `sitecustomize`, and ensurepip patches have been removed.
The terminal and Python wrappers do not set a Python or pip cache policy.

## Choose your cache preferences

On the **tablet**, add these lines to `/home/root/.bashrc`:

```sh
export PYTHONDONTWRITEBYTECODE=1
export PIP_NO_CACHE_DIR=1
```

Apply them to the current Bash shell with `source ~/.bashrc`. Inkline 0.3.5
starts interactive `/bin/bash`, which reads that file for each new terminal.
After updating Inkline, quit and reopen it to use the new shell launcher.
Existing shells and Python processes keep their existing environments.
These are personal settings; the installer does not add or remove them. It only
manages its clearly marked, `TERM_PROGRAM=inkline` battery-prompt block in `.bashrc`.

- [`PYTHONDONTWRITEBYTECODE=1`](https://docs.python.org/3.15/using/cmdline.html#envvar-PYTHONDONTWRITEBYTECODE)
  stops automatic `.pyc`/`__pycache__` writes when Python imports modules.
- [`PIP_NO_CACHE_DIR=1`](https://pip.pypa.io/en/stable/topics/caching/#disabling-caching)
  disables pip's HTTP and wheel cache. Repeated installs may download or build
  the same package again.
- Pip's installation-time bytecode compilation is separate. Use
  [`--no-compile`](https://pip.pypa.io/en/stable/cli/pip_install/#cmdoption-no-compile):

```sh
python3 -m pip install --no-compile PACKAGE
```

These settings do not remove existing caches or stop programs saving their
own files. Python's `-E` and `-I` modes ignore Python environment variables;
explicit compilation tools can still write bytecode. Stock `python3 -m venv`
uses ensurepip in isolated mode, so its pip bootstrap can create caches.

## Virtual environments without the ensurepip bootstrap

The wrappers select the tablet's certificate bundle. An activated virtual
environment calls its interpreter directly, so also add this to `.bashrc`
if you have not configured a different certificate store:

```sh
export SSL_CERT_FILE=/etc/ssl/certs/ca-certificates.crt
```

With the two cache settings above active, use the packaged pip to install into
a new environment without bootstrapping another pip there:

```sh
python3 -m venv --without-pip ~/venvs/project
python3 -m pip --python ~/venvs/project/bin/python install --no-compile PACKAGE
source ~/venvs/project/bin/activate
```

The environment and installed packages are persistent files. Temporary build
files use `/tmp`; application-specific caches need their own configuration.

## Verify the shell settings

```sh
printf '%s\n' "$SHELL" "$PYTHONDONTWRITEBYTECODE" "$PIP_NO_CACHE_DIR"
python3 -c 'import sys; print(sys.dont_write_bytecode)'
```

In a newly opened Inkline terminal, these print `/bin/bash`, `1`, `1`, and
`True`. Unset the two variables in `.bashrc` to restore the stock cache defaults.
