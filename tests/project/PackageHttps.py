"""Qualify explicit HTTPS import with a disposable test certificate and loopback origin."""

import argparse
import hashlib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import ssl
import subprocess
import tempfile
import threading


def Main():
    Parser = argparse.ArgumentParser(description=__doc__)
    Parser.add_argument("--sdk", type=Path, required=True)
    Args = Parser.parse_args()
    Sdk = Args.sdk.resolve()
    Cli = Sdk / json.loads((Sdk / "SdkManifest.json").read_bytes())["Components"]["Cli"]
    with tempfile.TemporaryDirectory(prefix="lui-package-https-") as Directory:
        Root = Path(Directory)
        Package = Root / "Origin"
        subprocess.run([Cli, "package", "init", Package, "--name", "Hosted", "--version", "1.0.0"],
                       check=True, capture_output=True, timeout=10)
        File = Root / "origin.luipkg"
        subprocess.run([Cli, "package", "pack", Package, "--output", File],
                       check=True, capture_output=True, timeout=10)
        Data = File.read_bytes()
        Digest = hashlib.sha256(Data).hexdigest()
        Key, Certificate = Root / "key.pem", Root / "certificate.pem"
        subprocess.run(["openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes", "-days", "1",
                        "-subj", "/CN=LUI package fixture", "-addext", "subjectAltName=IP:127.0.0.1",
                        "-keyout", Key, "-out", Certificate], check=True, capture_output=True, timeout=10)
        Requests = []

        class Handler(BaseHTTPRequestHandler):
            def do_GET(Self):
                Requests.append(Self.path)
                if Self.path == "/redirect":
                    Self.send_response(302)
                    Self.send_header("Location", "/package")
                    Self.end_headers()
                    return
                Self.send_response(200)
                Self.send_header("Content-Length", str(len(Data)))
                Self.end_headers()
                Self.wfile.write(Data)

            def log_message(Self, *_):
                pass

        with ThreadingHTTPServer(("127.0.0.1", 0), Handler) as Server:
            Context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
            Context.load_cert_chain(Certificate, Key)
            Server.socket = Context.wrap_socket(Server.socket, server_side=True)
            Thread = threading.Thread(target=Server.serve_forever, daemon=True)
            Thread.start()
            try:
                Origin = f"https://127.0.0.1:{Server.server_port}"
                Catalog = Root / "Catalog"
                TestEnvironment = {**os.environ, "SSL_CERT_FILE": str(Certificate)}

                def Fetch(Path, Hash, Failure=None, Environment=TestEnvironment):
                    Result = subprocess.run([Cli, "package", "fetch", Origin + Path, "--sha256", Hash, "--source", Catalog],
                                            text=True, capture_output=True, env=Environment, timeout=15)
                    if Failure:
                        assert Result.returncode != 0 and Failure in Result.stderr, Result
                    else:
                        assert Result.returncode == 0, Result

                Fetch("/package", Digest, "PackageDownloadUnavailable", os.environ.copy())
                Fetch("/redirect", Digest, "PackageDownloadRejected")
                assert Requests == ["/redirect"]
                Fetch("/package", "0" * 64, "PackageArchiveDigestMismatch")
                assert not Catalog.exists()
                Fetch("/package", Digest)
                File = Catalog / "Hosted/1.0.0.luipkg"
                assert File.is_file()
                Existing = File.read_bytes()
                Fetch("/package", Digest)
                assert File.read_bytes() == Existing
            finally:
                Server.shutdown()
                Thread.join(timeout=3)
    print("[LUI:PackageHttps] TLS trust, no redirects, raw digest pin and idempotent import passed")


if __name__ == "__main__":
    Main()
