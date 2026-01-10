import base64
import http.server
import os
import threading
import time
import urllib.parse
from email.utils import formatdate


def _safe_join(root, path):
    path = path.lstrip("/")
    path = urllib.parse.unquote(path)
    full = os.path.normpath(os.path.join(root, path))
    if os.path.commonpath([root, full]) != os.path.abspath(root):
        raise ValueError("Path traversal detected")
    return full


def _build_propfind_response(path, is_dir):
    stat = os.stat(path)
    size = 0 if is_dir else stat.st_size
    last_modified = formatdate(stat.st_mtime, usegmt=True)
    etag = f"\"{stat.st_mtime}-{size}\""
    if is_dir:
        resource_type = "<d:resourcetype><d:collection/></d:resourcetype>"
    else:
        resource_type = "<d:resourcetype/>"
    xml = (
        "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
        "<d:multistatus xmlns:d=\"DAV:\">"
        "<d:response>"
        f"<d:href>{urllib.parse.quote(path)}</d:href>"
        "<d:propstat>"
        "<d:prop>"
        f"<d:getcontentlength>{size}</d:getcontentlength>"
        f"<d:getlastmodified>{last_modified}</d:getlastmodified>"
        f"<d:getetag>{etag}</d:getetag>"
        f"{resource_type}"
        "</d:prop>"
        "<d:status>HTTP/1.1 200 OK</d:status>"
        "</d:propstat>"
        "</d:response>"
        "</d:multistatus>"
    )
    return xml.encode("utf-8")


def make_handler(root, username, password, stats):
    auth_token = base64.b64encode(f"{username}:{password}".encode("utf-8")).decode("ascii")

    class WebDavHandler(http.server.BaseHTTPRequestHandler):
        server_version = "MockWebDAV/1.0"

        def _check_auth(self):
            header = self.headers.get("Authorization", "")
            if not header.startswith("Basic "):
                return False
            return header[6:] == auth_token

        def _send_unauthorized(self):
            self.send_response(401)
            self.send_header("WWW-Authenticate", "Basic realm=\"Test\"")
            self.end_headers()

        def log_message(self, format, *args):
            return

        def do_PROPFIND(self):
            stats["propfind_calls"] += 1
            if not self._check_auth():
                self._send_unauthorized()
                return

            try:
                fs_path = _safe_join(root, self.path)
            except ValueError:
                self.send_response(400)
                self.end_headers()
                return

            if not os.path.exists(fs_path):
                self.send_response(404)
                self.end_headers()
                return

            is_dir = os.path.isdir(fs_path)
            body = _build_propfind_response(fs_path, is_dir)
            self.send_response(207)
            self.send_header("Content-Type", "application/xml; charset=utf-8")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def do_MKCOL(self):
            stats["mkcol_calls"] += 1
            if not self._check_auth():
                self._send_unauthorized()
                return

            try:
                fs_path = _safe_join(root, self.path)
            except ValueError:
                self.send_response(400)
                self.end_headers()
                return

            parent = os.path.dirname(fs_path)
            if not os.path.isdir(parent):
                self.send_response(409)
                self.end_headers()
                return

            if os.path.exists(fs_path):
                self.send_response(405)
                self.end_headers()
                return

            os.makedirs(fs_path, exist_ok=True)
            self.send_response(201)
            self.end_headers()

        def do_PUT(self):
            stats["put_calls"] += 1
            if not self._check_auth():
                self._send_unauthorized()
                return

            try:
                fs_path = _safe_join(root, self.path)
            except ValueError:
                self.send_response(400)
                self.end_headers()
                return

            parent = os.path.dirname(fs_path)
            if not os.path.isdir(parent):
                self.send_response(409)
                self.end_headers()
                return

            length = int(self.headers.get("Content-Length", "0"))
            data = self.rfile.read(length)
            with open(fs_path, "wb") as f:
                f.write(data)
            self.send_response(201)
            self.end_headers()

        def do_DELETE(self):
            stats["delete_calls"] += 1
            self.send_response(405)
            self.end_headers()

    return WebDavHandler


class WebDavTestServer:
    def __init__(self, root, host="127.0.0.1", port=0, username="user", password="pass"):
        self.root = os.path.abspath(root)
        self.host = host
        self.port = port
        self.username = username
        self.password = password
        self.stats = {
            "propfind_calls": 0,
            "mkcol_calls": 0,
            "put_calls": 0,
            "delete_calls": 0,
        }
        self._server = None
        self._thread = None

    def start(self):
        handler = make_handler(self.root, self.username, self.password, self.stats)
        self._server = http.server.ThreadingHTTPServer((self.host, self.port), handler)
        self.port = self._server.server_address[1]
        self._thread = threading.Thread(target=self._server.serve_forever, daemon=True)
        self._thread.start()
        time.sleep(0.1)

    def stop(self):
        if self._server:
            self._server.shutdown()
            self._server.server_close()
        if self._thread:
            self._thread.join(timeout=2)


if __name__ == "__main__":
    import argparse

    parser = argparse.ArgumentParser()
    parser.add_argument("--root", required=True)
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=19000)
    parser.add_argument("--user", default="user")
    parser.add_argument("--password", default="pass")
    args = parser.parse_args()

    os.makedirs(args.root, exist_ok=True)
    server = WebDavTestServer(args.root, args.host, args.port, args.user, args.password)
    server.start()
    print(f"Mock WebDAV server running on {args.host}:{server.port}")
    try:
        while True:
            time.sleep(1)
    except KeyboardInterrupt:
        server.stop()
