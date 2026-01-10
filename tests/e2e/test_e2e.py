import argparse
import os
import subprocess
import tempfile
import time

from mock_webdav_server import WebDavTestServer


def write_file(path, data):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.write(data)


def run_uploader(uploader, source, base_url):
    cmd = [
        uploader,
        "--source",
        source,
        "--remote",
        "/RemoteRoot",
        "--email",
        "user",
        "--app-password",
        "pass",
        "--base-url",
        base_url,
    ]
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        raise RuntimeError(f"Uploader failed: {result.stderr}\n{result.stdout}")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--uploader", required=True)
    args = parser.parse_args()

    with tempfile.TemporaryDirectory() as local_dir, tempfile.TemporaryDirectory() as remote_dir:
        write_file(os.path.join(local_dir, "sub", "image.jpg"), b"\x01\x02")
        write_file(os.path.join(local_dir, "sub", "doc.txt"), b"old")
        write_file(os.path.join(local_dir, "new.txt"), b"new")

        old_time = time.time() - 48 * 3600
        os.utime(os.path.join(local_dir, "sub", "doc.txt"), (old_time, old_time))
        os.utime(os.path.join(local_dir, "sub", "image.jpg"), (old_time, old_time))

        server = WebDavTestServer(remote_dir, username="user", password="pass")
        server.start()
        try:
            base_url = f"http://127.0.0.1:{server.port}"
            run_uploader(args.uploader, local_dir, base_url)
            first_puts = server.stats["put_calls"]

            run_uploader(args.uploader, local_dir, base_url)
            second_puts = server.stats["put_calls"]

            assert first_puts == second_puts
            assert os.path.exists(os.path.join(local_dir, "new.txt"))
        finally:
            server.stop()


if __name__ == "__main__":
    main()
