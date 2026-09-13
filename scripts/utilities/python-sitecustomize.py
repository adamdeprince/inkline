"""Use the reMarkable firmware's trust store, including inside virtualenvs."""
import os

_inkline_certificates = "/etc/ssl/certs/ca-certificates.crt"
if not (os.environ.get("SSL_CERT_FILE") or os.environ.get("SSL_CERT_DIR")):
    if os.path.isfile(_inkline_certificates):
        os.environ["SSL_CERT_FILE"] = _inkline_certificates
