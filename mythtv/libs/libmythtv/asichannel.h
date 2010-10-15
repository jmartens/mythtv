/// -*- Mode: c++ -*-

#ifndef _ASI_CHANNEL_H_
#define _ASI_CHANNEL_H_

#include <vector>
using namespace std;

// Qt headers
#include <QString>

// MythTV headers
#include "dtvchannel.h"

class ProgramAssociationTable;
class ASIStreamHandler;
class ProgramMapTable;
class ASIChannel;

class ASIChannel : public DTVChannel
{
    friend class ASISignalMonitor;
    friend class ASIRecorder;

  public:
    ASIChannel(TVRec *parent, const QString &device);
    ~ASIChannel(void);

    bool Open(void);
    void Close(void);

    // Sets
    bool SetChannelByString(const QString &chan);

    // Gets
    bool IsOpen(void) const { return m_isopen; }
    QString GetDevice(void) const { return m_device; }
    virtual vector<DTVTunerType> GetTunerTypes(void) const
        { return m_tuner_types; }

    // Channel scanning stuff
    bool TuneMultiplex(uint, QString) { return true; }
    bool Tune(const DTVMultiplex&, QString) { return true; }

  private:
    vector<DTVTunerType>     m_tuner_types;
    QString                  m_device;
    bool                     m_isopen;
    ProgramAssociationTable *m_pat;
    ProgramMapTable         *m_pmt;
};

#endif // _ASI_CHANNEL_H_
