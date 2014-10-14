#!/usr/bin/env python2
#
# Copyright (c) 2013,Thibault Saunier <thibault.saunier@collabora.com>
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
import sys
import time
import signal
import atexit
import tempfile
import subprocess
import traceback
import launcher.loggable as loggable

PROCESS_NAME = "org.freedesktop.gstvalidate"
ACTIVITY_NAME = "org.freedesktop.gstvalidate.GstValidateLaunch"
DEFAULT_READING_COMMAND = "adb shell logcat -c && adb shell logcat -v raw -s"

DEFAULT_REMOTE_TMP_DIR = "/sdcard/gst-validate/"

def _runcommand(command):
    command = "adb %s" % command
    loggable.log("utils", "Launching %s" %command)

    return subprocess.check_output(command, shell=True)


def copy_to_remote(local_path, remote_path):
    res = _runcommand("push %s %s" % (local_path, remote_path))

    return remote_path


def copy_from_remote(remote_path, local_path):
    res = _runcommand("pull %s %s"
            % (remote_path, local_path))

def mkdir(dirname):
    try:
        _runcommand("shell mkdir -p %s" % dirname)
    except subprocess.CalledProcessError as e:
        loggable.warning("utils", "Issue running command: %s" % e)
        return False

    return True

def remove(filename):
    _runcommand("shell rm %s" % filename)

def copy_media_to_remote(local_path):
    wanted_remote = os.path.join(DEFAULT_REMOTE_TMP_DIR, "medias",
                                 os.path.basename(local_path))

    mkdir(os.path.dirname(wanted_remote))

    try:
        res = _runcommand("shell ls -ls %s" % (wanted_remote))
    except subprocess.CalledProcessError as e:
        loggable.warning("utils", "Issue running command: %s" % e)
        return None

    if "No such file or directory" in res:
        return copy_to_remote(local_path, wanted_remote)

    size = int(res.split()[3])
    if size == os.path.getsize(local_path):
        return wanted_remote

    return copy_to_remote(local_path, wanted_remote)

class ReadAdbLogs(loggable.Loggable):

    def __init__(self, category, outputfile=sys.stdout, check_envvar=None):
        loggable.Loggable.__init__(self)

        if check_envvar is not None:
            tmp = os.environ.get(check_envvar)
            if tmp is not None:
                outputfile = tmp

        self._set_output(outputfile)
        self.category = category

    def _set_output(self, outputfile):
        if isinstance(outputfile, str):
            self.debug("Using %s as output" % outputfile)
            self.out = open(outputfile, 'w')
            self.out.truncate()
        else:
            self.out = outputfile

    def start(self):
        command = DEFAULT_READING_COMMAND + " " + self.category

        self.debug("Launching %s redirected to %s"
                   % (command, self.out))
        self.process = subprocess.Popen(command,
                                        stderr=self.out,
                                        stdout=self.out,
                                        shell=True,
                                        preexec_fn=os.setsid)
        self.debug("Launched")

    def stop(self, force=False):
        if self.process is None:
            return

        while self.process.poll() is None:
            os.killpg(self.process.pid, signal.SIGTERM)
            time.sleep(1)
        self.process = None


class ReadValidateAdbLogs(ReadAdbLogs):
    def __init__(self, category="GstValidateOutput", outputfile=sys.stdout):
        ReadAdbLogs.__init__(self, category)

        self.tmpfile = None
        self.out = None
        validate_ofiles = os.environ.get("GST_VALIDATE_FILE")
        if validate_ofiles:
            files = validate_ofiles.split(os.pathsep)

            if "stderr" not in files and  "stdout" not in files:
                self._set_output(files[0])
            else:
                for f in files:
                    if f == "stderr":
                        self._set_output(sys.stderr)
                    elif f == "stdout":
                        self._set_output(sys.stdout)
                    elif not self.tmpfile:
                        # Clean the tmp file
                        open(f, "w").close()
                        self.tmpfile = f
                    elif not self.out:
                        self._set_output(f)

                    if self.tmpfile and self.out:
                        break
            print("Tmp: %s, out: %s" %(self.tmpfile, self.out))
        else:
            self._set_output(sys.stdout)

        self.category = "GstValidateOutput"
        self.retcode = None
        self.retmessage = None

    def start(self):
        command = DEFAULT_READING_COMMAND + " " + self.category
        if self.out in [sys.stdout, sys.stderr]:
            if self.tmpfile is None:
                tmpfile = tempfile.NamedTemporaryFile()
                self.tmpfile = tmpfile.name
            command += "|tee %s" % self.tmpfile
        elif self.tmpfile is None:
            self.tmpfile = self.out.name

        self.debug("Launching %s redirected to %s"
                   % (command, self.out))
        self.process = subprocess.Popen(command,
                                        stderr=self.out,
                                        stdout=self.out,
                                        shell=True,
                                        preexec_fn=os.setsid)
        self.debug("Launched")

    def _get_result(self):
        self.debug ("Opening %s" % self.tmpfile)
        try:
            tmpfile = open(self.tmpfile, 'r')
        except IOError as e:
            self.error("Could not open file: %s", e)

            return self.retcode, self.retmessage

        for l in reversed(tmpfile.readlines()):
            if "<RETURN:" in l:
                m = l
                tmpr = m.split(" ", 2)
                self.retcode = int(tmpr[1])
                self.retmessage = re.sub("^\(|\) />", '', tmpr[2])
                self.info("Subprocess RETURNED: %s" % self.retcode)
                break

        return self.retcode, self.retmessage

    def check_result(self):
        if self.process is None:
            return

        self.process.poll()

        if self.process.returncode is not None:
            if self.retcode is None:
                self.retcode = -1
                self.retmessage = "adb stopped"
            return

        retcode, msg = self._get_result()
        if retcode is not None:
            self.info("Result is %s -- message %s"
                       % (retcode, msg))
            self.running = False

    def stop(self, force=False):
        if self.process is None:
            return

        self.info("Stopping process (forced: %s)", force)
        if self.retcode is None and force is False:
            for i in range(10):
                self.check_result()
                if self.retcode is not None:
                    break

                time.sleep(1)

        if self.retcode is None:
            self.retcode = -1
            self.retmessage = "Process never gave any result"

        while self.process.poll() is None:
            os.killpg(self.process.pid, signal.SIGTERM)
            time.sleep(1)
        self.process = None


class GstValidateAndroid(loggable.Loggable):
    def __init__(self):
        loggable.Loggable.__init__(self)
        self.force_clean = False
        self.validate_logs = None
        self.glib_logs = None
        self.gst_logs = None

    def _remote_is_running(self):
        try:
            output = _runcommand("shell ps |grep %s"
                                 % PROCESS_NAME)
        except subprocess.CalledProcessError as e:
            self.info("Remote is not running anymore")
            return False

        return True

    def _check_result(self):
        assert(self.validate_logs is not None)
        if self._remote_is_running() is False:
            self.validate_logs.stop()
            self.debug("Process stopped, returning %s",
                       self.validate_logs.retcode)

        self.validate_logs.check_result()

        return self.validate_logs.retcode

    def clean(self):
        if self._remote_is_running():
            command = "shell am force-stop %s" % PROCESS_NAME
            try:
                self.info("Cleaning process ($ %s)" % command)
                _runcommand(command)
            except subprocess.CalledProcessError as e:
                pass
        self.validate_logs.stop(self.force_clean)
        self.glib_logs.stop(self.force_clean)
        self.gst_logs.stop(self.force_clean)

        for i in range(len(sys.argv)):
            if sys.argv[i] == "--scenarios-defs-output-file":
                try:
                    command = "pull /sdcard/scenarios %s" % sys.argv[i + 1]
                    self.info("Getting scenario list from /sdcard/scenarios "
                              "to %s ($ %s" % (sys.argv[i + 1], command))
                    _runcommand(command)
                except Exception as e:
                    self.error("Could not copy remote:/sdcard/scenarios to %s: %s"
                              % (sys.argv[i + 1], e))

    def run(self, tool):
        _runcommand("start-server")
        self.validate_logs = ReadValidateAdbLogs()
        self.validate_logs.start()

        self.glib_logs = ReadAdbLogs("GLib")
        self.glib_logs.start()

        self.gst_logs = ReadAdbLogs("GStreamer", check_envvar="GST_DEBUG_FILE")
        self.gst_logs.start()

        if os.environ.get("GST_DEBUG"):
            sys.argv.extend(["--gst-debug", os.environ.get("GST_DEBUG")])

        try:
            if len(sys.argv) < 2 or sys.argv[1] != tool:
                sys.argv.insert(1, tool)

            command = "shell am start -a android.intent.action.VIEW -c " \
                      "android.intent.category.DEFAULT -n %s/%s -e args '%s'" \
                      % (PROCESS_NAME, ACTIVITY_NAME, ' '.join(sys.argv[1:]))

            self.debug("Launching: %s" % command)

            _runcommand(command)
        except OSError as e:
            self.error("Could not launch process: %s" % e)
            return -1
        except subprocess.CalledProcessError as e:
            self.error("Could not launch process: %s" % e)
            return -1

        ret = None
        while self._check_result() is None:
            time.sleep(1)

        return self.validate_logs.retcode

def main(tool):
    loggable.init("GST_VALIDATE_ANDROID_DEBUG", True, False)

    android_validate = GstValidateAndroid()
    atexit.register(android_validate.clean)

    force_clean = False
    ret = -1
    try:
        ret = android_validate.run(tool)
    except Exception as e:
        ret = -1
        android_validate.force_clean = True
        raise

    return ret
