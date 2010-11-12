
// Own header
#include "mythsystem.h"

// compat header
#include "compat.h"

// C++/C headers
#include <cerrno>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <time.h>
#include <signal.h>  // for kill()

// QT headers
#include <QCoreApplication>
#include <QThread>
#include <QMutex>
#include <QSemaphore>
#include <QMap>

# ifdef linux
#   include <sys/vfs.h>
#   include <sys/statvfs.h>
#   include <sys/sysinfo.h>
# else
#   ifdef __FreeBSD__
#     include <sys/mount.h>
#   endif
#   if CONFIG_CYGWIN
#     include <sys/statfs.h>
#   endif
#   ifndef _WIN32
#     include <sys/sysctl.h>
#   endif
# endif

// libmythdb headers
#include "mythcorecontext.h"
#include "mythevent.h"
#include "mythverbose.h"
#include "exitcodes.h"

#ifndef USING_MINGW
typedef struct {
    QMutex  mutex;
    uint    result;
    time_t  timeout;
    bool    background;
} PidData_t;

typedef QMap<pid_t, PidData_t *> PidMap_t;

class MythSystemReaper : public QThread
{
    public:
        void run(void);
        uint waitPid( pid_t pid, time_t timeout, bool background = false,
                      bool processEvents = true );
    private:
        PidMap_t    m_pidMap;
        QMutex      m_mapLock;
};

static class MythSystemReaper *reaper = NULL;

void MythSystemReaper::run(void)
{
    VERBOSE(VB_GENERAL, "Starting reaper thread");

    while (gCoreContext)
    {
        usleep(10000);
        if (!gCoreContext)
            break;

        // kill timed out pids
        m_mapLock.lock();
        if (m_pidMap.empty())
        {
            m_mapLock.unlock();
            continue;
        }

        PidMap_t::iterator it = m_pidMap.begin();
        time_t now = time(NULL);
        while (it != m_pidMap.end())
        {
            bool done = false;
            if ((*it)->timeout != 0 && (*it)->timeout <= now)
            {
                // Timed out
                (*it)->result = GENERIC_EXIT_TIMEOUT;
                VERBOSE(VB_GENERAL,
                        QString("Child PID %1 timed out, killing")
                        .arg(it.key()));
                kill(it.key(), SIGTERM);
                usleep(2500);
                done = true;
                int status;
                if (waitpid(it.key(), &status, WNOHANG) != it.key())
                {
                    kill(it.key(), SIGKILL);
                    done = waitpid(it.key(), &status, WNOHANG) == it.key();
                }
                (*it)->timeout = 0;
            }

            if (done)
            {
                if ((*it)->background)
                    delete *it;
                else
                    (*it)->mutex.unlock();
                it = m_pidMap.erase(it);
            }
            else
            {
                ++it;
            }
        }
        vector<pid_t> pids;
        pids.reserve(m_pidMap.size());
        for (it = m_pidMap.begin(); it != m_pidMap.end(); ++it)
        {
            if (it.key() > 0)
                pids.push_back(it.key());
        }
        m_mapLock.unlock();

        // reap our pids
        for (uint i = 0; i < pids.size(); i++)
        {
            int status;
            pid_t pid = waitpid(pids[i], &status, WNOHANG);
            if (pid < 0)
            {
                // The pid may have been removed from m_pidMap
                // while we were waiting on it, so make sure it
                // is there before we report an error.
                m_mapLock.lock();
                it = m_pidMap.find(pid);
                if (it != m_pidMap.end())
                {
                    VERBOSE(VB_GENERAL,
                            QString("waitpid(%1) failed").arg(pids[i]) + ENO);
                }
                m_mapLock.unlock();
            }
            if (pid <= 0)
                continue;

            if (pid != pids[i])
            {
                VERBOSE(VB_IMPORTANT,
                        QString("waitpid(%1) -> %2 "
                                "Returned PID does not match requested PID")
                        .arg(pids[i]).arg(pid));
                continue;
            }

            if (WIFEXITED(status) || WIFSIGNALED(status))
            {
                m_mapLock.lock();
                it = m_pidMap.find(pid);
                if (it == m_pidMap.end())
                {
                    VERBOSE(VB_GENERAL,
                            QString("PID %1: not found in map").arg(pid));
                    m_mapLock.unlock();
                    continue;
                }
                PidData_t *pidData = *it;
                it = m_pidMap.erase(it);
                m_mapLock.unlock();

                if (WIFEXITED(status))
                {
                    pidData->result = WEXITSTATUS(status);
                    VERBOSE(VB_GENERAL,
                            QString("PID %1: exited: status=%2, result=%3")
                            .arg(pid).arg(status).arg(pidData->result));
                }
                else if (WIFSIGNALED(status))
                {
                    pidData->result = GENERIC_EXIT_SIGNALLED;
                    VERBOSE(VB_GENERAL, QString("PID %1: signal: status=%2, "
                                                "result=%3, signal=%4")
                            .arg(pid).arg(status).arg(pidData->result) 
                            .arg(WTERMSIG(status)));
                }

                if (pidData->background)
                    delete pidData;
                else
                    pidData->mutex.unlock();
            }
            else if (WIFSTOPPED(status))
            {
                VERBOSE(VB_GENERAL, QString("PID %1: Got a stop signal %2")
                        .arg(pid).arg(WSTOPSIG(status)));
            }
            else if (WIFCONTINUED(status))
            {
                VERBOSE(VB_GENERAL, QString("PID %1: Got a continue signal")
                        .arg(pid));
            }
        }
    }
}

uint MythSystemReaper::waitPid( pid_t pid, time_t timeout, bool background,
                                bool processEvents )
{
    PidData_t  *pidData = new PidData_t;
    uint        result;

    if( timeout > 0 )
        pidData->timeout = time(NULL) + timeout;
    else
        pidData->timeout = 0;

    pidData->background = background;

    pidData->mutex.lock();
    m_mapLock.lock();
    m_pidMap.insert( pid, pidData );
    m_mapLock.unlock();

    if( !background ) {
        /* Now we wait for the thread to see the SIGCHLD */
        while( !pidData->mutex.tryLock(100) )
            if (processEvents)
                qApp->processEvents();

        result = pidData->result;
        delete pidData;

        return( result );
    }

    /* We are running in the background, the reaper will still catch the 
       child */
    return( GENERIC_EXIT_OK );
}
#endif



/** \fn myth_system(const QString&, uint, uint)
 *  \brief Runs a system command inside the /bin/sh shell.
 *
 *  Note: Returns GENERIC_EXIT_NOT_OK if it cannot execute the command.
 *  \return Exit value from command as an unsigned int in range [0,255].
 */
uint myth_system(const QString &command, uint flags, uint timeout)
{
    uint            result;

    if( !(flags & kMSRunBackground) && command.endsWith("&") )
    {
        VERBOSE(VB_GENERAL, "Adding background flag");
        flags |= kMSRunBackground;
    }

    myth_system_pre_flags( flags );

#ifndef USING_MINGW
    pid_t   pid;

    pid    = myth_system_fork( command, result );

    if( result == GENERIC_EXIT_RUNNING )
        result = myth_system_wait( pid, timeout, (flags & kMSRunBackground),
                                   (flags & kMSProcessEvents) );
#else
    STARTUPINFO si;
    PROCESS_INFORMATION pi;
    QString LOC_ERR = QString("myth_system('%1'): Error: ").arg(command);

    memset(&si, 0, sizeof(si));
    memset(&pi, 0, sizeof(pi));
    si.cb = sizeof(si);
    QString cmd = QString("cmd.exe /c %1").arg(command);
    if (!::CreateProcessA(NULL, cmd.toUtf8().data(), NULL, NULL,
                          FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi))
    {
        VERBOSE(VB_GENERAL, (LOC_ERR + "CreateProcess() failed because %1")
                .arg(::GetLastError()));
        result = MYTHSYSTEM__EXIT__EXECL_ERROR;
    }
    else
    {
        if (::WaitForSingleObject(pi.hProcess, INFINITE) == WAIT_FAILED)
            VERBOSE(VB_GENERAL,
                    (LOC_ERR + "WaitForSingleObject() failed because %1")
                    .arg(::GetLastError()));
        DWORD exitcode = GENERIC_EXIT_OK;
        if (!GetExitCodeProcess(pi.hProcess, &exitcode))
            VERBOSE(VB_GENERAL, (LOC_ERR + 
                    "GetExitCodeProcess() failed because %1")
                    .arg(::GetLastError()));
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        result = exitcode;
    }
#endif

    myth_system_post_flags( flags );

    return result;
}


void myth_system_pre_flags(uint &flags)
{
    bool isInUi = gCoreContext->HasGUI() && gCoreContext->IsUIThread();
    if( isInUi )
    {
        flags |= kMSInUi;
    }
    else
    {
        // These locks only happen in the UI, and only if the flags are cleared
        // so as we are not in the UI, set the flags to simplify logic further
        // down
        flags &= ~kMSInUi;
        flags |= kMSDontBlockInputDevs;
        flags |= kMSDontDisableDrawing;
        flags &= ~kMSProcessEvents;
    }

    if (flags & kMSRunBackground )
        flags &= ~kMSProcessEvents;

    // This needs to be a send event so that the MythUI locks the input devices
    // immediately instead of after existing events are processed
    // since this function could be called inside one of those events.
    if( !(flags & kMSDontBlockInputDevs) )
    {
        QEvent event(MythEvent::kLockInputDevicesEventType);
        QCoreApplication::sendEvent(gCoreContext->GetGUIObject(), &event);
    }

    // This needs to be a send event so that the MythUI m_drawState change is
    // flagged immediately instead of after existing events are processed
    // since this function could be called inside one of those events.
    if( !(flags & kMSDontDisableDrawing) )
    {
        QEvent event(MythEvent::kPushDisableDrawingEventType);
        QCoreApplication::sendEvent(gCoreContext->GetGUIObject(), &event);
    }
}

void myth_system_post_flags(uint &flags)
{
    // This needs to be a send event so that the MythUI m_drawState change is
    // flagged immediately instead of after existing events are processed
    // since this function could be called inside one of those events.
    if( !(flags & kMSDontDisableDrawing) )
    {
        QEvent event(MythEvent::kPopDisableDrawingEventType);
        QCoreApplication::sendEvent(gCoreContext->GetGUIObject(), &event);
    }

    // This needs to be a post event so that the MythUI unlocks input devices
    // after all existing (blocked) events are processed and ignored.
    if( !(flags & kMSDontBlockInputDevs) )
    {
        QEvent *event = new QEvent(MythEvent::kUnlockInputDevicesEventType);
        QCoreApplication::postEvent(gCoreContext->GetGUIObject(), event);
    }
}


#ifndef USING_MINGW
pid_t myth_system_fork(const QString &command, uint &result)
{
    QString LOC_ERR = QString("myth_system('%1'): Error: ").arg(command);
    VERBOSE(VB_GENERAL, QString("Launching: %1") .arg(command));

    pid_t child = fork();

    if (child < 0)
    {
        /* Fork failed, still in parent */
        VERBOSE(VB_GENERAL, (LOC_ERR + "fork() failed because %1")
                .arg(strerror(errno)));
        result = GENERIC_EXIT_NOT_OK;
        return -1;
    }
    else if (child == 0)
    {
        /* Child - NOTE: it is not safe to use VERBOSE between the fork and
         * execl calls in the child.  It causes occasional locking issues that
         * cause deadlocked child processes. */

        /* Close all open file descriptors except stdout/stderr */
        for (int i = sysconf(_SC_OPEN_MAX) - 1; i > 2; i--)
            close(i);

        /* Try to attach stdin to /dev/null */
        int fd = open("/dev/null", O_RDONLY);
        if (fd >= 0)
        {
            // Note: dup2() will close old stdin descriptor.
            if (dup2(fd, 0) < 0)
            {
                // Can't use VERBOSE due to locking fun.
		        QString message = LOC_ERR + 
                        "Cannot redirect /dev/null to standard input,"
                        "\n\t\t\tfailed to duplicate file descriptor." + ENO;
                cerr << message.constData() << endl;
            }
            close(fd);
        }
        else
        {
            // Can't use VERBOSE due to locking fun.
            QString message = LOC_ERR + "Cannot redirect /dev/null "
                    "to standard input, failed to open." + ENO;
            cerr << message.constData() << endl;
        }

        /* Run command */
        execl("/bin/sh", "sh", "-c", command.toUtf8().constData(), (char *)0);
        if (errno)
        {
            // Can't use VERBOSE due to locking fun.
            QString message = LOC_ERR + QString("execl() failed because %1")
                    .arg(strerror(errno));
            cerr << message.constData() << endl;
        }

        /* Failed to exec */
        _exit(MYTHSYSTEM__EXIT__EXECL_ERROR); // this exit is ok
    }

    /* Parent */
    result = GENERIC_EXIT_RUNNING;
    return child;
}

uint myth_system_wait(pid_t pid, uint timeout, bool background, 
                      bool processEvents)
{
    if( reaper == NULL )
    {
        reaper = new MythSystemReaper;
        reaper->start();
    }
    VERBOSE(VB_GENERAL, QString("PID %1: launched%2") .arg(pid)
        .arg(background ? " in the background, not waiting" : ""));
    return reaper->waitPid(pid, timeout, background, processEvents);
}
#endif


/*
 * vim:ts=4:sw=4:ai:et:si:sts=4
 */
