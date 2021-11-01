import os
import sys
import shutil
import subprocess as sub

curdir = os.path.dirname(__file__)
sys.path.append(curdir)
os.environ['PATH'] = "{}:{}".format(curdir, os.environ.get('PATH', ''))

def run_command(command):
    completed = sub.run(command, capture_output=True, text=True)
    return completed.returncode, completed.stdout, completed.stderr

def handler(event, context):
    command = ["program"] + event.get('args', [])

    print("$", " ".join(command))
    retcode, stdout, stderr = run_command(command)
    print("retcode={}".format(retcode))

    return {'retcode': retcode, 'stdout': stdout, 'stderr': stderr}
