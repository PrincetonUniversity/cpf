import json
import sys
import os
import subprocess
import shutil
from termcolor import colored
import datetime
import time
import re

def set_SLAMP_enviorn(modules=None, extra_flags=None):
  module_list = ["DISTANCE", "CONSTANT_VALUE", "CONSTANT_ADDRESS", "LINEAR_VALUE", "LINEAR_ADDRESS", "TRACE"]
  extra_flag_list = ["slamp-target-fn=", "slamp-target-loop=", "slamp-target-inst="]

  SLAMP_env = os.environ.copy()
  if modules:
    for module in module_list:
      if module in modules:
        SLAMP_env[module+"_MODULE"] = "1"
      else:
        SLAMP_env[module+"_MODULE"] = "0"

  extra_flag_env = "" 
  if extra_flags:
    for flag in extra_flag_list:
      for eflag in extra_flags:
        if(re.match(flag, eflag)):
          extra_flag_env += "-"+eflag+" "

  SLAMP_env["EXTRA_FLAGS"] = extra_flag_env
  return SLAMP_env


def run_SLAMP(root_path, bmark, modules=None, extra_flags=None):
    print("Generating %s on %s " % ("SLAMP", bmark))

    os.chdir(os.path.join(root_path, bmark, "src"))

    SLAMP_env = set_SLAMP_enviorn(modules, extra_flags)
    start_time = time.time()
    with open("SLAMP.log", "w") as fd:
      make_process = subprocess.Popen(["make", "benchmark.result.slamp.profile"],
                                      stdout=fd,
                                      stderr=fd, env=SLAMP_env)

    if make_process.wait() != 0:
        elapsed = time.time() - start_time
        print(colored("%s failed for %s , took %.4fs" % ("SLAMP", bmark, elapsed), 'red'))
        return False
    else:
        elapsed = time.time() - start_time
        print(colored("%s succeeded for %s, took %.4fs" % ("SLAMP", bmark, elapsed), 'green'))
        return True

def parse_SLAMP_output(root_path, bmark, result_path, modules):
  source = os.path.join(root_path, bmark, "src")
  outfiles = ["benchmark.result.slamp.profile", "slamp_access_module.json"]
  is_trace = False
  is_distance = False
  if modules:
    if("TRACE" in modules):
      outfiles += ["trace.txt"]
      is_trace = True
    if("DISTANCE" in modules):
      is_distance = True

  for outfile in outfiles:
    if(os.path.exists(os.path.join(source, outfile))):
      shutil.move(os.path.join(source, outfile), os.path.join(result_path, outfile))

  os.chdir(result_path)

  with open("benchmark.result.slamp.profile") as fr:
    depinfo = {}
    keys = ["loopID", "src", "dst", "baredst", "iscross", "count"]
    if(is_distance):
      keys+=["distanceInfo"]
    count = 0
    for line in fr:
      temp = {}
      if(not re.search('\S+ 0 0 0 0', line)):
        instdep = list(line.split(None, 6))
        for i in range (6):
          temp[keys[i]]=instdep[i]
        if(is_distance):
          distancelist = list(instdep[6].strip("[],i \n").split(", "))
          distancedic = {}
          for i in range (len(distancelist)):
            distance = list(distancelist[i].strip("()").split(None, 2))
            distancedic[distance[0]] = distance[1]
          temp[keys[6]]=distancedic
        depinfo[str(count)]=temp
        count += 1

    fw = open("loopDepInfo.json", "w")
    json.dump(depinfo, fw, indent=4)
    fw.close()


  #parse trace.txt
  if(is_trace):
    trace = {}
    keys = ["loadCount", "storeCount", "depCount", "mallocCount", "freeCount", "invocationCount", "iterationCount"]
    with open("trace.txt") as fr:
      count = 0
      for line in fr:
        data = list(line.strip().split(None, 7))
        loopno = 'loop'+str(count)
        dic = {}
        for i in range(len(keys)):
          dic[keys[i]]=data[i]
        trace[loopno]=dic
        count += 1

    fw = open("trace.json", "w")
    json.dump(trace, fw, indent=4)
    fw.close()



