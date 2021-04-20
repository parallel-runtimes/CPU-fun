import subprocess
import datetime
import platform
import os

hostName = platform.node().split(".")[0]


def capture(cmd):
    try:
        print("*** ", cmd)
        res = subprocess.run(cmd, capture_output=True, shell=True)
        return (res.stdout.decode("utf-8"), res.stderr.decode("utf-8"))
    except:
        print("Command execution  failed")
        return ("", "")


commands = (
    (
        "./omp_scan",
        {
            "threads": (1,2,4,8),
            "args": ("parallel", "parallelRed", "parallelQ", "taskCritical", "taskTR"),
        },
    ),
    ("./omp_scan", {"threads": (1,), "args": ("serial",),}),
    ("grep", {"threads": (1,), "args": ("-c",)}),
)


searchRe = "[aA].*[eE].*[iI].*[oO].*[uU]"
repeats = 10

def runOnce(image, arg, threads):
    command = (
        "OMP_NUM_THREADS="
        + str(threads)
        + " /usr/bin/time "
        + image
        + " "
        + arg
        + ' "'
        + searchRe
        + '" < large.txt'
    )
    (out, err) = capture(command)
    return err.strip()


# Functions which may be useful elsewhere
def outputName(test):
    """Generate an output file name based on the test, hostname, date, and a sequence number"""
    dateString = datetime.date.today().isoformat()
    nameBase = test + "_" + hostName + "_" + dateString
    nameBase = nameBase.replace("__", "_")
    fname = nameBase + "_1.res"
    version = 1
    while os.path.exists(fname):
        version = version + 1
        fname = nameBase + "_" + str(version) + ".res"
    return fname


def run(testDesc):
    (image, ops) = testDesc
    threads = ops["threads"]
    args = ops["args"]
    for arg in args:
        res = {}
        for thread in threads:
            res[thread] = []
            for i in range(repeats):
                print("*** ", image, " ", arg, " thread ", thread)
                res[thread].append(runOnce(image, arg, thread))
        with open(outputName(image + "_" + arg), "w") as f:
            print("Scan time", file=f)
            if image == "grep":
                print(image, " ", arg, file=f)
            else:
                print(arg, file=f)
            print("Threads, Time, User Time, System Time", file=f)
            for t in res.keys():
                for times in res[t]:
                    (real, r, user, u, sys, s) = times.split()
                    print(
                        t,
                        ", ",
                        real.strip(),
                        "s, ",
                        user.strip(),
                        "s, ",
                        sys.strip(),
                        "s",
                        file=f,
                    )


def runAll():
    for runs in commands:
        run(runs)


runAll()
