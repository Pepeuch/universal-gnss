"""Prepare private local-console provisioning; never opens a serial port or flashes.

Output contains credentials/private keys. Keep it outside the repository, transmit
only through the intended local console, trust the certificate fingerprint locally,
and remove private material from the host when it is no longer needed.
"""
import argparse
import hashlib
import ipaddress
import json
import os
from pathlib import Path
import re
import secrets
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[4]


def prepare(output, hostname, address=None):
    output = Path(output).resolve()
    if output == ROOT or ROOT in output.parents or output.exists():
        raise ValueError('output must be new and outside UG source roots')
    if not re.fullmatch(r'[a-z0-9](?:[a-z0-9-]{0,30}[a-z0-9])?', hostname):
        raise ValueError('invalid local hostname')
    extra = ',IP:' + str(ipaddress.ip_address(address)) if address else ''
    output.mkdir(mode=0o700, parents=True)
    # No global umask mutation, and no credential text in command arguments/logs.
    with tempfile.TemporaryDirectory(dir=output) as temporary:
        key, cert = Path(temporary) / 'key.pem', Path(temporary) / 'cert.pem'
        subprocess.run(['openssl', 'req', '-x509', '-newkey', 'ec', '-pkeyopt',
                        'ec_paramgen_curve:P-256', '-nodes', '-days', '365',
                        '-subj', '/CN=' + hostname + '.local', '-addext',
                        'subjectAltName=DNS:' + hostname + '.local' + extra,
                        '-addext', 'basicConstraints=critical,CA:FALSE',
                        '-addext', 'keyUsage=critical,digitalSignature',
                        '-addext', 'extendedKeyUsage=serverAuth',
                        '-keyout', str(key), '-out', str(cert)],
                       check=True, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
        token = secrets.token_hex(32)
        identity = {'certificate_pem': cert.read_text(), 'private_key_pem': key.read_text(),
                    'confirm': 'REPLACE_TLS_IDENTITY'}
        settings = {'name': hostname, 'admin_token_sha256': hashlib.sha256(token.encode()).hexdigest()}
        commands = 'tls-init ' + json.dumps(identity, separators=(',', ':')) + '\n'
        commands += 'config ' + json.dumps(settings, separators=(',', ':')) + '\n'
        commands += 'reboot CONFIRM\n'
        for name, contents in {'console-input.txt': commands, 'admin-token.txt': token + '\n',
                               'certificate.pem': cert.read_text()}.items():
            with os.fdopen(os.open(output / name, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600), 'w') as stream:
                stream.write(contents)
    fingerprint = subprocess.check_output(['openssl', 'x509', '-in', str(output / 'certificate.pem'),
                                           '-noout', '-fingerprint', '-sha256'], text=True).strip()
    print('Prepared private files outside UG. No serial access, flash or publication.')
    print(fingerprint)
    return output


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', required=True)
    parser.add_argument('--hostname', default='ug-rtk')
    parser.add_argument('--ip', help='optional intended local address included in certificate SAN')
    args = parser.parse_args()
    prepare(args.output, args.hostname, args.ip)
