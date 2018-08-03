"""
Test the GDB remote reproducer.
"""

from __future__ import print_function

import os
import lldb
from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil


class TestGdbRemoteReproducer(TestBase):

    mydir = TestBase.compute_mydir(__file__)

    def test(self):
        """Test record and replay of gdb-remote packets."""
        self.build()

        # Create temp directory for the reproducer.
        temp_dir = tempfile.mkdtemp()

        # Run debug session and record gdb remote packets.
        self.runCmd("settings set generate-reproducer true")
        self.runCmd("settings set reproducer-path {}".format(temp_dir))

        exe = self.getBuildArtifact("a.out")
        target = self.dbg.CreateTarget(exe)

        lldbutil.run_break_set_by_file_and_line(
            self, "main.c", 13, loc_exact=True)

        self.runCmd("run", RUN_SUCCEEDED)

        self.expect(
            "bt",
            substrs=['a.out`foo', 'main.c:13', 'a.out`main', 'main.c:17'])

        self.expect('cont', 'testing')

        # Reset the debugger instance.
        lldb.SBDebugger.Destroy(lldb.DBG)
        lldb.DBG = lldb.SBDebugger.Create()

        # Ensure reproducer files are in our temp directory.
        self.assertTrue(os.path.exists(os.path.join(temp_dir, 'index.yaml')))
        self.assertTrue(
            os.path.exists(os.path.join(temp_dir, 'gdb-remote.yaml')))

        exe = self.getBuildArtifact("a.out")
        target = self.dbg.CreateTarget(exe)

        # Now replay the session from the reproducer.
        self.runCmd("settings set reproducer-path {}".format(temp_dir))

        lldbutil.run_break_set_by_file_and_line(
            self, "main.c", 13, loc_exact=True)

        self.runCmd("run", RUN_SUCCEEDED)

        self.expect(
            "bt",
            substrs=['a.out`foo', 'main.c:13', 'a.out`main', 'main.c:17'])

        # Ensure we're looking at the replay and we don't seen the binary's
        # output.
        self.expect('cont', matching=False, substrs=['testing'])

        # Reset the debugger instance.
        lldb.SBDebugger.Destroy(lldb.DBG)
        lldb.DBG = lldb.SBDebugger.Create()
