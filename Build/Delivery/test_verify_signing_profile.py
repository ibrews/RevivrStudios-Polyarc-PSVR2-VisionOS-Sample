import datetime
import hashlib
import unittest
from verify_signing_profile import validate


class ProfileTests(unittest.TestCase):
    def setUp(self):
        self.cert = hashlib.sha1(b'fixture certificate').hexdigest().upper()
        self.data = {'UUID': 'fixture', 'TeamIdentifier': ['C624J4S2F8'],
                     'Entitlements': {'application-identifier': 'C624J4S2F8.com.agilelens.*'},
                     'ProvisionedDevices': ['m5'], 'DeveloperCertificates': [b'fixture certificate'],
                     'ExpirationDate': datetime.datetime(2100, 1, 1)}

    def test_valid(self):
        self.assertEqual(validate(self.data, 'com.agilelens.lumenwork.MyProject', self.cert, 'm5')['result'], 'pass')

    def test_wrong_bundle(self):
        with self.assertRaisesRegex(ValueError, 'bundle'):
            validate(self.data, 'com.other.app', self.cert, 'm5')

    def test_wrong_device(self):
        with self.assertRaisesRegex(ValueError, 'device'):
            validate(self.data, 'com.agilelens.app', self.cert, 'm2')

    def test_wrong_certificate(self):
        with self.assertRaisesRegex(ValueError, 'certificate'):
            validate(self.data, 'com.agilelens.app', 'different', 'm5')

    def test_expired(self):
        self.data['ExpirationDate'] = datetime.datetime(2000, 1, 1)
        with self.assertRaisesRegex(ValueError, 'expiration'):
            validate(self.data, 'com.agilelens.app', self.cert, 'm5')


if __name__ == '__main__':
    unittest.main()

