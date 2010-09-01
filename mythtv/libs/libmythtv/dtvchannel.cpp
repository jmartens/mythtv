// C++ headers
#include <algorithm>
using namespace std;

// MythTV headers
#include "mythdb.h"
#include "cardutil.h"
#include "dtvchannel.h"

#define LOC QString("DTVChan(%1): ").arg(GetDevice())
#define LOC_WARN QString("DTVChan(%1) Warning: ").arg(GetDevice())
#define LOC_ERR QString("DTVChan(%1) Error: ").arg(GetDevice())

QMutex                    DTVChannel::master_map_lock;
QMap<QString,DTVChannel*> DTVChannel::master_map;

DTVChannel::DTVChannel(TVRec *parent)
    : ChannelBase(parent),
      tunerType(DTVTunerType::kTunerTypeUnknown),
      sistandard("mpeg"),         tuningMode(QString::null),
      currentProgramNum(-1),
      currentATSCMajorChannel(0), currentATSCMinorChannel(0),
      currentTransportID(0),      currentOriginalNetworkID(0)
{
}

DTVChannel::~DTVChannel()
{
    QMutexLocker locker(&master_map_lock);
    QMap<QString,DTVChannel*>::iterator it = master_map.begin();
    for (; it != master_map.end(); ++it)
    {
        if (*it == this)
        {
            master_map.erase(it);
            break;
        }
    }
}

void DTVChannel::SetDTVInfo(uint atsc_major, uint atsc_minor,
                            uint dvb_orig_netid,
                            uint mpeg_tsid, int mpeg_pnum)
{
    QMutexLocker locker(&dtvinfo_lock);
    currentProgramNum        = mpeg_pnum;
    currentATSCMajorChannel  = atsc_major;
    currentATSCMinorChannel  = atsc_minor;
    currentTransportID       = mpeg_tsid;
    currentOriginalNetworkID = dvb_orig_netid;
}

QString DTVChannel::GetSIStandard(void) const
{
    QMutexLocker locker(&dtvinfo_lock);
    QString tmp = sistandard; tmp.detach();
    return tmp;
}

void DTVChannel::SetSIStandard(const QString &si_std)
{
    QMutexLocker locker(&dtvinfo_lock);
    sistandard = si_std.toLower();
    sistandard.detach();
}

QString DTVChannel::GetSuggestedTuningMode(bool is_live_tv) const
{
    uint cardid = GetCardID();
    QString input = GetCurrentInput();

    uint quickTuning = 0;
    if (cardid && !input.isEmpty())
        quickTuning = CardUtil::GetQuickTuning(cardid, input);

    bool useQuickTuning = (quickTuning && is_live_tv) || (quickTuning > 1);

    QMutexLocker locker(&dtvinfo_lock);
    if (!useQuickTuning && ((sistandard == "atsc") || (sistandard == "dvb")))
    {
        QString tmp = sistandard; tmp.detach();
        return tmp;
    }

    return "mpeg";
}

QString DTVChannel::GetTuningMode(void) const
{
    QMutexLocker locker(&dtvinfo_lock);
    QString tmp = tuningMode; tmp.detach();
    return tmp;
}

vector<DTVTunerType> DTVChannel::GetTunerTypes(void) const
{
    vector<DTVTunerType> tts;
    if (tunerType != DTVTunerType::kTunerTypeUnknown)
        tts.push_back(tunerType);
    return tts;
}

void DTVChannel::SetTuningMode(const QString &tuning_mode)
{
    QMutexLocker locker(&dtvinfo_lock);
    tuningMode = tuning_mode.toLower();
    tuningMode.detach();
}

/** \brief Returns cached MPEG PIDs for last tuned channel.
 *  \param pid_cache List of PIDs with their TableID
 *                   types is returned in pid_cache.
 */
void DTVChannel::GetCachedPids(pid_cache_t &pid_cache) const
{
    int chanid = GetChanID();
    if (chanid > 0)
        ChannelUtil::GetCachedPids(chanid, pid_cache);
}

/** \brief Saves MPEG PIDs to cache to database
 * \param pid_cache List of PIDs with their TableID types to be saved.
 */
void DTVChannel::SaveCachedPids(const pid_cache_t &pid_cache) const
{
    int chanid = GetChanID();
    if (chanid > 0)
        ChannelUtil::SaveCachedPids(chanid, pid_cache);
}

DTVChannel *DTVChannel::GetMaster(const QString &videodevice)
{
    QMutexLocker locker(&master_map_lock);

    QMap<QString,DTVChannel*>::iterator it = master_map.find(videodevice);
    if (it != master_map.end())
        return *it;

    QString tmp = videodevice; tmp.detach();
    master_map[tmp] = this;

    return this;
}

const DTVChannel *DTVChannel::GetMaster(const QString &videodevice) const
{
    QMutexLocker locker(&master_map_lock);

    QMap<QString,DTVChannel*>::iterator it = master_map.find(videodevice);
    if (it != master_map.end())
        return *it;

    QString tmp = videodevice; tmp.detach();
    master_map[tmp] = (DTVChannel*) this;

    return this;
}
