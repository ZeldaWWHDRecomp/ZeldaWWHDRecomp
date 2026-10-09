import unittest

from resource_sampler import (ResourceSampler, available_bytes, memory_totals,
                              private_bytes, process_tree)


PACKAGE = "org.wwhdrecomp.wwhd"
PS = """  PID PPID NAME
    1    0 init
   10    1 org.wwhdrecomp.wwhd
   11    1 org.wwhdrecomp.wwhd:setup
   12   11 clang
   13   12 cc1
   20    1 org.wwhdrecomp.wwhd.other
   21   20 lld
"""
# Android API 36.1 dumpsys meminfo -s output, reduced to its summary totals.
MEMORY = "           TOTAL PSS:     3957            TOTAL RSS:     4316       TOTAL SWAP PSS:     1956\n"


class ResourceSamplerTest(unittest.TestCase):
    def test_package_boundary_and_native_descendants(self):
        self.assertEqual(process_tree(PS, PACKAGE),
                         {10: "app", 11: "app", 12: "native_child", 13: "native_child"})
        self.assertEqual(process_tree("PID PPID NAME\n1 0 init", PACKAGE), {})

    def test_summary_totals_and_missing_rss(self):
        self.assertEqual(memory_totals(MEMORY), {"pss_bytes": 3957 * 1024, "rss_bytes": 4316 * 1024})
        self.assertEqual(memory_totals("TOTAL PSS: 20"), {"pss_bytes": 20480, "rss_bytes": None})
        for invalid in ("No process found for: 12", "TOTAL RSS: 100", "TOTAL PSS: invalid"):
            with self.assertRaises(ValueError): memory_totals(invalid)

    def test_storage_requires_complete_numeric_output(self):
        self.assertEqual(private_bytes("304988\t.\n"), 304988 * 1024)
        self.assertEqual(available_bytes("Filesystem 1K-blocks Used Available Use% Mounted on\n/dev/block/dm-54 6082144 1199428 4740504 21% /data/user/0\n"), 4740504 * 1024)
        for invalid in ("", "2 files\n3 .", "du: permission denied", "12 ./other"):
            with self.assertRaises(ValueError): private_bytes(invalid)
        with self.assertRaises(ValueError): available_bytes("Filesystem\npermission denied")

    def sample(self, after=PS, missing=None):
        reads = 0

        def shell(*command):
            nonlocal reads
            if command[0] == "ps":
                reads += 1
                return (PS if reads == 1 else after).encode()
            if command[0] == "dumpsys":
                return ("No process found" if command[-1] == missing else MEMORY).encode()
            if command[-1].endswith("host.json"): return b'{"state":"running","stage":"compile","input":"private-name"}'
            if command[-1].endswith("state.json"): return b'{"last_event":{"stage":"compile","source":"private-source","arguments":"private-arguments"}}'
            if "du" in command: return b"123 ."
            if command[0] == "df": return b"Filesystem 1K-blocks Used Available Use% Mounted on\n/dev/a 1000 400 600 40% /data\n"
            raise AssertionError(command)

        sampler = ResourceSampler(shell, PACKAGE, "no_backup/ondevice/jobs/authored")
        return sampler.stop()

    def test_stable_group_sums_include_tools_and_exclude_input_names(self):
        result = self.sample()
        self.assertEqual(result["max_sampled_sum_pss_bytes"], 4 * 3957 * 1024)
        self.assertEqual(result["complete_memory_samples"], 1)
        self.assertEqual(result["native_child_samples"], 1)
        self.assertEqual(result["complete_native_child_samples"], 1)
        self.assertNotIn("private-name", str(result))
        self.assertNotIn("private-source", str(result))
        self.assertNotIn("private-arguments", str(result))
        self.assertEqual(result["samples"][0]["job_state"]["stage"], "compile")

    def test_exit_or_process_birth_does_not_claim_complete_peak(self):
        for result in (self.sample(missing="12"), self.sample(after=PS + "14 11 lld\n")):
            self.assertEqual(result["complete_memory_samples"], 0)
            self.assertEqual(result["incomplete_memory_samples"], 1)
            self.assertIsNone(result["max_sampled_sum_pss_bytes"])
            self.assertEqual(result["complete_native_child_samples"], 0)


if __name__ == "__main__": unittest.main()
