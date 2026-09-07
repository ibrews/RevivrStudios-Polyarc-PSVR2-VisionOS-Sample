#!/usr/bin/env python3
"""Check the preserved provisioning profile against this package's identity."""
import argparse
import datetime
import fnmatch
import hashlib
import json
import plistlib
from pathlib import Path
import subprocess


def validate(data, bundle, certificate, device, now=None):
    now = now or datetime.datetime.now(datetime.timezone.utc)
    expiry = data['ExpirationDate'].replace(tzinfo=datetime.timezone.utc)
    entitlements = data['Entitlements']
    checks = {
        'team': data.get('TeamIdentifier') == ['C624J4S2F8'],
        'bundle': fnmatch.fnmatchcase('C624J4S2F8.' + bundle, entitlements.get('application-identifier', '')),
        'device': device in data.get('ProvisionedDevices', []),
        'certificate': certificate.upper() in [hashlib.sha1(cert).hexdigest().upper() for cert in data.get('DeveloperCertificates', [])],
        'expiration': expiry > now,
    }
    failed = [key for key, passed in checks.items() if not passed]
    if failed:
        raise ValueError('profile mismatch: ' + ', '.join(failed))
    return {'result': 'pass', 'uuid': data['UUID'], 'bundle': bundle,
            'team': 'C624J4S2F8', 'device': device, 'certificate_sha1': certificate,
            'expires_utc': expiry.isoformat()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('profile', type=Path)
    parser.add_argument('--bundle', required=True)
    parser.add_argument('--certificate', required=True)
    parser.add_argument('--device', required=True)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    # Verify the CMS signature without importing certificates into the SSH
    # process's keychain. Device installation separately validates Apple trust.
    decoded = subprocess.check_output(['openssl', 'smime', '-verify', '-inform', 'DER', '-noverify', '-in', str(args.profile)])
    result = validate(plistlib.loads(decoded), args.bundle, args.certificate, args.device)
    result['profile_sha256'] = hashlib.sha256(args.profile.read_bytes()).hexdigest()
    args.output.write_text(json.dumps(result, indent=2) + '\n')
    print('SIGNING_PROFILE_OK ' + result['uuid'])


if __name__ == '__main__':
    main()

