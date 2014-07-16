#!/usr/bin/env python3
#
#       gnl_testsuite.py 
#
# Copyright (c) 2014, Thibault Saunier tsaunier@gnome.org
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU Lesser General Public
# License as published by the Free Software Foundation; either
# version 2.1 of the License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public
# License along with this program; if not, write to the
# Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
# Boston, MA 02110-1301, USA.

def register_default_scenarios(self):
    """
    Registers default test scenarios
    """
    self.add_scenarios([
                 "play_15s",
                 "seek_forward",
                 "seek_backward",
                 "change_state_intensive",
                 "scrub_forward_seeking"])


COMPOSITION_DESCRIPTIONS = \
    [
        ("video.one_source",
         """gnlsource, bin_desc=\\" videotestsrc ! capsfilter caps=video/x-raw,height=720,width=1080 \\", start=0, duration=2.0,priority=1\;""", "autovideosink"),

        ("one_after_another",
         # TOPOLOGY
         #
         # 0           1           2  | Priority
         # ------------------------------------------
         # [       source1         ]  | 1
         #
         """gnlsource, bin_desc=\\" videotestsrc ! capsfilter caps=video/x-raw,height=720,width=1080 \\", start=0, duration=2.0, priority=2\;
            gnlsource, bin_desc=\\" videotestsrc pattern=snow ! capsfilter caps=video/x-raw,height=720,width=1080 \\", start=2.0, duration=2.0, priority=2\;""",
         "autovideosink"),

        ("video.one_under_another",
         # TOPOLOGY
         #
         # 0           1           2           3           4 | Priority
         # --------------------------------------------------------------
         # [       source1        ]                          | 1
         #             [        source2       ]              | 2
         #
         """gnlsource, bin_desc=\\" videotestsrc ! capsfilter caps=video/x-raw,height=720,width=1080 \\",start=0, duration=2.0,priority=0\;
            gnlsource, bin_desc=\\" videotestsrc pattern=snow ! capsfilter caps=video/x-raw,height=720,width=1080 \\", start=1.0, duration=2.0, priority=1\;""",
         "autovideosink"),

        ("video.complex_operations",

         # TOPOLOGY
         #
         # 0           1           2           3           4     ..   6 | Priority
         # ----------------------------------------------------------------------------
         #                         [------ oper ----------]             | 1
         # [--------------------- source1 ----------------]             | 2
         #                         [------------ source2       ------]  | 3
         """gnloperation, bin_desc=compositor, start=2.0, duration=2.0, priority=0\;\
            gnlsource, bin_desc=\\" videotestsrc ! capsfilter caps=video/x-raw,height=720,width=1080 \\", start=0, duration=4.0, priority=1\;
            gnlsource, bin_desc=\\" videotestsrc pattern=snow ! capsfilter caps=video/x-raw,height=720,width=1080 \\",  start=2.0, duration=4.0, priority=2\;""",
         "autovideosink"),

        ("video.complex_expandable_operations",
         # TOPOLOGY
         #
         # 0           1           2           3           4     ..   6 | Priority
         # ----------------------------------------------------------------------------
         # [ ......................[------ oper ----------]..........]  | 1 EXPANDABLE
         # [--------------------- source1 ----------------]             | 2
         #                         [------------ source2       ------]  | 3
         """gnloperation, bin_desc=compositor, start=\(guint64\) 2000000000, duration=\(guint64\) 2000000000, expandable=true, priority=0\;
            gnlsource, bin_desc=\\" videotestsrc ! capsfilter caps=video/x-raw,height=720,width=1080 \\", start=0, duration=4.0, priority=1\;
            gnlsource, bin_desc=\\" videotestsrc pattern=snow ! capsfilter caps=video/x-raw,height=720,width=1080 \\", start=2.0, duration=4.0, priority=2\;""",
         "autovideosink"),

        ("video.one_after_another_mixed",
         # TOPOLOGY
         #
         # 0           1           2           3           4     | Priority
         # ----------------------------------------------------------------------------
         # [ ...................... oper ..................]     | 1 EXPANDABLE
         # [--- source1------------][----- source2   ------]     | 2
         """gnlsource, bin_desc=\\" videotestsrc ! capsfilter caps=video/x-raw,height=720,width=1080 \\",start=0,duration=2.0,priority=2\;
            gnlsource, bin_desc=\\"videotestsrc pattern=snow\\",start=2.0,duration=2.0,priority=2\;
             gnloperation, bin_desc=compositor, expandable=true, priority=0""",
         "autovideosink"),

        ("audio.one_source",
         """gnlsource, bin_desc=\\" audiotestsrc ! capsfilter caps=audio/x-raw \\", start=0, duration=2.0,priority=1\;""", "autoaudiosink"),

        ("one_after_another",
         # TOPOLOGY
         #
         # 0           1           2  | Priority
         # ------------------------------------------
         # [       source1         ]  | 1
         #
         """gnlsource, bin_desc=\\" audiotestsrc ! capsfilter caps=audio/x-raw \\", start=0, duration=2.0, priority=2\;
            gnlsource, bin_desc=\\" audiotestsrc wave=triangle ! capsfilter caps=audio/x-raw \\", start=2.0, duration=2.0, priority=2\;""",
         "autoaudiosink"),

        ("audio.one_under_another",
         # TOPOLOGY
         #
         # 0           1           2           3           4 | Priority
         # --------------------------------------------------------------
         # [       source1        ]                          | 1
         #             [        source2       ]              | 2
         #
         """gnlsource, bin_desc=\\" audiotestsrc ! capsfilter caps=audio/x-raw \\",start=0, duration=2.0,priority=0\;
            gnlsource, bin_desc=\\" audiotestsrc wave=triangle ! capsfilter caps=audio/x-raw \\", start=1.0, duration=2.0, priority=1\;""",
         "autoaudiosink"),

        ("audio.complex_operations",

         # TOPOLOGY
         #
         # 0           1           2           3           4     ..   6 | Priority
         # ----------------------------------------------------------------------------
         #                         [------ oper ----------]             | 1
         # [--------------------- source1 ----------------]             | 2
         #                         [------------ source2       ------]  | 3
         """gnloperation, bin_desc=adder, start=2.0, duration=2.0, priority=0\;\
            gnlsource, bin_desc=\\" audiotestsrc ! capsfilter caps=audio/x-raw \\", start=0, duration=4.0, priority=1\;
            gnlsource, bin_desc=\\" audiotestsrc wave=triangle ! capsfilter caps=audio/x-raw \\",  start=2.0, duration=4.0, priority=2\;""",
         "autoaudiosink"),

        ("audio.complex_expandable_operations",
         # TOPOLOGY
         #
         # 0           1           2           3           4     ..   6 | Priority
         # ----------------------------------------------------------------------------
         # [ ......................[------ oper ----------]..........]  | 1 EXPANDABLE
         # [--------------------- source1 ----------------]             | 2
         #                         [------------ source2       ------]  | 3
         """gnloperation, bin_desc=adder, start=\(guint64\) 2000000000, duration=\(guint64\) 2000000000, expandable=true, priority=0\;
            gnlsource, bin_desc=\\" audiotestsrc ! capsfilter caps=audio/x-raw \\", start=0, duration=4.0, priority=1\;
            gnlsource, bin_desc=\\" audiotestsrc wave=triangle ! capsfilter caps=audio/x-raw \\", start=2.0, duration=4.0, priority=2\;""",
         "autoaudiosink"),

        ("audio.one_after_another_mixed",
         # TOPOLOGY
         #
         # 0           1           2           3           4     | Priority
         # ----------------------------------------------------------------------------
         # [ ...................... oper ..................]     | 1 EXPANDABLE
         # [--- source1------------][----- source2   ------]     | 2
         """gnlsource, bin_desc=\\" audiotestsrc ! capsfilter caps=audio/x-raw \\",start=0,duration=2.0,priority=2\;
            gnlsource, bin_desc=\\"audiotestsrc wave=triangle\\",start=2.0,duration=2.0,priority=2\;
             gnloperation, bin_desc=adder, expandable=true, priority=0""",
         "autoaudiosink")
    ]

def register_default_test_generators(self):
    """
    Registers default test generators
    """
    self.add_generators(
        [
            GnlValidateTestsGenerator("simple", self, COMPOSITION_DESCRIPTIONS)
        ]
    )

def register_default_blacklist(self):
    pass

def register_defaults(self):
    self.register_default_scenarios()
    self.register_default_blacklist()
    self.register_default_test_generators()

try:
    GnlValidateTestManager.register_defaults = register_defaults
    GnlValidateTestManager.register_default_blacklist = register_default_blacklist
    GnlValidateTestManager.register_default_test_generators = register_default_test_generators
    GnlValidateTestManager.register_default_scenarios = register_default_scenarios
except NameError:
    pass
