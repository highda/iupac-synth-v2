#!/usr/bin/env python3
"""Verify a staged onedir helper through source and frozen production protocols."""
import argparse, hashlib, json, os, shutil, stat, subprocess, tempfile, time
from pathlib import Path

REQUESTS = [
    {"protocolVersion": 1, "requestId": "smiles", "mode": "smiles", "text": "CCN(CC)C(=O)c1ccc(Cl)cc1"},
    {"protocolVersion": 1, "requestId": "name", "mode": "name", "text": "2,2,2-trifluoroethan-1-ol"},
    {"protocolVersion": 1, "requestId": "discovery", "action": "discover", "query": "gasotransmitter", "prefix": False, "limit": 8},
]
BLOCKED_ENV = {"PYTHONHOME":"/no/python", "PYTHONPATH":"/no/modules", "JAVA_HOME":"/no/java", "CLASSPATH":"/no/jar", "LD_LIBRARY_PATH":"/no/libs", "JAVA_TOOL_OPTIONS":"-Duser.language=xx"}
# The dynamic loader a DAW can poison is platform specific; poison the one the
# payload actually runs under so the frozen closure is proven, not assumed (#42).
if os.uname().sysname == "Darwin":
    # DYLD_INSERT_LIBRARIES is deliberately absent: dyld itself aborts any process
    # when an inserted dylib is missing, so poisoning it here would test dyld, not
    # the payload. Its real mitigation is the DAW-inherited environment sanitising
    # in src/chemistry/Extension.cpp and chemistry/helper.py _child_environment,
    # which strip it before the helper and OPSIN are spawned.
    BLOCKED_ENV.update({"DYLD_LIBRARY_PATH":"/no/libs", "DYLD_FALLBACK_LIBRARY_PATH":"/no/fallback",
                        "DYLD_FRAMEWORK_PATH":"/no/frameworks", "DYLD_VERSIONED_LIBRARY_PATH":"/no/versioned"})

# Harness bound for a freshly copied payload. It is not a product deadline: the
# in-place payload keeps the 45 s production deadline below, while a relocated
# copy on macOS additionally pays first-launch code-signature validation of every
# newly written Mach-O, and the deliberate OPSIN-deadline case spends 10 s of its
# own inside the helper before the diagnostic is emitted.
COPY_TIMEOUT = 90

def invoke(command, request, env, timeout=45):
    started=time.monotonic()
    run=subprocess.run(command,input=json.dumps(request),text=True,capture_output=True,env=env,timeout=timeout)
    elapsed=time.monotonic()-started
    if run.returncode != 0: raise RuntimeError(f"helper failed {run.returncode}: {run.stdout} {run.stderr}")
    return json.loads(run.stdout), elapsed

def sha(path):
    h=hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda:stream.read(1024*1024),b""): h.update(block)
    return h.hexdigest()

def native_dependencies(path):
    command=["otool","-L",str(path)] if os.uname().sysname=="Darwin" else ["ldd",str(path)]
    run=subprocess.run(command,text=True,capture_output=True,check=True)
    return sorted(line.strip() for line in run.stdout.splitlines() if line.strip() and not line.strip().endswith(":"))

def main():
    p=argparse.ArgumentParser(); p.add_argument("--source",type=Path,required=True); p.add_argument("--payload",type=Path,required=True); p.add_argument("--report",type=Path,required=True); a=p.parse_args()
    payload=a.payload.resolve(); exe=payload/"helper/iupac-analysis-helper"
    required=[exe,payload/"java/bin/java",payload/"resources/opsin-cli-2.8.0.jar",payload/"resources/discovery/discovery-v2.sqlite3"]
    if any(not x.is_file() for x in required): raise RuntimeError("payload is incomplete")
    for path in payload.rglob("*"):
        if path.is_symlink() and payload not in path.resolve().parents: raise RuntimeError(f"escaping symlink: {path}")
    source_env=dict(os.environ,IUPAC_OPSIN_JAR=str(a.source/"third_party/opsin/opsin-cli-2.8.0.jar"))
    frozen_env=dict(os.environ); frozen_env.update(BLOCKED_ENV); frozen_env["PATH"]="/no/system/runtime"
    timings=[]
    for request in REQUESTS:
        expected,_=invoke([os.sys.executable,str(a.source/"chemistry/helper.py")],request,source_env)
        actual,elapsed=invoke([str(exe)],request,frozen_env)
        if actual != expected: raise RuntimeError(f"source/frozen mismatch for {request['requestId']}")
        timings.append({"requestId":request["requestId"],"seconds":elapsed})
    bad=subprocess.run([str(exe)],input="{}",text=True,capture_output=True,env=frozen_env,timeout=15)
    if bad.returncode != 2 or json.loads(bad.stdout).get("status") != "error": raise RuntimeError("damaged request handling failed")
    # The frozen helper must stay silent on stderr. src/chemistry/Extension.cpp appends helper
    # stderr to a failed request's diagnostic, so any loader chatter -- for instance RDKit's
    # Boost.Python modules probing for the numpy the payload deliberately does not ship (#97) --
    # would reach the user as a fake "damaged install" message.
    quiet=subprocess.run([str(exe)],input=json.dumps(REQUESTS[0]),text=True,capture_output=True,env=frozen_env,timeout=45)
    if quiet.returncode != 0 or quiet.stderr != "":
        raise RuntimeError(f"frozen helper wrote to stderr on a successful request: {quiet.stderr[:512]!r}")
    # The pre-warm action the plugin sends when an editor opens (#97) must answer from the
    # frozen payload, best-effort per stage, and stay just as silent.
    warm_request={"protocolVersion":1,"requestId":"warm","action":"warm"}
    warm=subprocess.run([str(exe)],input=json.dumps(warm_request),text=True,capture_output=True,env=frozen_env,timeout=COPY_TIMEOUT)
    warm_response=json.loads(warm.stdout) if warm.stdout else {}
    if warm.returncode != 0 or warm.stderr != "" or warm_response.get("status") != "ok" \
            or warm_response.get("warm",{}).get("discovery") != "ok" or warm_response.get("warm",{}).get("opsin") != "ok":
        raise RuntimeError(f"frozen pre-warm failed: {warm.stdout[:512]} {warm.stderr[:512]}")
    with tempfile.TemporaryDirectory(prefix="iupac payload with spaces ") as temp:
        moved=Path(temp)/"read only payload"; shutil.copytree(payload,moved,symlinks=True)
        for path in moved.rglob("*"):
            path.chmod(0o555 if path.is_dir() or os.access(path,os.X_OK) else 0o444)
        actual,_=invoke([str(moved/"helper/iupac-analysis-helper")],REQUESTS[0],frozen_env,COPY_TIMEOUT)
        if actual["status"] != "ok": raise RuntimeError("read-only relocation failed")
    with tempfile.TemporaryDirectory(prefix="iupac damaged payload ") as temp:
        damaged=Path(temp)/"payload"; shutil.copytree(payload,damaged,symlinks=True)
        (damaged/"resources/opsin-cli-2.8.0.jar").rename(damaged/"resources/opsin.damaged")
        run=subprocess.run([str(damaged/"helper/iupac-analysis-helper")],input=json.dumps(REQUESTS[1]),text=True,capture_output=True,env=frozen_env,timeout=COPY_TIMEOUT)
        if run.returncode != 2 or "artifact is missing" not in json.loads(run.stdout).get("diagnostic",""): raise RuntimeError("damaged payload was not diagnosed")
    with tempfile.TemporaryDirectory(prefix="iupac deadline payload ") as temp:
        deadline=Path(temp)/"payload"; shutil.copytree(payload,deadline,symlinks=True)
        java=deadline/"java/bin/java"; real_java=java.with_name("java.real"); java.rename(real_java)
        java.write_text("#!/bin/sh\n/bin/sleep 60 &\necho $! > \"$IUPAC_TEST_CHILD_PID\"\nwait\n"); java.chmod(0o755)
        pidfile=Path(temp)/"child.pid"; timeout_env=dict(frozen_env,IUPAC_TEST_CHILD_PID=str(pidfile))
        run=subprocess.run([str(deadline/"helper/iupac-analysis-helper")],input=json.dumps(REQUESTS[1]),text=True,capture_output=True,env=timeout_env,timeout=COPY_TIMEOUT)
        if run.returncode != 2 or "deadline" not in json.loads(run.stdout).get("diagnostic",""): raise RuntimeError("deadline was not diagnosed")
        child=int(pidfile.read_text()); time.sleep(.1)
        try: os.kill(child,0)
        except ProcessLookupError: pass
        else: raise RuntimeError("deadline left an OPSIN descendant running")
    modules=subprocess.check_output([str(payload/"java/bin/java"),"--list-modules"],text=True).splitlines()
    files=[x for x in payload.rglob("*") if x.is_file()]
    report={"schemaVersion":1,"architecture":os.uname().machine,"files":len(files),"unpackedBytes":sum(x.stat().st_size for x in files),"startup":timings,"javaModules":modules,"nativeDependencies":{"helper":native_dependencies(exe),"java":native_dependencies(payload/"java/bin/java")},"artifacts":{str(x.relative_to(payload)):sha(x) for x in required[1:]},"negativeIsolation":{"path":frozen_env["PATH"],"poisonedEnvironment":sorted(BLOCKED_ENV),"sourceTreeRequired":False,"damagedPayload":"diagnosed","deadlineTreeCleanup":"passed","silentStderr":"passed","prewarm":"passed"},"sqliteVersion":subprocess.check_output([os.sys.executable,"-c","import sqlite3;print(sqlite3.sqlite_version)"],text=True).strip()}
    a.report.write_text(json.dumps(report,indent=2,sort_keys=True)+"\n")
if __name__=="__main__": main()
