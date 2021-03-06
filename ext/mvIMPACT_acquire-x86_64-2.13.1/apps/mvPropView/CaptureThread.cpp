//-----------------------------------------------------------------------------
#include <apps/Common/wxAbstraction.h>
#include "CaptureThread.h"
#include <limits>
#include "PropViewFrame.h"
#include <wx/event.h>
#include <algorithm>

using namespace std;
using namespace mvIMPACT::acquire;

//=============================================================================
//================= Implementation TimerLite ==================================
//=============================================================================
//-----------------------------------------------------------------------------
class TimerLite
//-----------------------------------------------------------------------------
{
    clock_t m_endClocks;
    clock_t Delay2Clocks( unsigned long nextDelay_ms ) const
    {
        return ( nextDelay_ms * CLOCKS_PER_SEC ) / 1000;
    }
public:
    explicit TimerLite() : m_endClocks( 0 ) {}
    explicit TimerLite( unsigned long nextDelay_ms )
    {
        Restart( nextDelay_ms );
    }
    void Restart( unsigned long nextDelay_ms )
    {
        m_endClocks = clock() + Delay2Clocks( nextDelay_ms );
    }
    bool IsExpired( unsigned long nextDelay_ms )
    {
        clock_t curClocks = clock();
        if( curClocks > m_endClocks )
        {
            m_endClocks = curClocks + Delay2Clocks( nextDelay_ms );
            return true;
        }
        return false;
    }
};

//=============================================================================
//================= Implementation CaptureThread ==============================
//=============================================================================

unsigned int CaptureThread::m_activeInstance = 0;
unsigned int CaptureThread::m_instanceCnt = 0;

//-----------------------------------------------------------------------------
CaptureThread::CaptureThread( FunctionInterface* pFuncInterface, Device* pDev, wxWindow* pParentWindow, unsigned int pendingImageQueueDepth ) :
    wxThread( wxTHREAD_JOINABLE ), m_boLive( false ), m_boRecord( false ), m_boContinuousRecording( false ), m_boForwardIncompleteFrames( false ),
    m_currentRequest( INVALID_ID ), m_discardedImages( 0 ), m_instanceNr( m_instanceCnt++ ),
    m_lastErrorMessage(), m_lastRequestResult(), m_currentPlotInfoPath(), m_nextPendingRequest( INVALID_ID ),
    m_pendingRequestsForDisplayMax( static_cast<list<int>::size_type>( ( pendingImageQueueDepth < 1 ) ? 1 : pendingImageQueueDepth ) ), m_pendingRequestsForDisplay(),
    m_parentWindowID( pParentWindow->GetId() ), m_pDev( pDev ), m_pFuncInterface( pFuncInterface ),
    m_ImageRequestControl( pDev ), m_SettingIndex( 0 ), m_pParentWindowEventHandler( pParentWindow->GetEventHandler() ),
    m_currentSequenceIndexToDisplay( 0 ), m_currentCountToRecord( 0 ), m_totalCountToRecord( 0 ),
    m_maxNumberToRequest( 0 ), m_numberRequested( 0 ), m_numberReturned( 0 ),
    m_SkippedImageCount( 0 ), m_captureQueueDepth( 0 ), m_currentlyActiveRequests( 0 )
//-----------------------------------------------------------------------------
{
#if MULTI_SETTING_HACK
    m_CurrentSetting = 0;
    m_CaptureSettingUsageMode = csumManual;
#endif // #if MULTI_SETTING_HACK
}

//-----------------------------------------------------------------------------
CaptureThread::~CaptureThread()
//-----------------------------------------------------------------------------
{
    wxCriticalSectionLocker locker( m_critSect );
    UnlockPendingRequests();
}

//-----------------------------------------------------------------------------
bool CaptureThread::CheckSequenceRestart( bool boInformUserOnRecordStart ) const
//-----------------------------------------------------------------------------
{
    if( !boInformUserOnRecordStart )
    {
        return true;
    }

    bool result = false;
    wxMessageDialog dlg( NULL,
                         wxString::Format( wxT( "Restarts the image recording. All current recorded images (%lu) will be lost. Check 'Settings -> Silent recording' to get rid of this and other questions." ), static_cast<unsigned long int>( m_recordedSequence.size() ) ),
                         wxT( "Warning" ),
                         wxNO_DEFAULT | wxYES_NO | wxICON_INFORMATION );

    if( dlg.ShowModal() == wxID_YES )
    {
        result = true;
    }

    return result;
}

//-----------------------------------------------------------------------------
void CaptureThread::CollectBufferInfo( ComponentIterator it, std::vector<wxString>& infoStrings, const wxString& path /* = wxT("") */ ) const
//-----------------------------------------------------------------------------
{
    while( it.isValid() )
    {
        if( it.isVisible() )
        {
            if( it.isProp() )
            {
                Property prop( it );
                const unsigned int valCount = prop.valCount();
                if( valCount > 1 )
                {
                    for( int i = 0; i < static_cast<int>( valCount ); i++ )
                    {
                        infoStrings.push_back( wxString::Format( wxT( "%s%s[%d]: %s" ), path.c_str(), ConvertedString( it.name() ).c_str(), i, ConvertedString( prop.readS( i ) ).c_str() ) );
                    }
                }
                else
                {
                    infoStrings.push_back( wxString::Format( wxT( "%s%s: %s" ), path.c_str(), ConvertedString( it.name() ).c_str(), ConvertedString( prop.readS() ).c_str() ) );
                }
            }
            else if( it.isList() )
            {
                CollectBufferInfo( it.firstChild(), infoStrings, path + ConvertedString( it.name() ) + wxT( "/" ) );
            }
        }
        ++it;
    }
}

//-----------------------------------------------------------------------------
size_t CaptureThread::DisplaySequenceRequest( size_t nr )
//-----------------------------------------------------------------------------
{
    wxCriticalSectionLocker locker( m_critSect );
    m_currentSequenceIndexToDisplay = ( nr < m_recordedSequence.size() ) ? nr : m_recordedSequence.size() - 1;
    int req = m_recordedSequence[m_currentSequenceIndexToDisplay];
    if( req != m_currentRequest )
    {
        if( m_pendingRequestsForDisplayMax == 1 )
        {
            if( m_nextPendingRequest != INVALID_ID )
            {
                UnlockRequest( m_nextPendingRequest );
            }
            m_nextPendingRequest = req;
        }
        else
        {
            while( m_pendingRequestsForDisplay.size() >= m_pendingRequestsForDisplayMax )
            {
                UnlockRequest( m_pendingRequestsForDisplay.front() );
                m_pendingRequestsForDisplay.pop_front();
            }
            m_pendingRequestsForDisplay.push_back( req );
        }
    }

    return m_currentSequenceIndexToDisplay;
}

//-----------------------------------------------------------------------------
size_t CaptureThread::DisplaySequenceRequestNext( void )
//-----------------------------------------------------------------------------
{
    return DisplaySequenceRequest( m_currentSequenceIndexToDisplay + 1 );
}

//-----------------------------------------------------------------------------
size_t CaptureThread::DisplaySequenceRequestPrev( void )
//-----------------------------------------------------------------------------
{
    return DisplaySequenceRequest( ( m_currentSequenceIndexToDisplay != 0 ) ? ( m_currentSequenceIndexToDisplay - 1 ) : 0 );
}

//-----------------------------------------------------------------------------
void* CaptureThread::Entry( void )
//-----------------------------------------------------------------------------
{
    const unsigned long waitBeforeBreak = 60;
    TimerLite forceBreakTimer( waitBeforeBreak );
    while( !TestDestroy() )
    {
        int requestNr;
        {
            wxCriticalSectionLocker locker( m_critSect );
            if( m_boLive )
            {
                RequestImages();
            }
        }
        if( ( requestNr = m_pFuncInterface->imageRequestWaitFor( 0, 0 ) ) < 0 )
        {
            if( ( requestNr = m_pFuncInterface->imageRequestWaitFor( 200, 0 ) ) >= 0 )
            {
                forceBreakTimer.Restart( waitBeforeBreak );
            }
        }
        else if( forceBreakTimer.IsExpired( waitBeforeBreak ) )
        {
            Sleep( 10 );
        }
        if( requestNr >= 0 )
        {
            wxCriticalSectionLocker locker( m_critSect );
            if( m_pFuncInterface->isRequestNrValid( requestNr ) )
            {
                const Request* pReq = m_pFuncInterface->getRequest( requestNr );
                if( pReq )
                {
                    if( m_activeInstance == m_instanceNr )
                    {
                        {
                            wxCriticalSectionLocker locker_config( m_critSectConfig );
                            --m_currentlyActiveRequests;
                            ++m_numberReturned;
                            m_lastRequestResult = ConvertedString( pReq->requestResult.readS().c_str() );
                        }
                        TRequestResult rr = pReq->requestResult.read();
                        HOBJ settingUsed = ( pReq->infoSettingUsed.isValid() ) ? static_cast<HOBJ>( pReq->infoSettingUsed.read() ) : static_cast<HOBJ>( INVALID_ID );
#if MULTI_SETTING_HACK
                        if( settingUsed == INVALID_ID )
                        {
                            // deal with drivers that do not support the 'SettingUsed' property
                            if( m_PendingRequests[0] > 0 )
                            {
                                m_PendingRequests[0] = m_PendingRequests[0] - 1;
                            }
                        }
                        else
                        {
                            for( unsigned int i = 0; i < m_SettingCount; i++ )
                            {
                                if( ( m_Settings[i].second == settingUsed ) && ( m_PendingRequests[i] > 0 ) )
                                {
                                    if( rr == rrOK )
                                    {
                                        // Support for SVSEM 'TransferFrameType: Increasing':
                                        // one request will be returned multiple times with an increasing count of valid lines (LineCounter),
                                        // but with the same 'FameNr'. So only one request per 'FrameNr' is necessary.
                                        if( pReq->infoFrameNr.read() != m_LastPendingRequestFrameNr[i] )
                                        {
                                            m_PendingRequests[i] = m_PendingRequests[i] - 1;
                                            m_LastPendingRequestFrameNr[i] = pReq->infoFrameNr.read();
                                        }
                                        // Support for SVSEM 'TransferFrameType: Block':
                                        else if( pReq->imageOffsetY.read() != 0 )
                                        {
                                            m_PendingRequests[i] = m_PendingRequests[i] - 1;
                                        }
                                    }
                                    else
                                    {
                                        m_PendingRequests[i] = m_PendingRequests[i] - 1;
                                    }
                                    break;
                                }
                            }
                        }
#endif // #if MULTI_SETTING_HACK
                        wxCommandEvent eInfo( imageInfoEvent, m_parentWindowID );
                        RequestInfoData* pReqInfo = new RequestInfoData();
                        GetRequestInfoData( pReqInfo, pReq, settingUsed );
                        eInfo.SetClientData( pReqInfo );
                        ::wxPostEvent( m_pParentWindowEventHandler, eInfo );
                        if( ( rr == rrOK ) || ( ( rr == rrFrameIncomplete ) && m_boForwardIncompleteFrames ) )
                        {
                            if( m_boRecord )
                            {
                                m_recordedSequence.push_back( requestNr );
                            }
                            if( m_pendingRequestsForDisplayMax == 1 )
                            {
                                if( m_nextPendingRequest != INVALID_ID )
                                {
                                    const Request* pRequestToUnlock = m_pFuncInterface->getRequest( m_nextPendingRequest );
                                    int settingUsed = ( pRequestToUnlock->infoSettingUsed.isValid() ) ? pRequestToUnlock->infoSettingUsed.read() : INVALID_ID;
                                    UnlockRequest( m_nextPendingRequest );
                                    ++m_discardedImages;
                                    if( m_SkippedImageCount++ == 0 )
                                    {
                                        wxCommandEvent e( imageSkippedEvent, m_parentWindowID );
                                        e.SetInt( settingUsed );
                                        ::wxPostEvent( m_pParentWindowEventHandler, e );
                                    }
                                }
                                else
                                {
                                    wxCommandEvent e( imageReadyEvent, m_parentWindowID );
                                    e.SetClientData( m_pDev );
                                    ::wxPostEvent( m_pParentWindowEventHandler, e );
                                }
                                m_nextPendingRequest = requestNr;
                            }
                            else
                            {
                                if( m_pendingRequestsForDisplay.size() >= m_pendingRequestsForDisplayMax )
                                {
                                    while( m_pendingRequestsForDisplay.size() >= m_pendingRequestsForDisplayMax )
                                    {
                                        const Request* pRequestToUnlock = m_pFuncInterface->getRequest( m_pendingRequestsForDisplay.front() );
                                        int settingUsed = ( pRequestToUnlock->infoSettingUsed.isValid() ) ? pRequestToUnlock->infoSettingUsed.read() : INVALID_ID;
                                        UnlockRequest( m_pendingRequestsForDisplay.front() );
                                        m_pendingRequestsForDisplay.pop_front();
                                        ++m_discardedImages;
                                        if( m_SkippedImageCount++ == 0 )
                                        {
                                            wxCommandEvent e( imageSkippedEvent, m_parentWindowID );
                                            e.SetInt( settingUsed );
                                            ::wxPostEvent( m_pParentWindowEventHandler, e );
                                        }
                                    }
                                }
                                else
                                {
                                    wxCommandEvent e( imageReadyEvent, m_parentWindowID );
                                    e.SetClientData( m_pDev );
                                    ::wxPostEvent( m_pParentWindowEventHandler, e );
                                }
                                m_pendingRequestsForDisplay.push_back( requestNr );
                            }
                        }
                        else
                        {
                            if( rr == rrTimeout )
                            {
                                wxCommandEvent e( imageTimeoutEvent, m_parentWindowID );
                                ::wxPostEvent( m_pParentWindowEventHandler, e );
                            }
                            if( rr & rrUnprocessibleRequest )
                            {
                                m_lastErrorMessage = ConvertedString( pReq->requestResult.readS() );
                            }
                            m_pFuncInterface->imageRequestUnlock( requestNr );
                            if( GetLiveMode() )
                            {
                                // Livemode is stopped depending on request result
                                if( rr & rrUnprocessibleRequest )
                                {
                                    InternalSetLiveMode( false );
                                    wxCommandEvent e( liveModeAborted, m_parentWindowID );
                                    e.SetString( m_lastErrorMessage );
                                    ::wxPostEvent( m_pParentWindowEventHandler, e );
                                    if( m_currentCountToRecord )
                                    {
                                        m_currentCountToRecord = 1;
                                    }
                                }
                            }
                        }

                        bool boMultiFrameDone = false;
                        {
                            wxCriticalSectionLocker locker_config( m_critSectConfig );
                            if( m_boLive && ( m_maxNumberToRequest > 0 ) && ( m_maxNumberToRequest == m_numberReturned ) )
                            {
                                boMultiFrameDone = true;
                            }
                        }
                        if( boMultiFrameDone )
                        {
                            InternalSetLiveMode( false );
                            wxCommandEvent e( sequenceReadyEvent, m_parentWindowID );
                            e.SetInt( eosrMultiFrameDone );
                            ::wxPostEvent( m_pParentWindowEventHandler, e );
                        }

                        if( m_boContinuousRecording )
                        {
                            const int requestCount = m_pFuncInterface->requestCount();
                            {
                                wxCriticalSectionLocker locker_config( m_critSectConfig );
                                RequestContainer::size_type maxSequenceSize = static_cast<RequestContainer::size_type>( ( m_captureQueueDepth > 0 ) ? requestCount - m_captureQueueDepth : requestCount / 2 );
                                if( m_recordedSequence.size() > maxSequenceSize )
                                {
                                    int reqNrFreed = FreeLastRequestFromSequence( m_pendingRequestsForDisplay.empty() ? INVALID_ID : m_pendingRequestsForDisplay.back() );
                                    if( ( m_pendingRequestsForDisplayMax > 1 ) && ( reqNrFreed != INVALID_ID ) )
                                    {
                                        list<int>::iterator it;
                                        while( ( it = find( m_pendingRequestsForDisplay.begin(), m_pendingRequestsForDisplay.end(), reqNrFreed ) ) != m_pendingRequestsForDisplay.end() )
                                        {
                                            m_pendingRequestsForDisplay.erase( it );
                                        }
                                    }
                                }
                            }
                        }
                        else if( m_currentCountToRecord && m_boRecord )
                        {
                            --m_currentCountToRecord;
                            if( ( m_currentCountToRecord == 0 ) || ( m_recordedSequence.size() == m_pFuncInterface->requestCount() ) )
                            {
                                InternalSetLiveMode( false );
                                wxCommandEvent e( sequenceReadyEvent, m_parentWindowID );
                                e.SetInt( eosrRecordingDone );
                                ::wxPostEvent( m_pParentWindowEventHandler, e );
                            }
                        }
                    }
                    else
                    {
                        m_pFuncInterface->imageRequestUnlock( requestNr );
                        ++m_discardedImages;
                    }
                }
            }
        }
    }
    return 0;
}

//-----------------------------------------------------------------------------
int CaptureThread::FreeLastRequestFromSequence( int requestNotToUnlock )
//-----------------------------------------------------------------------------
{
    int nr = m_recordedSequence.front();
    bool boMustUnlock = nr != requestNotToUnlock;
    if( boMustUnlock )
    {
        m_pFuncInterface->imageRequestUnlock( nr );
    }
    m_recordedSequence.pop_front();
    return boMustUnlock ? nr : INVALID_ID;
}

//-----------------------------------------------------------------------------
void CaptureThread::FreeSequence( int requestNotToUnlock /* = INVALID_ID */ )
//-----------------------------------------------------------------------------
{
    while( !m_recordedSequence.empty() )
    {
        FreeLastRequestFromSequence( requestNotToUnlock );
    }
    m_currentSequenceIndexToDisplay = 0;
}

//-----------------------------------------------------------------------------
TRequestResult CaptureThread::GetImageData( RequestData& requestData ) const
//-----------------------------------------------------------------------------
{
    wxCriticalSectionLocker locker( m_critSect );
    if( ( m_pendingRequestsForDisplayMax == 1 ) && ( m_nextPendingRequest != INVALID_ID ) )
    {
        m_currentRequest = m_nextPendingRequest;
        m_nextPendingRequest = INVALID_ID;
    }
    else if( ( m_pendingRequestsForDisplayMax > 1 ) && !m_pendingRequestsForDisplay.empty() )
    {
        m_currentRequest = m_pendingRequestsForDisplay.front();
        m_pendingRequestsForDisplay.pop_front();
    }

    requestData.requestNr_ = m_currentRequest;
    if( m_currentRequest != INVALID_ID )
    {
        const Request* pReq = m_pFuncInterface->getRequest( m_currentRequest );
        if( pReq )
        {
            ImageBufferDesc nbd( pReq->getImageBufferDesc() );
            requestData.image_ = nbd;
            //requestData.image_ = ImageBufferDesc(pReq->getImageBufferDesc()); // doesn't work with GCC...
            GetRequestInfoData( &requestData.requestInfo_, pReq, ( pReq->infoSettingUsed.isValid() ) ? static_cast<HOBJ>( pReq->infoSettingUsed.read() ) : static_cast<HOBJ>( INVALID_ID ) );
            requestData.bayerParity_ = pReq->imageBayerMosaicParity.read();
            if( requestData.bayerParity_ != bmpUndefined )
            {
                requestData.pixelFormat_ = wxString::Format( wxT( "%s(BayerPattern=%s)" ), ConvertedString( pReq->imagePixelFormat.readS() ).c_str(), ConvertedString( pReq->imageBayerMosaicParity.readS() ).c_str() );
            }
            else
            {
                requestData.pixelFormat_ = ConvertedString( pReq->imagePixelFormat.readS() );
            }
            return pReq->requestResult.read();
        }
    }
    else
    {
        requestData = RequestData();
        return rrOK;
    }
    return rrError;
}

//-----------------------------------------------------------------------------
void CaptureThread::GetImageInfo( bool boFillInfoVector, std::vector<wxString>& infoStrings ) const
//-----------------------------------------------------------------------------
{
    wxCriticalSectionLocker locker( m_critSect );
    if( m_currentRequest != INVALID_ID )
    {
        const Request* pRequest = m_pFuncInterface->getRequest( m_currentRequest );
        if( pRequest )
        {
            if( boFillInfoVector )
            {
                ComponentIterator it( pRequest->getInfoIterator() );
                CollectBufferInfo( it, infoStrings );
            }
        }
    }
}

//-----------------------------------------------------------------------------
wxString CaptureThread::GetLastRequestResult( void ) const
//-----------------------------------------------------------------------------
{
    wxCriticalSectionLocker locker_config( m_critSectConfig );
    return m_lastRequestResult;
}

//-----------------------------------------------------------------------------
void CaptureThread::GetRequestInfoData( RequestInfoData* pReqInfo, const Request* pReq, HOBJ settingUsed ) const
//-----------------------------------------------------------------------------
{
    if( !m_currentPlotInfoPath.empty() )
    {
        Property p( pReq->getComponentLocator().findComponent( m_currentPlotInfoPath ) );
        pReqInfo->plotValue.type = p.type();
        switch( p.type() )
        {
        case ctPropInt:
            pReqInfo->plotValue.value.intRep    = PropertyI( p ).read();
            break;
        case ctPropInt64:
            pReqInfo->plotValue.value.int64Rep  = PropertyI64( p ).read();
            break;
        case ctPropFloat:
            pReqInfo->plotValue.value.doubleRep = PropertyF( p ).read();
            break;
        default:
            break;
        }
    }
    pReqInfo->exposeTime_us = pReq->infoExposeTime_us.read();
    pReqInfo->frameNr = pReq->infoFrameNr.read();
    pReqInfo->gain_dB = pReq->infoGain_dB.read();
    pReqInfo->settingUsed = settingUsed;
    pReqInfo->timeStamp_us = pReq->infoTimeStamp_us.read();
    pReqInfo->requestResult = pReq->requestResult.read();
}

//-----------------------------------------------------------------------------
size_t CaptureThread::GetSkippedImageCount( void )
//-----------------------------------------------------------------------------
{
    size_t res = m_SkippedImageCount;
    m_SkippedImageCount = 0;
    return res;
}

//-----------------------------------------------------------------------------
bool CaptureThread::InternalSetLiveMode( bool boOn, bool boInformUserOnRecordStart /* = true */ )
//-----------------------------------------------------------------------------
{
    if( boOn )
    {
        UnlockPendingRequests();
        if( m_boRecord )
        {
            if( m_recordedSequence.empty() || CheckSequenceRestart( boInformUserOnRecordStart ) )
            {
                InternalSetRecordMode( true, true );
            }
        }
        else
        {
            FreeSequence();
        }

        m_discardedImages = 0;
        m_SkippedImageCount = 0;
        int framesRequested = 0;
        int result = RequestImages( &framesRequested );
        if( ( result == DMR_NO_ERROR ) ||
            ( ( result == DEV_NO_FREE_REQUEST_AVAILABLE ) && ( framesRequested > 0 ) ) )
        {
            m_boLive = boOn;
            result = m_pFuncInterface->acquisitionStart();
            switch( result )
            {
            case DMR_NO_ERROR:
            case DMR_FEATURE_NOT_AVAILABLE:
                break;
            case DMR_ACQUISITION_ENGINE_BUSY:
            default:
                wxMessageDialog dlg( NULL, wxString::Format( wxT( "%s(%d): Live not started as calling 'AcquisitionStart' did fail(Reason: %s)! More information can be found in the *.log-file or the debug output.\n" ), ConvertedString( __FUNCTION__ ).c_str(), __LINE__, ConvertedString( ImpactAcquireException::getErrorCodeAsString( result ) ).c_str() ), wxT( "Error" ), wxOK | wxICON_WARNING );
                dlg.ShowModal();
                break;
            }
        }
        else if( result != DEV_NO_FREE_REQUEST_AVAILABLE )
        {
            wxMessageDialog dlg( NULL, wxString::Format( wxT( "%s(%d): Live not started as queuing requests did fail(Reason: %s)! More information can be found in the *.log-file or the debug output.\n" ), ConvertedString( __FUNCTION__ ).c_str(), __LINE__, ConvertedString( ImpactAcquireException::getErrorCodeAsString( result ) ).c_str() ), wxT( "Error" ), wxOK | wxICON_WARNING );
            dlg.ShowModal();
        }
    }
    else
    {
        m_boLive = false;
        m_pFuncInterface->acquisitionStop();
        RequestReset();
    }

    return m_boLive;
}

//-----------------------------------------------------------------------------
bool CaptureThread::InternalSetRecordMode( bool boOn, bool boUnlockCurrentRequest )
//-----------------------------------------------------------------------------
{
    if( boOn )
    {
        m_currentSequenceIndexToDisplay = 0;
        RequestReset();
        UnlockPendingRequests();
        FreeSequence( boUnlockCurrentRequest ? INVALID_ID : m_currentRequest );
        m_currentCountToRecord = ( m_totalCountToRecord == 0 ) ? m_pFuncInterface->requestCount() : m_totalCountToRecord;
    }
    m_boRecord = boOn;
    return m_boRecord;
}

//-----------------------------------------------------------------------------
int CaptureThread::RequestSingle( bool boInformUserOnRecordStart )
//-----------------------------------------------------------------------------
{
    if( !m_boRecord )
    {
        FreeSequence( m_currentRequest );
    }

#if MULTI_SETTING_HACK
    if( m_CaptureSettingUsageMode == csumAutomatic )
    {
        m_ImageRequestControl.setting.write( m_Settings[m_CurrentSetting].second );
        m_CurrentSetting = static_cast<unsigned int>( ( m_CurrentSetting + 1 ) % m_SettingCount );
    }
#endif // #if MULTI_SETTING_HACK

    int result = m_pFuncInterface->imageRequestSingle( &m_ImageRequestControl );
    if( m_boRecord && ( result < 0 ) && CheckSequenceRestart( boInformUserOnRecordStart ) )
    {
        SetRecordMode( true );
        result = RequestSingle( boInformUserOnRecordStart );
    }
    else if( result == DMR_NO_ERROR )
    {
        if( ( result = m_pFuncInterface->acquisitionStart() ) == DMR_FEATURE_NOT_AVAILABLE )
        {
            // do not report this as an error. Not every device/driver combination will support this
            result = DMR_NO_ERROR;
        }
    }
    return result;
}

//-----------------------------------------------------------------------------
int CaptureThread::RequestReset( void )
//-----------------------------------------------------------------------------
{
    wxCriticalSectionLocker locker_config( m_critSectConfig );
    int result = m_pFuncInterface->imageRequestReset( 0, 0 );
    m_currentlyActiveRequests = 0;
    for( unsigned int i = 0; i < m_SettingCount; i++ )
    {
#if MULTI_SETTING_HACK
        m_LastPendingRequestFrameNr[i] = std::numeric_limits<int64_type>::max();
        m_PendingRequests[i] = 0;
#endif // #if MULTI_SETTING_HACK
    }
    return result;
}

//-----------------------------------------------------------------------------
int CaptureThread::RequestImages( int* pFramesRequested /* = 0 */ )
//-----------------------------------------------------------------------------
{
    int requestCnt = 0;
    int requestResult = DMR_NO_ERROR;
    do
    {
#if MULTI_SETTING_HACK
        unsigned int index = 0;
        unsigned int lastSettingIndex = 0;
#endif // #if MULTI_SETTING_HACK
        {
            wxCriticalSectionLocker locker_config( m_critSectConfig );
            if( m_boRecord && ( m_totalCountToRecord > 0 ) && ( m_currentlyActiveRequests - m_currentCountToRecord <= 0 ) )
            {
                break;
            }

            if( ( m_maxNumberToRequest > 0 ) && ( m_maxNumberToRequest == m_numberRequested ) )
            {
                break;
            }
#if MULTI_SETTING_HACK
            if( m_CaptureSettingUsageMode == csumAutomatic )
            {
                lastSettingIndex = m_SettingIndex;
                unsigned int minVal = std::numeric_limits<unsigned int>::max();
                for( unsigned int i = 0; i < m_SettingCount; i++ )
                {
                    if( ( m_PendingRequests[m_SettingIndex] < m_pFuncInterface->requestCount() / m_SettingCount ) && ( m_PendingRequests[m_SettingIndex] < minVal ) )
                    {
                        index = m_SettingIndex;
                        minVal = m_PendingRequests[m_SettingIndex];
                    }
                    m_SettingIndex = ( m_SettingIndex + 1 ) % static_cast<int>( m_SettingCount );
                }
                if( minVal == std::numeric_limits<unsigned int>::max() )
                {
                    m_SettingIndex = lastSettingIndex;
                    m_RequestFailed[m_SettingIndex]++;
                    break;
                }
                m_ImageRequestControl.setting.write( m_Settings[index].second );
                m_SettingIndex = ( index + 1 ) % static_cast<int>( m_SettingCount );
            }
            else
            {
                int currentSetting = m_ImageRequestControl.setting.read();
                for( unsigned int i = 0; i < m_SettingCount; i++ )
                {
                    if( m_Settings[i].second == currentSetting )
                    {
                        index = i;
                        break;
                    }
                }
            }
#endif // #if MULTI_SETTING_HACK
        }
        if( ( requestResult = m_pFuncInterface->imageRequestSingle( &m_ImageRequestControl ) ) != DMR_NO_ERROR )
        {
#if MULTI_SETTING_HACK
            m_SettingIndex = lastSettingIndex;
            m_RequestFailed[m_SettingIndex]++;
#endif // #if MULTI_SETTING_HACK
            break;
        }
#if MULTI_SETTING_HACK
        {
            wxCriticalSectionLocker locker_config( m_critSectConfig );
            if( m_CaptureSettingUsageMode == csumAutomatic )
            {
                m_PendingRequests[index] = m_PendingRequests[index] + 1;
                m_RequestFailed[index] = 0;
            }
        }
#endif // #if MULTI_SETTING_HACK
        ++requestCnt;
        {
            wxCriticalSectionLocker locker_config( m_critSectConfig );
            ++m_currentlyActiveRequests;
            ++m_numberRequested;
            if( ( m_captureQueueDepth > 0 ) && ( m_currentlyActiveRequests >= m_captureQueueDepth ) )
            {
                break;
            }
        }
    }
    while( true );
    if( pFramesRequested )
    {
        *pFramesRequested = requestCnt;
    }
    return requestResult;
}

//-----------------------------------------------------------------------------
void CaptureThread::SetActive( void )
//-----------------------------------------------------------------------------
{
    wxCriticalSectionLocker locker( m_critSect );
    m_activeInstance = m_instanceNr;
}

//-----------------------------------------------------------------------------
void CaptureThread::SetCaptureMode( bool boForwardIncompleteFrames )
//-----------------------------------------------------------------------------
{
    m_boForwardIncompleteFrames = boForwardIncompleteFrames;
}

//-----------------------------------------------------------------------------
void CaptureThread::SetCaptureQueueDepth( int queueDepth )
//-----------------------------------------------------------------------------
{
    wxCriticalSectionLocker locker_config( m_critSectConfig );
    m_captureQueueDepth = queueDepth;
}

#if MULTI_SETTING_HACK
//-----------------------------------------------------------------------------
void CaptureThread::SetCaptureSettingUsageMode( TCaptureSettingUsageMode mode )
//-----------------------------------------------------------------------------
{
    wxCriticalSectionLocker locker( m_critSect );
    m_CaptureSettingUsageMode = mode;
}
#endif // #if MULTI_SETTING_HACK

//-----------------------------------------------------------------------------
void CaptureThread::SetContinuousRecording( bool boContinuous )
//-----------------------------------------------------------------------------
{
    wxCriticalSectionLocker locker( m_critSect );
    m_boContinuousRecording = boContinuous;
}

//-----------------------------------------------------------------------------
void CaptureThread::SetCurrentPlotInfoPath( const string& currentPlotInfoPath )
//-----------------------------------------------------------------------------
{
    wxCriticalSectionLocker locker( m_critSect );
    m_currentPlotInfoPath = currentPlotInfoPath;
}

//-----------------------------------------------------------------------------
bool CaptureThread::SetLiveMode( bool boOn, bool boInformUserOnRecordStart /* = true */ )
//-----------------------------------------------------------------------------
{
    wxCriticalSectionLocker locker( m_critSect );
    return InternalSetLiveMode( boOn, boInformUserOnRecordStart );
}

//-----------------------------------------------------------------------------
void CaptureThread::SetMultiFrameSequenceSize( size_t sequenceSize )
//-----------------------------------------------------------------------------
{
    wxCriticalSectionLocker locker( m_critSect );
    m_maxNumberToRequest = sequenceSize;
    m_numberRequested = 0;
    m_numberReturned = 0;
}

//-----------------------------------------------------------------------------
bool CaptureThread::SetRecordMode( bool boOn )
//-----------------------------------------------------------------------------
{
    wxCriticalSectionLocker locker( m_critSect );
    return InternalSetRecordMode( boOn, false );
}

//-----------------------------------------------------------------------------
void CaptureThread::SetRecordSequenceSize( size_t sequenceSize )
//-----------------------------------------------------------------------------
{
    wxCriticalSectionLocker locker( m_critSect );
    m_totalCountToRecord = sequenceSize;
}

//-----------------------------------------------------------------------------
void CaptureThread::UnlockPendingRequests( void )
//-----------------------------------------------------------------------------
{
    if( m_pendingRequestsForDisplayMax == 1 )
    {
        if( m_nextPendingRequest != INVALID_ID )
        {
            m_pFuncInterface->imageRequestUnlock( m_nextPendingRequest );
            m_nextPendingRequest = INVALID_ID;
        }
    }
    else
    {
        while( !m_pendingRequestsForDisplay.empty() )
        {
            m_pFuncInterface->imageRequestUnlock( m_pendingRequestsForDisplay.front() );
            m_pendingRequestsForDisplay.pop_front();
        }
    }
}

//-----------------------------------------------------------------------------
void CaptureThread::UnlockRequest( int nr, bool boForceUnlock /* = false */ )
//-----------------------------------------------------------------------------
{
    // Unlock of request only if it's not part of the recorded sequence.
    // If the force flag is specified make sure the request will be removed
    // from the recorded sequence
    // (a request usage counter would be better)
    RequestContainer::iterator it = m_recordedSequence.begin();
    RequestContainer::const_iterator itEND = m_recordedSequence.end();
    while( it != itEND )
    {
        if( *it == nr )
        {
            break;
        }
        ++it;
    }
    if( it == itEND )
    {
        m_pFuncInterface->imageRequestUnlock( nr );
    }
    else if( boForceUnlock )
    {
        m_pFuncInterface->imageRequestUnlock( nr );
        m_recordedSequence.erase( it );
    }

    if( m_currentRequest == nr )
    {
        m_currentRequest = INVALID_ID;
    }
}

#if MULTI_SETTING_HACK
//-----------------------------------------------------------------------------
void CaptureThread::UpdateSettingTable( void )
//-----------------------------------------------------------------------------
{
    wxCriticalSectionLocker locker( m_critSect );
    m_ImageRequestControl.setting.getTranslationDict( m_Settings );
    m_SettingCount = m_Settings.size();
    m_PendingRequests.resize( m_SettingCount );
    m_RequestFailed.resize( m_SettingCount );
    m_LastPendingRequestFrameNr.resize( m_SettingCount );
    for( unsigned int i = 0; i < m_SettingCount; i++ )
    {
        m_LastPendingRequestFrameNr[i] = std::numeric_limits<int64_type>::max();
    }
}
#endif // #if MULTI_SETTING_HACK
