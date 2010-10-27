/** -*- Mode: c++ -*-
 *  Class ASIChannel
 */

// MythTV includes
#include "mythverbose.h"
#include "mpegtables.h"
#include "asichannel.h"

#define LOC     QString("ASIChan(%1): ").arg(GetDevice())
#define LOC_ERR QString("ASIChan(%1), Error: ").arg(GetDevice())

ASIChannel::ASIChannel(TVRec *parent, const QString &device) :
    DTVChannel(parent), m_device(device), m_isopen(false),
    m_pat(NULL), m_pmt(NULL)
{
    m_tuner_types.push_back(DTVTunerType::kTunerTypeASI);
}

ASIChannel::~ASIChannel(void)
{
    if (IsOpen())
        Close();

    if (m_pat)
    {
        delete m_pat;
        m_pat = NULL;
    }

    if (m_pmt)
    {
        delete m_pmt;
        m_pmt = NULL;
    }
}

bool ASIChannel::Open(void)
{
    VERBOSE(VB_CHANNEL, LOC + "Open()");

    if (m_device.isEmpty())
        return false;

    if (m_isopen)
        return true;

    if (!InitializeInputs())
        return false;

    if (m_inputs.find(m_currentInputID) == m_inputs.end())
        return false;

    m_isopen = true;

    return true;
}

void ASIChannel::Close()
{
    VERBOSE(VB_CHANNEL, LOC + "Close()");
    m_isopen = false;
}

bool ASIChannel::SetChannelByString(const QString &channum)
{
    QString tmp     = QString("SetChannelByString(%1): ").arg(channum);
    QString loc     = LOC + tmp;
    QString loc_err = LOC_ERR + tmp;

    VERBOSE(VB_CHANNEL, loc);

    ClearDTVInfo();

    InputMap::const_iterator it = m_inputs.find(m_currentInputID);
    if (it == m_inputs.end())
        return false;

    uint mplexid_restriction;
    if (!IsInputAvailable(m_currentInputID, mplexid_restriction))
    {
        VERBOSE(VB_IMPORTANT, loc_err + "Input is not available");
        return false;
    }

    // Get the current channel data
    QString tvformat, modulation, freqtable, freqid, si_std;
    int finetune;
    uint64_t frequency;
    int mpeg_prog_num;
    uint atsc_major, atsc_minor, mplexid, tsid, netid;

    if (!ChannelUtil::GetChannelData(
        (*it)->sourceid, channum,
        tvformat, modulation, freqtable, freqid,
        finetune, frequency,
        si_std, mpeg_prog_num, atsc_major, atsc_minor, tsid, netid,
        mplexid, m_commfree))
    {
        VERBOSE(VB_IMPORTANT, loc_err +
                "Unable to find channel in database.");

        return false;
    }

    if (mplexid_restriction && (mplexid != mplexid_restriction))
    {
        VERBOSE(VB_IMPORTANT, loc_err + "Multiplex is not available");
        return false;
    }

    // Change the channel, no-op unless there is an external channel
    // changer since this is baseband.

    bool ok = (!(*it)->externalChanger.isEmpty()) ?
        ChangeExternalChannel(freqid) : true;
    if (!ok)
    {
        VERBOSE(VB_IMPORTANT, loc_err + "External channel changer failed");
        return false;
    }

    // Clear out any old PAT or PMT & save version info
    uint version = 0;
    if (m_pat)
    {
        VERBOSE(VB_IMPORTANT, loc + "Deleting m_pat");
        version = (m_pat->Version()+1) & 0x1f;
        delete m_pat; m_pat = NULL;
        delete m_pmt; m_pmt = NULL;
    }

    if (atsc_minor || (mpeg_prog_num>=0))
    {
        VERBOSE(VB_IMPORTANT, loc + "atsc_minor or mpeg_prog_num are set");
        SetSIStandard(si_std);
        SetDTVInfo(atsc_major, atsc_minor, netid, tsid, mpeg_prog_num);
    }
    else
    {
        // We need to pull the pid_cache since there are no tuning tables
        pid_cache_t pid_cache;
        int chanid = ChannelUtil::GetChanID((*it)->sourceid, channum);
        ChannelUtil::GetCachedPids(chanid, pid_cache);
        if (pid_cache.empty())
        {
            VERBOSE(VB_IMPORTANT, loc_err + "PID cache is empty");
            return false;
        }

        VERBOSE(VB_IMPORTANT, loc + "Creating m_pat");

        // Now we construct the PAT & PMT
        vector<uint> pnum; pnum.push_back(1);
        vector<uint> pid;  pid.push_back(9999);
        m_pat = ProgramAssociationTable::Create(0,version,pnum,pid);

        int pcrpid = -1;
        vector<uint> pids;
        vector<uint> types;
        pid_cache_t::iterator pit = pid_cache.begin(); 
        for (; pit != pid_cache.end(); pit++)
        {
            if (!pit->GetStreamID())
                continue;
            pids.push_back(pit->GetPID());
            types.push_back(pit->GetStreamID());
            if (pit->IsPCRPID())
                pcrpid = pit->GetPID();
            if ((pcrpid < 0) && StreamID::IsVideo(pit->GetStreamID()))
                pcrpid = pit->GetPID();
        }
        if (pcrpid < 0)
            pcrpid = pid_cache[0].GetPID();

        m_pmt = ProgramMapTable::Create(
            pnum.back(), pid.back(), pcrpid, version, pids, types);

        SetSIStandard("mpeg");
        SetDTVInfo(0,0,0,0,-1);
    }

    m_curchannelname = channum;
    m_inputs[m_currentInputID]->startChanNum = channum;

    return true;
}
