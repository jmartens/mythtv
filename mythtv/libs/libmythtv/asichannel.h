/// -*- Mode: c++ -*-

#ifndef _ASI_CHANNEL_H_
#define _ASI_CHANNEL_H_

#include <vector>
using namespace std;

// Qt headers
#include <QString>

// MythTV headers
#include "dtvchannel.h"

class ASIChannel;
class ASIStreamHandler;
class ProgramMapTable;

class ASIChannel : public DTVChannel
{
    friend class ASISignalMonitor;
    friend class ASIRecorder;

  public:
    ASIChannel(TVRec *parent, const QString &device);
    ~ASIChannel(void);

    bool Open(void) { return InitializeInputs(); }
    void Close(void) {}

    // Sets
    bool SetChannelByString(const QString &chan)
        { m_curchannelname = chan; return true; }

    // Gets
    bool IsOpen(void) const { return true; }
    QString GetDevice(void) const { return m_device; }
    virtual vector<DTVTunerType> GetTunerTypes(void) const
    { return m_tuner_types; }

    bool TuneMultiplex(uint, QString) { return true; }
    bool Tune(const DTVMultiplex&, QString) { return true; }

  private:
    vector<DTVTunerType>  m_tuner_types;
    QString               m_device;
};

#endif // _ASI_CHANNEL_H_
