// -*- Mode: c++ -*-

#ifndef _ASISTREAMHANDLER_H_
#define _ASISTREAMHANDLER_H_

#include <vector>
using namespace std;

#include <QMutex>
#include <QMap>

#include "streamhandler.h"
#include "util.h"

class QString;
class ASIStreamHandler;
class DTVSignalMonitor;
class ASIChannel;
class DeviceReadBuffer;

typedef QMap<uint,int> FilterMap;

//#define RETUNE_TIMEOUT 5000

// Note : This class always uses a DRB && a TS reader.

class ASIStreamHandler : public StreamHandler
{
  public:
    static ASIStreamHandler *Get(const QString &devicename);
    static void Return(ASIStreamHandler * & ref);

    virtual void AddListener(MPEGStreamData *data,
                             bool allow_section_reader = false,
                             bool needs_drb            = false)
    {
        StreamHandler::AddListener(data, false, true);
    } // StreamHandler

  private:
    ASIStreamHandler(const QString &);
    ~ASIStreamHandler();

    bool Open(void);
    void Close(void);

    virtual void run(void); // QThread

    virtual void PriorityEvent(int fd); // DeviceReaderCB

  private:
    int                                     _device_num;
    int                                     _buf_size;
    int                                     _fd;

    // for implementing Get & Return
    static QMutex                           _handlers_lock;
    static QMap<QString, ASIStreamHandler*> _handlers;
    static QMap<QString, uint>              _handlers_refcnt;
};

#endif // _ASISTREAMHANDLER_H_
