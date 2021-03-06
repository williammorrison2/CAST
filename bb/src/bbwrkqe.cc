/*******************************************************************************
 |    bbwrkqe.cc
 |
 |  © Copyright IBM Corporation 2015,2016. All Rights Reserved
 |
 |    This program is licensed under the terms of the Eclipse Public License
 |    v1.0 as published by the Eclipse Foundation and available at
 |    http://www.eclipse.org/legal/epl-v10.html
 |
 |    U.S. Government Users Restricted Rights:  Use, duplication or disclosure
 |    restricted by GSA ADP Schedule Contract with IBM Corp.
 *******************************************************************************/

#include <sys/syscall.h>

#include "bbinternal.h"
#include "BBLV_Info.h"
#include "BBLV_Metadata.h"
#include "bbwrkqe.h"
#include "bbserver_flightlog.h"
#include "Extent.h"
#include "ExtentInfo.h"
#include "util.h"
#include "xfer.h"


void WRKQE::addToAccumulatedTime()
{
    // NOTE: It is possible with multiple threads when thread A is
    //       processing the last work item, additional work items are
    //       enqueued, dequeued, and processed by other threads.
    //       In this case, when thread A is finished with it's work item,
    //       currentStartTime will already be zero and the appropriate
    //       accumulated time will have already been calculated.
    if (currentStartTime)
    {
        BB_GetTimeDifference(currentStartTime);
        accumulatedTime += currentStartTime;
        currentStartTime = 0;
    }

    return;
}

void WRKQE::addWorkItem(WorkID& pWorkItem, const bool pValidateQueue)
{
    wrkq->push(pWorkItem);
    // NOTE: Unless we are debugging a problem, pValidateQueue always comes in as false.
    //       Therefore, we never hit the endOnError() below in production...
    if (pValidateQueue && this != HPWrkQE)
    {
        // NOTE:  This method exists to make sure that the number of work queue entries
        //        matches the number of entries in the vector of extents to transfer
        //        for this LVKey when we are adding an entry to the work queue.
        BBLV_Info* l_LV_Info = pWorkItem.getLV_Info();
        if (l_LV_Info && (getWrkQ_Size() != l_LV_Info->getNumberOfExtents()))
        {
            LOG(bb,error) << "WRKQE::addWorkItem(): Mismatch between number of elements on work queue (" << getWrkQ_Size() << ") and number of extents in the vector of extents (" \
                          << l_LV_Info->getNumberOfExtents() << ")";
            endOnError();
        }
    }

    incrementNumberOfWorkItems();

    // If the first item in the queue and we have not captured the
    // current clock, do so now.
    // NOTE: The currentStartTime can be non-zero with this being
    //       the only work item on the queue.  Another thread could be
    //       processsing what was the last entry, had removed the work
    //       item, but the currentStartTime isn't zeroed until processing
    //       for the work item is complete.
    if (getWrkQ_Size() == 1 && (!currentStartTime))
    {
        BB_GetTime(currentStartTime);
    }

    if (g_LogAllAsyncRequestActivity && this == HPWrkQE)
    {
        LOG(bb,info) << "AsyncRequest -> incrementNumberOfHP_WorkItems: numberOfWorkItems " << HPWrkQE->getNumberOfWorkItems();
    }

    return;
};

double WRKQE::calcTotalAccumulatedTime()
{
    uint64_t l_NumberOfSeconds = (currentStartTime ? currentStartTime : 0);
    if (l_NumberOfSeconds)
    {
        BB_GetTimeDifference(l_NumberOfSeconds);
    }
    l_NumberOfSeconds += accumulatedTime;

    return (double)l_NumberOfSeconds/(double)g_TimeBaseScale;
}

void WRKQE::dump(const char* pSev, const char* pPrefix, const DUMP_ALL_DATA_INDICATOR pDataInd)
{
    if (wrkqmgr.checkLoggingLevel(pSev))
    {
        int l_HP_TransferQueueLocked = 0;
        int l_HP_TransferQueueUnlocked = 0;
        int l_TransferQueueLocked = 0;
        size_t l_NumberOfInFlightExtents = 0;
        BBLV_Info* l_LV_Info = NULL;

        string l_JobId = to_string(jobid);
        string l_ActiveTransferDefs = "";
        string l_JobStepId = "";
        string l_Handle = "";
        string l_ContribId = "";
        string l_Rate = "";
        string l_Bucket = "";
        string l_WorkQueueReturnedWithNegativeBucket = "";
        string l_Suspended = (suspended ? "Y" : "N");
        string l_Output = "Job " + l_JobId;
        string l_Output2 = "";

        if (!this->transferQueueIsLocked())
        {
            if (HPWrkQE && HPWrkQE->transferQueueIsLocked())
            {
                HPWrkQE->unlock((LVKey*)0, "WRKQMGR::dump - before");
                l_HP_TransferQueueUnlocked = 1;
            }
            this->lock((LVKey*)0, "WRKQE::dump - before");
            l_TransferQueueLocked = 1;
            if (l_HP_TransferQueueUnlocked)
            {
                HPWrkQE->lock((LVKey*)0, "WRKQMGR::dump - before");
                l_HP_TransferQueueLocked = 1;
            }
        }

        size_t l_NumberOfWorkItems = getNumberOfWorkItems();
        size_t l_NumberOfWorkItemsProcessed = getNumberOfWorkItemsProcessed();
        string l_SuspendedReposts = to_string(getSuspendedReposts());

        if (wrkq)
        {
            size_t l_NumberOfExtents = getWrkQ_Size();
            // NOTE: If we don't have any work items left on the work queue, we do not have an 'easy'
            //       way to obtain l_LV_Info/l_ExtentInfo because we don't currently have the local
            //       metadata lock and can't obtain it because we must hold the work queue manager lock.
            //
            //       This is unfortunate, as we could have inflight entries without any entries left on
            //       the work queue and we won't directly display that information via this dump.
            //       However, in this case, if you compare the number of work items to the number of
            //       work items processed, you should be able to infer the number of inflight work items.
            if ((this != HPWrkQE) && l_NumberOfExtents)
            {
                l_LV_Info = getLV_Info();
                if (l_LV_Info)
                {
                    ExtentInfo l_ExtentInfo = l_LV_Info->getNextExtentInfo();
                    l_NumberOfInFlightExtents = getNumberOfInFlightExtents();
                    // \todo low priority
                    // NOTE: We have taken SIGSEGV on the following statement if pSev is "debug"
                    l_ActiveTransferDefs = to_string(l_LV_Info->getNumberOfTransferDefsWithOutstandingWorkItems());
                    l_JobStepId = (l_ExtentInfo.getTransferDef() ? to_string(l_ExtentInfo.getTransferDef()->getJobStepId()) : "None");
                    l_Handle = to_string(l_ExtentInfo.getHandle());
                    l_ContribId = to_string(l_ExtentInfo.getContrib());
                    if (rate || pDataInd == DUMP_ALL_DATA)
                    {
                        l_Rate = (HPWrkQE != this ? to_string(rate) : "H");
                        l_Bucket = (HPWrkQE != this ? to_string(bucket) : "H");
                        l_WorkQueueReturnedWithNegativeBucket = (HPWrkQE != this ? to_string(workQueueReturnedWithNegativeBucket) : "H");
                    }
                }
            }

            if (suspended || pDataInd == DUMP_ALL_DATA)
            {
                l_Output += ", Susp " + l_Suspended;
                l_Output += ", SuspRpst " + l_SuspendedReposts;
            }
            if (l_ActiveTransferDefs.size())
            {
                l_Output += ", #ActTDefs " + l_ActiveTransferDefs;
            }
            if (l_JobStepId.size())
            {
                l_Output += ", Jobstep " + l_JobStepId;
            }
            if (l_Handle.size())
            {
                l_Output += ", Hdl " + l_Handle;
            }
            if (l_ContribId.size())
            {
                l_Output += ", Cntb " + l_ContribId;
            }
            if (rate || pDataInd == DUMP_ALL_DATA)
            {
                if (l_Rate.size())
                {
                    l_Output += ", Rate " + l_Rate;
                }
                if (l_Bucket.size())
                {
                    l_Output += ", Bkt " + l_Bucket;
                }
                if (pDataInd == DUMP_ALL_DATA)
                {
                    if (l_WorkQueueReturnedWithNegativeBucket.size())
                    {
                        l_Output += ", WQRNegB " + l_WorkQueueReturnedWithNegativeBucket;
                    }
                }
            }

            if ((this != HPWrkQE) && l_NumberOfExtents)
            {
                l_Output2 = ", #InFlt " + to_string(l_NumberOfInFlightExtents);
            }

            double l_TotalTime = calcTotalAccumulatedTime();
            char l_Temp[64] = {'\0'};
            snprintf(l_Temp, sizeof(l_Temp), "%.6f", l_TotalTime);
            l_Output2 = l_Output2 + ", Time " + string(l_Temp) + (currentStartTime ? "+" : "");

            if (!strcmp(pSev,"debug"))
            {
                LOG(bb,debug) << pPrefix << lvKey << ", " << l_Output << ", #Items " << l_NumberOfWorkItems << ", #Proc'd " << l_NumberOfWorkItemsProcessed << l_Output2 << ", CurSize " << getWrkQ_Size();
            }
            else if (!strcmp(pSev,"info"))
            {
                LOG(bb,info) << pPrefix << lvKey << ", " << l_Output << ", #Items " << l_NumberOfWorkItems << ", #Proc'd " << l_NumberOfWorkItemsProcessed << l_Output2 << ", CurSize " << getWrkQ_Size();
            }
        }
        else
        {
            LOG(bb,error) << pPrefix << lvKey << ", " << l_Output << ", #Items " << l_NumberOfWorkItems << ", #Proc'd " << l_NumberOfWorkItemsProcessed << ", wrkq is NULL";
        }

        if (l_HP_TransferQueueLocked)
        {
            l_HP_TransferQueueLocked = 0;
            HPWrkQE->unlock((LVKey*)0, "WRKQMGR::dump - after");
        }
        if (l_TransferQueueLocked)
        {
            l_TransferQueueLocked = 0;
            this->unlock((LVKey*)0, "WRKQE::dump - exit");
        }
        if (l_HP_TransferQueueUnlocked)
        {
            l_HP_TransferQueueUnlocked = 0;
            HPWrkQE->lock((LVKey*)0, "WRKQMGR::dump - after");
        }
    }

    return;
}

int WRKQE::getIssuingWorkItem()
{
    return issuingWorkItem;
}

uint64_t WRKQE::getNumberOfInFlightExtents()
{
    return (lvinfo ? lvinfo->getNumberOfInFlightExtents() : 0);
}

void WRKQE::loadBucket()
{
    if (rate)
    {
        if (getWrkQ_Size())
        {
            dump("debug", "loadBucket(): Before bucket modification: ", DUMP_ALL_DATA);
            int64_t l_Rate = (int64_t)rate;

            // NOTE: If there are extents in the work queue, simply add to the
            //       bucket value.  If during the last 'bucket cycle' this work
            //       queue did not schedule any work, let the bucket value grow
            //       so that, in the long run, we converge on the specified
            //       rate/sec.
            // NOTE: If the current bucket value is zero, add (rate-1) so we
            //       do not send as double the rate if the size of the extents
            //       is exactly the rate/sec.  (A bucket value of zero has I/O
            //       scheduled for another extent.)
//            bucket = MIN(bucket+l_Rate, l_Rate);
            bucket += (bucket != 0 ? l_Rate : (l_Rate-1));

            if (bucket >= 0)
            {
                workQueueReturnedWithNegativeBucket = 0;
            }
            dump("debug", "loadBucket(): After bucket modification: ", DUMP_ALL_DATA);
        }
        else
        {
            // If this work queue currently has no entries,
            // set the bucket value to zero
            bucket = 0;
            workQueueReturnedWithNegativeBucket = 0;
        }
    }

    return;
}

// NOTE: pLVKey is not currently used, but can come in as null.
void WRKQE::lock(const LVKey* pLVKey, const char* pMethod)
{
    stringstream errorText;

    if (!transferQueueIsLocked())
    {
        // NOTE: We must obtain the lock before we verify the lock protocol.
        //       Otherwise, the issuingWorkItem check will fail...
        pthread_mutex_lock(&lock_transferqueue);
        transferQueueLocked = pthread_self();

        // Verify lock protocol
        if (issuingWorkItem)
        {
            FL_Write(FLError, lockPV_TQLock1, "WRKQE::lock: Transfer queue lock being obtained while a work item is being issued",0,0,0,0);
            errorText << "WRKQE::lock: Transfer queue lock being obtained while a work item is being issued";
            LOG_ERROR_TEXT_AND_RAS(errorText, bb.internal.lockprotocol.locktq1)
            endOnError();
        }

        pid_t tid = syscall(SYS_gettid);  // \todo eventually remove this.  incurs syscall for each log entry
        if (HPWrkQE == this)
        {
            if (strstr(pMethod, "%") == NULL)
            {
                if (g_LockDebugLevel == "info")
                {
                    LOG(bb,info) << "HPTRN_Q:   LOCK <- " << pMethod << ", jobid " << jobid << ", " << lvKey;
                }
                else
                {
                    LOG(bb,debug) << "HPTRN_Q:   LOCK <- " << pMethod << ", jobid " << jobid << ", " << lvKey;
                }
            }
            FL_Write(FLMutex, lockHPTransferQ, "lockHPTransferQueue.  threadid=%ld",tid,0,0,0);
        }
        else
        {
            if (strstr(pMethod, "%") == NULL)
            {
                if (g_LockDebugLevel == "info")
                {
                    LOG(bb,info) << "TRNFR_Q:   LOCK <- " << pMethod << ", jobid " << jobid << ", " << lvKey;
                }
                else
                {
                    LOG(bb,debug) << "TRNFR_Q:   LOCK <- " << pMethod << ", jobid " << jobid << ", " << lvKey;
                }
            }
            FL_Write(FLMutex, lockTransferQ, "lockTransferQueue.  threadid=%ld",tid,0,0,0);
        }
    }
    else
    {
        FL_Write(FLError, lockTransferQERROR, "lockTransferQueue called when lock already owned by thread",0,0,0,0);
        flightlog_Backtrace(__LINE__);
        // For now, also to the console...
        LOG(bb,error) << "TRNFR_Q: Request made to lock the transfer queue by " << pMethod << ", but the lock is already owned.";
        logBacktrace();
    }

    return;
}

double WRKQE::processBucket(BBTagID& pTagId, ExtentInfo& pExtentInfo)
{
    double l_Delay = 0;
    BBLV_Info* l_LV_Info = 0;
    BBTransferDef* l_TransferDef = 0;
    bool l_MadeBucketModification = false;

    if (rate)
    {
        // Must be a throttled work queue...
        if (getWrkQ_Size())
        {
            Extent* l_Extent = pExtentInfo.getExtent();
            LOG(bb,debug) << "processBucket(): For extent with length " << l_Extent->getLength();
            dump("debug", "processBucket(): Before bucket modification: ");

            // NOTE: The code below is very similar to the code in transferExtent() in xfer.cc.  The code there
            //       logs messages if an extent is not going to be transferred for some reason.  Here, we simply
            //       want to check for those similar conditions, determining if we should decrement the bucket value.
            l_LV_Info = getLV_Info();
            if ((!(l_Extent->isCP_Transfer())) && l_Extent->getLength())
            {
                BBTagInfo* l_TagInfo = pExtentInfo.getTagInfo();
                if (l_TagInfo)
                {
                    if (!l_LV_Info->stageOutEnded())
                    {
                        if (!(l_LV_Info->resizeLogicalVolumeDuringStageOut() && l_LV_Info->stageOutStarted() && (l_Extent->flags & BBI_TargetSSD)))
                        {
                            l_TransferDef = pExtentInfo.transferDef;
                            if (!l_TransferDef->failed())
                            {
                                if (!l_TagInfo->canceled())
                                {
                                    //  Handle not marked as canceled
                                    if (!l_TransferDef->canceled())
                                    {
                                        // This extent will be transferred...
                                        // NOTE: We are guaranteed that this work queue is throttled, so
                                        //       even if this bucket has a value of zero, it must be decremented
                                        uint64_t l_Length = (uint64_t)(l_Extent->getLength());
                                        if (bucket >= 0)
                                        {
                                            // No delay
                                            l_MadeBucketModification = true;
                                            bucket -= (int64_t)l_Length;
                                        }
                                        else
                                        {
                                            // With delay
                                            l_Delay = max((Throttle_TimeInterval-Throttle_Timer.getCurrentElapsedTimeInterval())*1000000,(double)0);
                                            LOG(bb,debug) << "processBucket(): Delay for " << (float)l_Delay/1000000.0 << " seconds";
                                        }
                                        if (bucket < 0)
                                        {
                                            workQueueReturnedWithNegativeBucket = 1;
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
        else
        {
            // All extents have been processed.  Set the bucket value to zero.
            if (bucket)
            {
                bucket = 0;
            }
        }
    }

    if (l_MadeBucketModification)
    {
        dump("debug", "processBucket(): After bucket modification: ");
    }
    else
    {
        dump("debug", "processBucket(): No bucket modification: ");
    }

    return l_Delay;
}

void WRKQE::removeWorkItem(WorkID& pWorkItem, const bool pValidateQueue, bool& pLastWorkItemRemoved)
{
    // NOTE: Unless we are debugging a problem, pValidateQueue always comes in as false.
    //       Therefore, we never hit the endOnError() below in production...
    if (pValidateQueue && this != HPWrkQE)
    {
        // NOTE:  This method exists to make sure that the number of work queue entries
        //        matches the number of entries in the vector of extents to transfer
        //        for this LVKey when we are removing the first entry from the work queue.
        LVKey l_Key = (wrkq->front()).getLVKey();
        if (getWrkQ_Size() != getLV_Info()->getNumberOfExtents())
        {
            LOG(bb,error) << "WRKQE::removeWorkItem(): Mismatch between number of elements on work queue (" << getWrkQ_Size() << ") and number of extents in the vector of extents (" << getLV_Info()->getNumberOfExtents() << ") to transfer for " << l_Key;
            endOnError();
        }
    }

    pWorkItem = wrkq->front();
    wrkq->pop();

    // If the queue is now empty, indicate that back to our invoker.
    // NOTE: We do not update the accumulated time here, as we want to
    //       include the time it takes to process this last work item.
    //       The accumulated time will be updated in transferWorker()
    //       after this work item is processed.
    pLastWorkItemRemoved = (getWrkQ_Size() ? false : true);

    return;
};

void WRKQE::setIssuingWorkItem(const int pValue)
{
    issuingWorkItem = pValue;

    return;
}

// NOTE: pLVKey is not currently used, but can come in as null.
void WRKQE::unlock(const LVKey* pLVKey, const char* pMethod)
{
    stringstream errorText;

    if (transferQueueIsLocked())
    {
        // Verify lock protocol
        if (issuingWorkItem)
        {
            FL_Write(FLError, lockPV_TQUnlock1, "WRKQE::unlock: Transfer queue lock being released while a work item is being issued",0,0,0,0);
            errorText << "WRKQE::unlock: Transfer queue lock being released while a work item is being issued";
            LOG_ERROR_TEXT_AND_RAS(errorText, bb.internal.lockprotocol.unlocktq1)
            endOnError();
        }

        pid_t tid = syscall(SYS_gettid);  // \todo eventually remove this.  incurs syscall for each log entry
        if (HPWrkQE == this)
        {
            FL_Write(FLMutex, unlockHPTransQ, "unlockHPTransferQueue.  threadid=%ld",tid,0,0,0);

            if (strstr(pMethod, "%") == NULL)
            {
                if (g_LockDebugLevel == "info")
                {
                    LOG(bb,info) << "HPTRN_Q: UNLOCK <- " << pMethod << ", jobid " << jobid << ", " << lvKey;
                }
                else
                {
                    LOG(bb,debug) << "HPTRN_Q: UNLOCK <- " << pMethod << ", jobid " << jobid << ", " << lvKey;
                }
            }
        }
        else
        {
            FL_Write(FLMutex, unlockTransferQ, "unlockTransferQueue.  threadid=%ld",tid,0,0,0);

            if (strstr(pMethod, "%") == NULL)
            {
                if (g_LockDebugLevel == "info")
                {
                    LOG(bb,info) << "TRNFR_Q: UNLOCK <- " << pMethod << ", jobid " << jobid << ", " << lvKey;
                }
                else
                {
                    LOG(bb,debug) << "TRNFR_Q: UNLOCK <- " << pMethod << ", jobid " << jobid << ", " << lvKey;
                }
            }
        }

        transferQueueLocked = 0;
        pthread_mutex_unlock(&lock_transferqueue);
    }
    else
    {
        FL_Write(FLError, unlockTransferQERROR, "unlockTransferQueue called when lock not owned by thread",0,0,0,0);
        flightlog_Backtrace(__LINE__);
        // For now, also to the console...
        LOG(bb,error) << "TRNFR_Q: Request made to unlock the transfer queue by " << pMethod << ", but the lock is not owned.";
        logBacktrace();
    }

    return;
}
