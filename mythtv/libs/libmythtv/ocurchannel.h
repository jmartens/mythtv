/// -*- Mode: c++ -*-

#ifndef _OCUR_CHANNEL_H_
#define _OCUR_CHANNEL_H_

#include <vector>
using namespace std;

// Qt headers
#include <QString>

// MythTV headers
#include "dtvchannel.h"

class OCURChannel;
class OCURStreamHandler;
class ProgramMapTable;

class OCURChannel : public DTVChannel
{
    friend class OCURSignalMonitor;
    friend class OCURRecorder;

  public:
    OCURChannel(TVRec *parent, const QString &device);
    ~OCURChannel(void);

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

#endif // _OCUR_CHANNEL_H_
