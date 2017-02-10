/*
 * Copyright (C) 2017 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <errno.h>
#include <fcntl.h>
#include <sched.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <vector>

static const char kNetNsDir[] = "/var/run/netns";

class FileDescriptor {
public:
    explicit FileDescriptor(int fd) : mFd(fd) { }
    ~FileDescriptor() {
        if (mFd != -1) {
            close(mFd);
            mFd = -1;
        }
    }
    int get() const { return mFd; }
private:
    int mFd;
};

static void printUsage(const char* program) {
    fprintf(stderr, "%s <namespace> <program> [options...]\n", program);
}

static bool setNetworkNamespace(const char* ns) {
    // There is a file in the net namespace dir (usually /var/run/netns) with
    // the same name as the namespace. This file is bound to /proc/<pid>/net by
    // the 'ip' command when the namespace is created. This allows us to access
    // the file of a process running in that network namespace without knowing
    // its pid, knowing the namespace name is enough.
    //
    // We are going to call setns which requires a file descriptor to that proc
    // file in /proc/<pid>/net. The process has to already be running in that
    // namespace. Since the file in the net namespace dir has been bound to
    // such a file already we just have to open /var/run/netns/<namespace> and
    // we have the required file descriptor.
    char nsPath[PATH_MAX];
    snprintf(nsPath, sizeof(nsPath), "%s/%s", kNetNsDir, ns);

    FileDescriptor nsFd(open(nsPath, O_RDONLY | O_CLOEXEC));
    if (nsFd.get() == -1) {
        fprintf(stderr, "Cannot open network namespace '%s' at '%s': %s\n",
                ns, nsPath, strerror(errno));
        return false;
    }

    if (setns(nsFd.get(), CLONE_NEWNET) == -1) {
        fprintf(stderr, "Cannot set network namespace '%s': %s\n",
                ns, strerror(errno));
        return false;
    }
    return true;
}

/**
 * Execute a given |command| with |argc| number of parameters that are located
 * in |argv|. The first parameter in |argv| is the command that should be run
 * followed by its arguments.
 */
static int execCommand( int argc, char** argv) {
    if (argc <= 0 || argv == nullptr || argv[0] == nullptr) {
        fprintf(stderr, "No command specified\n");
        return 1;
    }

    std::vector<char*> arguments;
    // Place all the arguments in the vector and the terminating null
    arguments.insert(arguments.begin(), argv, argv + argc);
    arguments.push_back(nullptr);

    if (execvp(argv[0], arguments.data()) == -1) {
        fprintf(stderr, "Could not execute command '%s", argv[0]);
        for (int i = 1; i < argc; ++i) {
            // Be nice to the user and print quotes if there are spaces to
            // indicate how we saw it. If there are already single quotes in
            // there confusion will ensue.
            if (strchr(argv[1], ' ')) {
                fprintf(stderr, " \"%s\"", argv[i]);
            } else {
                fprintf(stderr, " %s", argv[i]);
            }
        }
        fprintf(stderr, "': %s\n", strerror(errno));
        return errno;
    }
    // execvp never returns unless it fails so this is just to return something.
    return 0;
}

/**
 * Enter a given network namespace argv[1] and execute command argv[2] with
 * options argv[3..argc-1] in that namespace.
 */
int main(int argc, char* argv[]) {
    if (argc < 3) {
        printUsage(argv[0]);
        return 1;
    }

    // First set the new network namespace for this process
    if (!setNetworkNamespace(argv[1])) {
        return 1;
    }

    // Now run the command with all the remaining parameters
    return execCommand(argc - 2, &argv[2]);
}

