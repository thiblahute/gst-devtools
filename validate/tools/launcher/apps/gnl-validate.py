#!/usr/bin/env python3
#
#       gnl-validate.py 
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

import os
import re
import time
import urlparse
import subprocess
import ConfigParser
import xml.etree.ElementTree as ET
from loggable import Loggable

from baseclasses import GstValidateTest, TestsManager, Test, \
    ScenarioManager, NamedDic, GstValidateTestsGenerator
from utils import path2url, DEFAULT_TIMEOUT, which, GST_SECOND, Result

######################################
#       Private global variables     #
######################################

# definitions of commands to use
GNL_VALIDATE_COMMAND = "gnl-validate-1.0"
if "win32" in sys.platform:
    GNL_VALIDATE_COMMAND += ".exe"


#################################################
#       API to be used to create testsuites     #
#################################################


class GnlValidateTestsGenerator(GstValidateTestsGenerator):
    def __init__(self, name, test_manager, gnlcomposition_descriptions=None,
                 valid_scenarios=[]):
        """
        @name: The name of the generator
        @gnlcomposition_descriptions: A list of tuple of the form:
                                 (test_name, pipeline_description)
        @valid_scenarios: A list of scenario name that can be used with that generator
        """
        GstValidateTestsGenerator.__init__(self, name, test_manager)
        self._gnlcomposition_description = gnlcomposition_descriptions
        self._valid_scenarios = valid_scenarios

    def get_fname(self, scenario, protocol=None, name=None):
        if name is None:
            name = self.name

        if protocol is not None:
            protocol_str = "%s." % protocol
        else:
            protocol_str = ""

        if scenario is not None and scenario.name.lower() != "none":
            return "%s.%s%s.%s" % ("gnl", protocol_str, name, scenario.name)

        return "%s.%s%s" % ("gnl", protocol_str, name)

    def generate_tests(self, uri_minfo_special_scenarios, scenarios):

        if self._valid_scenarios:
            scenarios = [scenario for scenario in scenarios if
                          scenario.name in self._valid_scenarios]

        return super(GnlValidateTestsGenerator, self).generate_tests(
              uri_minfo_special_scenarios, scenarios)

    def populate_tests(self, uri_minfo_special_scenarios, scenarios):
        for name, composition, sink in self._gnlcomposition_description:
            for scenario in scenarios:
                fname = self.get_fname(scenario, name=name)
                self.add_test(GnlValidateLaunchTest(fname,
                                                    self.test_manager.options,
                                                    self.test_manager.reporter,
                                                    composition,
                                                    sink,
                                                    scenario=scenario)
                              )


class GnlValidateLaunchTest(GstValidateTest):
    def __init__(self, classname, options, reporter, composition_desc, sink,
                 timeout=DEFAULT_TIMEOUT, scenario=None, media_descriptor=None):
        try:
            timeout = GST_VALIDATE_PROTOCOL_TIMEOUTS[media_descriptor.get_protocol()]
        except KeyError:
            pass
        except AttributeError:
            pass

        duration = 0
        if scenario:
            duration = scenario.get_duration()
        elif media_descriptor:
            duration = media_descriptor.get_duration() / GST_SECOND

        super(GnlValidateLaunchTest, self).__init__(GNL_VALIDATE_COMMAND, classname,
                                              options, reporter,
                                              duration=duration,
                                              scenario=scenario,
                                              timeout=timeout)

        self.sink = sink
        self.composition_desc = composition_desc
        self.media_descriptor = media_descriptor

    def build_arguments(self):
        GstValidateTest.build_arguments(self)
        self.add_arguments(re.sub( '\s+', ' ', self.composition_desc.replace("\n", ' ')).strip())

        sink = "--set-sink "
        if self.options.mute:
            sink += "fakesink"
        else:
            sink += self.sink

        self.add_arguments(sink)

    def get_current_value(self):
        if self.scenario:
            sent_eos = self.sent_eos_position()
            if sent_eos is not None:
                t = time.time()
                if ((t - sent_eos)) > 30:
                    if self.media_descriptor.get_protocol() == Protocols.HLS:
                        self.set_result(Result.PASSED,
                                        """Got no EOS 30 seconds after sending EOS,
                                        in HLS known and tolerated issue:
                                        https://bugzilla.gnome.org/show_bug.cgi?id=723868""")
                        return Result.KNOWN_ERROR

                    self.set_result(Result.FAILED, "Pipeline did not stop 30 Seconds after sending EOS")

                    return Result.FAILED

        return self.get_current_position()


class GnlValidateTestManager(GstValidateBaseTestManager):

    name = "gnl"

    def __init__(self):
        super(GnlValidateTestManager, self).__init__()
        self._uris = []
        self._run_defaults = True
        self._is_populated = False
        execfile(os.path.join(os.path.dirname(__file__), "apps",
                 "gnl", "gnl_testsuite.py"), globals())

    def init(self):
        if which(GNL_VALIDATE_COMMAND):
            return True
        return False

    def add_options(self, parser):
        group = parser.add_argument_group("GstValidate tools specific options"
                            " and behaviours",
description="""When using --wanted-tests, all the scenarios can be used, even those which have
not been tested and explicitely activated if you set use --wanted-tests ALL""")

    def populate_testsuite(self):

        if self._is_populated is True:
            return

        if not self.options.config:
            if self._run_defaults:
                self.register_defaults()
            else:
                self.register_all()

        self._is_populated = True

    def _list_uris(self):
        return []

    def list_tests(self):
        if self.tests:
            return self.tests

        if self._run_defaults:
            scenarios = [self.scenarios_manager.get_scenario(scenario_name)
                         for scenario_name in self.get_scenarios()]
        else:
            scenarios = self.scenarios_manager.get_scenario(None)
        uris = self._list_uris()

        for generator in self.get_generators():
            for test in generator.generate_tests(uris, scenarios):
                self.add_test(test)

        return self.tests

    def set_settings(self, options, args, reporter):
        if options.wanted_tests:
            for i in range(len(options.wanted_tests)):
                if "ALL" in options.wanted_tests[i]:
                    self._run_defaults = False
                    options.wanted_tests[i] = options.wanted_tests[i].replace("ALL", "")
        try:
            options.wanted_tests.remove("")
        except ValueError:
            pass

        super(GnlValidateTestManager, self).set_settings(options, args, reporter)

def gst_validate_checkout_element_present(element_name):
    null = open(os.devnull)
    return subprocess.call("gst-inspect-1.0 %s" %element_name, shell=True, stdout=null, stderr=null)
