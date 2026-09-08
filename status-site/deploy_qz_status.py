"""Deploy wilhelm-media.at/QZ-status via SFTP (IONOS).

Reuses the wilhelm-media SFTP account from ../../wilhelm-media/.env, exactly as
deploy_artwork.py does. Selective and NON-wiping: touches only QZ-status/,
the rest of the docroot stays untouched.

  python deploy_qz_status.py --dry-run
  python deploy_qz_status.py
"""
import sys, stat
from pathlib import Path
import paramiko

LOCAL = Path(__file__).parent
ENV_PATH = LOCAL.parent.parent / "wilhelm-media" / ".env"
REMOTE_DIR = "QZ-status"
DRY = "--dry-run" in sys.argv
FILES = ["index.html"]


def load_env(p):
    if not p.exists():
        sys.exit("[!] Missing %s" % p)
    env = {}
    for line in p.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        k, v = line.split("=", 1)
        env[k.strip()] = v.strip()
    return env


ENV = load_env(ENV_PATH)
print("[i] host=%s user=%s -> %s/" % (ENV["SFTP_HOST"], ENV["SFTP_USER"], REMOTE_DIR))
for fn in FILES:
    p = LOCAL / fn
    print("    %-16s %8d bytes  %s" % (fn, p.stat().st_size,
                                       "OK" if p.exists() else "MISSING"))
if DRY:
    print("[i] dry run - nothing uploaded")
    sys.exit(0)

t = paramiko.Transport((ENV["SFTP_HOST"], int(ENV.get("SFTP_PORT", 22))))
t.connect(username=ENV["SFTP_USER"], password=ENV["SFTP_PASS"])
sftp = paramiko.SFTPClient.from_transport(t)
try:
    sftp.stat(REMOTE_DIR)
    print("[i] remote dir exists")
except IOError:
    sftp.mkdir(REMOTE_DIR)
    print("[+] created %s/" % REMOTE_DIR)
for fn in FILES:
    remote = "%s/%s" % (REMOTE_DIR, fn)
    sftp.put(str(LOCAL / fn), remote)
    print("[+] uploaded %s (%d bytes)" % (remote, sftp.stat(remote).st_size))
sftp.close()
t.close()
print("[OK] https://wilhelm-media.at/%s/" % REMOTE_DIR)
