
/**
 * 1) starts sc for each config file in /etc/shoutcast
 * 2) monitors is an sc crashes, and restarts it
 * 3) writes to log about restarts
 */

#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <string>
#include <list>
#include <string.h>
#include <dirent.h>
#include <unistd.h>
#include <errno.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <fcntl.h>
#include <time.h>

#define SC_CONFIG   "/etc/scd"

#if 1
#define SC_EXEC     "/opt/shoutcast/sc_serv"
#define SC_PID      "/var/lib/scd/scd.pid"
#define SC_LOGDIR   "/var/lib/scd/"
#define SC_LOG      SC_LOGDIR"scd.log"
#else
#define SC_VAR      "./"
#define SC_EXEC     "./sc_test"
#define SC_PID      SC_VAR"sc_daemon.pid"
#define SC_LOG      SC_VAR"sc_daemon.log"
#endif


struct Child {
    pid_t pid;
    std::string config;
    std::string log;
    FILE* out;
};

typedef std::list<Child> Children;

bool g_running = true;
Children g_children;

std::string timeStr()
{
    time_t now = time(0);
    struct tm tstruct;
    char buf[80];
    tstruct = *localtime(&now);
    strftime(buf, sizeof(buf), "<%Y-%m-%d %X> : ", &tstruct);
    return buf;
}


std::string logPrefix()
{
    std::ostringstream oss;
    oss << timeStr();
    oss << "[" << std::setw(6) << getpid() << "] ";
    return oss.str();
}


void start()
{
    std::ofstream pidfile(SC_PID);
    if (!pidfile.good()) {
        std::cerr << "cannot open PID file " << SC_PID << ": " << strerror(errno) << std::endl;
        exit(errno);
    }
    pidfile << getpid() << std::endl;
    pidfile.close();

    std::ofstream log;

    log.open(SC_LOG, std::ofstream::app);
    log << "\n"
        << logPrefix() << "starting shoutcast daemon" << std::endl;
    log << logPrefix() << "reading shoutcast configuration files from " << SC_CONFIG << std::endl;
    log.close();

    // Enumerate config files in shoutcast config dir.
    DIR *dir;
    struct dirent *ent;
    dir = opendir (SC_CONFIG);
    if (dir == NULL) {
        log.open(SC_LOG, std::ofstream::app);
        log << logPrefix() << "cannot read scd configuration direcoty " << SC_CONFIG << ": " << strerror(errno) << std::endl;
        log.close();
        exit(errno);
    }

    for (; ent = readdir(dir); ent != NULL) {
        std::string cfgfile = ent->d_name;
        if (cfgfile == "." || cfgfile == "..") {
            continue;
        }
        std::string cfgpath = SC_CONFIG;
        cfgpath += "/";
        cfgpath += cfgfile;
        Child c;
        c.pid = 0;
        c.config = cfgpath;
        size_t idx = cfgfile.find(".");
        c.log = SC_LOGDIR"sc_stream_";
        c.log += cfgfile.substr(0, idx);
        c.log += ".log";
        g_children.push_back(c);

        log.open(SC_LOG, std::ofstream::app);
        log << logPrefix() << "found shoutcast configuration: " << cfgpath << std::endl;
        log.close();
    }
    closedir(dir);

    // Start respawn loop.
    while (g_running) {
        // Respawn all children with zero pid.
        for (Children::iterator i = g_children.begin();
                i != g_children.end(); ++i) {
            if (i->pid == 0) {
                i->pid = fork();
                if (i->pid == 0) {
                    // Open log file and redirect stdout, stderr.
                    FILE *f = fopen(i->log.c_str(), "a");
                    int r1 = dup2(fileno(f), 1);
                    int r2 = dup2(fileno(f), 2);
                    fclose(f);

                    // Start child process.
                    std::cout << timeStr()
                              << "[" << getpid() << "] started"
                              << std::endl;
                    if (execl(SC_EXEC, SC_EXEC, i->config.c_str(),
                            (char*)0) < 0) {
                        exit(1);
                    }
                } else {
                    log.open(SC_LOG, std::ofstream::app);
                    log << logPrefix() << "spawned server for config " << i->config << ", server pid is [" << i->pid << "]" << std::endl;
                    log.close();
                }
            }
        }

        // Wait for any failed children.
        int status;
        pid_t pid = wait(&status);
        if (!g_running)
            break;

        // Mark a failed child to restart.
        for (Children::iterator i = g_children.begin(); i != g_children.end(); ++i) {
            if (i->pid == pid) {
                log.open(SC_LOG, std::ofstream::app);
                log << logPrefix() << "server with pid [" << pid << "] (" << i->config << ") failed with status " << status << std::endl;
                log.close();
                i->pid = 0;
                break;
            }
        }
    }

    log.open(SC_LOG, std::ofstream::app);
    log << logPrefix() << "terminated" << "\n" << std::endl;
    log.close();
}


int oldPID()
{
    std::ifstream pidfile(SC_PID);
    if (pidfile.good()) {
        int oldpid;
        pidfile >> oldpid;
        return oldpid;
    }
    return -1;
}


int running()
{
    int old = oldPID();
    if (old == -1)
        return -1;
    int status = kill(old, 0);
    if (status == 0) {
        return old;
    } else {
        unlink(SC_PID);
        return -1;
    }
}


void sigHandler(int signum)
{
    g_running = false;

    std::ofstream log;

    log.open(SC_LOG, std::ofstream::app);
    log << logPrefix() << "SIG " << signum << " arrived, terminating" << std::endl;

    for (Children::iterator i = g_children.begin(); i != g_children.end(); ++i) {
        if (i->pid) {
            log << logPrefix() << "sending SIGKILL to server [" << i->pid << "] (" << i->config << ")" << std::endl;
            kill(i->pid, SIGKILL);
            int status;
            waitpid(i->pid, &status, 0);
            log << logPrefix() << "terminated server [" << i->pid << "]" << std::endl;
        }
    }

    unlink(SC_PID);

    log << logPrefix() << "exiting scd" << std::endl;
    log.close();
    exit(0);
}


void usage()
{
    std::cout << "Usage: scd start|stop|status|report" << std::endl;
}


int main(int argc, char *argv[])
{
    signal(SIGTERM, sigHandler);

    // Check log file
    std::ofstream log(SC_LOG, std::ofstream::app);
    if (!log.good()) {
        std::cerr << "cannot open log " << SC_LOG << ": " << strerror(errno) << std::endl;
        exit(errno);
    }
    log.close();

    if (argc != 2) {
        usage();
        return EINVAL;
    }

    if (strcmp(argv[1], "start") == 0) {
        int pid = running();
        if (pid > 0) {
            std::cerr << "already running: [" << pid << "]" << std::endl;
            return EEXIST;
        }

        if (fork() == 0) {
            start();
        }
    } else if (strcmp(argv[1], "stop") == 0) {
         int pid = running();
         if (pid == -1) {
             std::cerr << "not running" << std::endl;
             return ENOENT;
         }
         int res = kill(pid, SIGTERM);
         if (res != 0) {
            std::cerr << "failed to stop [" << pid << "]: " << errno << std::endl;
            return errno;
         }
    } else if (strcmp(argv[1], "status") == 0) {
        return running() > 0 ? 0 : 1;
    } else if (strcmp(argv[1], "report") == 0) {
        int pid = running();
        if (pid > 0) {
            std::cout << "running [" << pid << "]" << std::endl;
        } else {
            std::cout << "not running" << std::endl;
        }
    } else {
        usage();
        return 1;
    }

    return 0;
}

