// -*- Mode: c++ -*-
/*
 *  Copyright (C) Daniel Kristjansson 2010
 *
 *  Copyright notice is in asirecorder.cpp of the MythTV project.
 */

#ifndef _ASI_RECORDER_H_
#define _ASI_RECORDER_H_

// MythTV includes
#include "dtvrecorder.h"

class ASIStreamHandler;
class RecordingProfile;
class ASIChannel;
class QString;
class TVRec;

/** \class ASIRecorder
 *  \brief This is a specialization of DTVRecorder used to
 *         handle streams from ASI drivers.
 *
 *  \sa DTVRecorder
 */
class ASIRecorder : public DTVRecorder
{
  public:
    ASIRecorder(TVRec *rec, ASIChannel *channel);

    void SetOptionsFromProfile(RecordingProfile *profile,
                               const QString &videodev,
                               const QString &audiodev,
                               const QString &vbidev);

    void StartRecording(void);

    bool Open(void);
    bool IsOpen(void) const;
    void Close(void);

  private:
    ASIChannel       *m_channel;
    ASIStreamHandler *m_stream_handler;
};

#endif // _ASI_RECORDER_H_
