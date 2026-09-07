import csv
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

from Build.Delivery.verify_lumen_lite_cook import VerificationError, verify_shader_stats


def write_stats(directory: str, rows: list[dict[str, str]]) -> Path:
    path = Path(directory) / "ShaderTypeStats-METAL_SM5_IOS.csv"
    with path.open("w", encoding="utf-8", newline="") as stream:
        writer = csv.DictWriter(
            stream,
            fieldnames=["ShaderTypeName", "ShaderType", "Frequency", "NumShaders", "ShadersSize"],
        )
        writer.writeheader()
        writer.writerows(rows)
    return path


def valid_rows() -> list[dict[str, str]]:
    return [
        {"ShaderTypeName": "FDownsampleDepthAndNormalCS", "ShaderType": "Global", "Frequency": "SF_Compute", "NumShaders": "1", "ShadersSize": "6467"},
        {"ShaderTypeName": "FMarkProbesUsedByGBufferCS", "ShaderType": "Global", "Frequency": "SF_Compute", "NumShaders": "2", "ShadersSize": "14700"},
        {"ShaderTypeName": "FInterpolateIrradianceProbesToPixelsCS", "ShaderType": "Global", "Frequency": "SF_Compute", "NumShaders": "4", "ShadersSize": "56232"},
    ]


class LumenLiteShaderStatsTests(unittest.TestCase):
    def test_complete_inventory_passes(self):
        with tempfile.TemporaryDirectory() as directory:
            result = verify_shader_stats(write_stats(directory, valid_rows()))
        self.assertEqual((4, 56232), result["FInterpolateIrradianceProbesToPixelsCS"])

    def test_missing_shader_fails(self):
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaisesRegex(VerificationError, "FMarkProbesUsedByGBufferCS=missing"):
                verify_shader_stats(write_stats(directory, valid_rows()[:1] + valid_rows()[2:]))

    def test_zero_shader_count_fails(self):
        rows = valid_rows()
        rows[2]["NumShaders"] = "0"
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaisesRegex(VerificationError, "NumShaders:0"):
                verify_shader_stats(write_stats(directory, rows))

    def test_truncated_permutation_domain_fails(self):
        rows = valid_rows()
        rows[1]["NumShaders"] = "1"
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaisesRegex(VerificationError, "require >= 2"):
                verify_shader_stats(write_stats(directory, rows))

    def test_wrong_platform_filename_fails(self):
        with tempfile.TemporaryDirectory() as directory:
            path = write_stats(directory, valid_rows())
            wrong = path.with_name("ShaderTypeStats-METAL_ES3_1_IOS.csv")
            path.rename(wrong)
            with self.assertRaisesRegex(VerificationError, "METAL_SM5_IOS"):
                verify_shader_stats(wrong)


if __name__ == "__main__":
    unittest.main()
