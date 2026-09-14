"""Keep automatic bytecode writes off and use the firmware's trust store."""
import sys
import os

sys.dont_write_bytecode = True
os.environ["PYTHONDONTWRITEBYTECODE"] = "1"
os.environ.setdefault("PIP_COMPILE", "0")

_inkline_certificates = "/etc/ssl/certs/ca-certificates.crt"
if not (os.environ.get("SSL_CERT_FILE") or os.environ.get("SSL_CERT_DIR")):
    if os.path.isfile(_inkline_certificates):
        os.environ["SSL_CERT_FILE"] = _inkline_certificates
