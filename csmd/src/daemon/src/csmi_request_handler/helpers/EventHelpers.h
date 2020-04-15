/*================================================================================
   
    csmd/src/daemon/src/csmi_request_handler/helpers/EventHelpers.h

  © Copyright IBM Corporation 2015-2017. All Rights Reserved

    This program is licensed under the terms of the Eclipse Public License
    v1.0 as published by the Eclipse Foundation and available at
    http://www.eclipse.org/legal/epl-v10.html

    U.S. Government Users Restricted Rights:  Use, duplication or disclosure
    restricted by GSA ADP Schedule Contract with IBM Corp.
 
================================================================================*/
#ifndef _EVENT_HELPERS_H_
#define _EVENT_HELPERS_H_
#include <stdint.h>

// Infrastructure Event Headers
#include "include/csm_core_event.h"
#include "include/csm_timer_event.h"
#include "include/csm_system_event.h"


// Network Headers
#include "csmnet/src/CPP/csm_network_msg_cpp.h"
#include "csmnet/src/CPP/address.h"
#include "csmnet/src/CPP/csm_message_and_address.h"
#include "csmnet/src/CPP/csm_multicast_message.h"

// TODO Label
#include "include/csm_daemon_state.h"
#include "include/csm_daemon_network_manager.h"
#include "include/csm_bitmap.h"

#include "csmd/src/daemon/src/csmi_request_handler/csmi_base.h"

namespace csm {
namespace daemon {
namespace helper {

/** An invalid state value, represents a non-existent state. */
extern const uint64_t BAD_STATE;
/** An invalid timeout length, represents an invalid timeout time. */
extern const uint64_t BAD_TIMEOUT_LEN;

/** @defgroup Event_Types Infrastructure Event Helpers
 * @{ */

/** Results of a typeid on @ref csm::network::MessageAndAddress. */
extern const std::type_info& NETWORK_EVENT_TYPE;

/** Results of a typeid on @ref csm::db::DBRespContent. */
extern const std::type_info& DB_RESP_EVENT_TYPE;

/** Results of a typeid on @ref csm::db::DBReqContent. */
extern const std::type_info& DB_REQ_EVENT_TYPE;

/** Results of a typeid on @ref csm::daemon::TimerContent. */
extern const std::type_info& TIMER_EVENT_TYPE;

/** Results of a typeid on @ref csm::daemon::SystemContent. */
extern const std::type_info& SYSTEM_EVENT_TYPE;  

/** Results of a typeid on @ref csm::daemon::BitMap. */
extern const std::type_info& ENVIRONMENTAL_EVENT_TYPE;
/** @}*/

/** @defgroup Messages_And_Address Network and Message Helpers */

/** 
 * @brief Convert errno errors to CSM error codes.
 * @param[in] aErrorno The incoming error code.
 *
 * @return The converted error code.
 */
inline csmi_cmd_err_t GetCmdErr(int aErrorno)
{
    csmi_cmd_err_t newErrorNo = (csmi_cmd_err_t)aErrorno;
    switch(aErrorno)
    {
        case ECOMM:
            newErrorNo = CSMERR_SENDRCV_ERROR;
            break;
        case ETIMEDOUT:
            newErrorNo = CSMERR_TIMEOUT;
            break;
        case EPERM:
            newErrorNo = CSMERR_PERM;
            break;
        default:
        ;
    }
    
    return newErrorNo;
}

/** @brief Create a new Network Message.
 *  @ingroup Messages_And_Address
 * 
 *  @param[in]  msg    An existing message with a command, priority, src, and dst to be 
 *                       copied into the message generated by this function.
 *  @param[in]  buf    A byte array containing the information to send in the message. 
 *  @param[in]  bufLen The length of the buffer specifed in @p buf.
 *  @param[in]  flags  The flags to set in the created message.
 *  @param[out] rspMsg The message produced by this function
 * 
 * @return True if the returned message has a valid checksum and was sucessfully initialized.
 */
inline bool CreateNetworkMessage(
                        const csm::network::Message& msg,
                        const char *buf, 
                        const uint32_t bufLen, 
                        const uint8_t flags,
                        csm::network::Message &rspMsg)
{
    //uint64_t messageId = (msgId == nullptr) ?  msg.GetMessageID() : ( *msgId );
    ///< @todo is there a better way to do this?
    bool hdrvalid = rspMsg.Init(
                        msg.GetCommandType(), 
                        flags, 
                        msg.GetPriority(), 
                        msg.GetMessageID(),
                        msg.GetSrcAddr(),
                        msg.GetDstAddr(), 
                        msg.GetUserID(), 
                        msg.GetGroupID(),
                        std::string(buf, bufLen),
                        msg.GetReservedID());

    if (!hdrvalid)
      LOG(csmapi, error) << "Message.Init returned FALSE!";

    return hdrvalid;
}

/** @brief Create a returned Address based on the input address.
 *  @ingroup Messages_And_Address
 *
 *  @note The returned address should be freed by the invoker.
 *
 *  @param[in] reqAddress The Address of a message to reply to. 
 *  @return A reply address appropriate to the address type. If the address was not
 *           a MQTT Topic or Local address a nullptr is returned.  
 */
inline csm::network::Address_sptr CreateReplyAddress(const csm::network::Address* reqAddress)
{
    csm::network::Address_sptr rspAddress = nullptr; //< Smart Pointer to the reply address.
    csm::network::AddressType addr_type = reqAddress->GetAddrType(); //< Cache the address type.
  
    switch ( addr_type )
    {
        case csm::network::CSM_NETWORK_TYPE_AGGREGATOR:
        {
            const csm::network::AddressAggregator* aggAddr = 
                dynamic_cast<const csm::network::AddressAggregator*>( reqAddress );

            if ( aggAddr )
                rspAddress = std::make_shared<csm::network::AddressAggregator>( *aggAddr );
            break;
        }

        case csm::network::CSM_NETWORK_TYPE_UTILITY:
        {
            const csm::network::AddressUtility* utilAddr = 
                dynamic_cast<const csm::network::AddressUtility*>( reqAddress );

            if ( utilAddr )
                rspAddress = std::make_shared<csm::network::AddressUtility>( *utilAddr );
            break;
        }
        case csm::network::CSM_NETWORK_TYPE_LOCAL:
        {
            const csm::network::AddressUnix* unixAddr = 
                dynamic_cast<const csm::network::AddressUnix*>( reqAddress );

            if ( unixAddr )
                rspAddress = std::make_shared<csm::network::AddressUnix>( *unixAddr );
            break;
        }
        case csm::network::CSM_NETWORK_TYPE_PTP:
        {
            const csm::network::AddressPTP* ptpAddr = 
                dynamic_cast<const csm::network::AddressPTP*>( reqAddress );
            
            if ( ptpAddr )
                rspAddress = std::make_shared<csm::network::AddressPTP>( *ptpAddr );
            break; 
        }
        case csm::network::CSM_NETWORK_TYPE_ABSTRACT:                                           
        {
            const csm::network::AddressAbstract* absAddr = 
                dynamic_cast<const csm::network::AddressAbstract*>( reqAddress );

            if ( absAddr )
                rspAddress = std::make_shared<csm::network::AddressAbstract>( *absAddr );
            break;
        }
        default:
            LOG(csmapi, warning) << "csm::daemon::helper::CreateReplyAddress():"
                " Unexpected Address Type: " << addr_type;
            break;
    }

    return rspAddress;
}

/** @brief Create a NetworkEvent using a MessageAndAddress and an optional context.
 *  @ingroup Event_Types
 *  
 *  @param[in] content  An already assembled message.
 *  @param[in] aContext The context to be used in the event.
 */
inline csm::daemon::NetworkEvent* CreateNetworkEvent (
                const csm::network::MessageAndAddress &content,
                const csm::daemon::EventContext_sptr aContext = nullptr )
{
    return ( new csm::daemon::NetworkEvent(
                    content, csm::daemon::EVENT_TYPE_NETWORK, aContext) );
}

/** @brief Create a NetworkEvent using the Message, Address and an optional context.
 *  @ingroup Event_Types
 *
 *  @param[in] msg      A message for the event.
 *  @param[in] addr     An address to send the event to.
 *  @param[in] aContext The context to supply to the new event (optional).
 */
inline csm::daemon::NetworkEvent* CreateNetworkEvent(
                const csm::network::Message &msg, 
                const csm::network::Address_sptr addr,
                const csm::daemon::EventContext_sptr aContext = nullptr )
{
    csm::network::MessageAndAddress content( msg, addr );
    return CreateNetworkEvent(content, aContext);
}

/** @brief Create a new NetworkEvent and construct a MessageAndAdress using the input.
 *  @ingroup Event_Types
 *
 *  @param[in] payload    A byte array containing the information to send.
 *  @param[in] payloadLen The length of payload.
 *  @param[in] flags      The flags to be set in the returned Message header.
 *  @param[in] msg        An existing message with a command, priority, src, and dst to be 
 *                          copied into the message generated by this function.
 *  @param[in] addr       The destination address for this Event.
 *  @param[in] aContext   A context to associate this network event with.
 *  @param[in] isError    Sets the error flag in the message. FIXME THIS DOESN'T HAPPEN!
 *  
 *  @return Return the NetworkEvent. If the Message is not created, nullptr will be returned.
 */
inline csm::daemon::NetworkEvent* CreateNetworkEvent(
                const char *payload, 
                const uint32_t payloadLen, 
                const uint32_t flags,
                const csm::network::Message &msg, 
                const csm::network::Address_sptr addr,
                const csm::daemon::EventContext_sptr aContext,
                bool isError = false )
{
    // Create the Network message with the input.
    csm::network::Message rspMsg;
    csm::daemon::NetworkEvent *netEvent = nullptr;
    bool ret = csm::daemon::helper::CreateNetworkMessage(msg, payload, payloadLen, flags, rspMsg);
  
    // If the message creation succeeded generate the event.
    if (ret)
    {
        csm::network::MessageAndAddress content( rspMsg, addr );
    
        netEvent = new csm::daemon::NetworkEvent(content,
                          csm::daemon::EVENT_TYPE_NETWORK, aContext);
    }
  
    return netEvent;
}

/** @brief Create a DBReqEvent containing sqlStmt and the context
 *  @ingroup Event_Types
 *
 *  @param[in] sqlStmt The SQL Statement to push to the database.
 *  @param[in] context The Context of a handler invoking this Create.
 *  @param[in] dbConn  A Connection to the Database, optional.
 *
 *  @return A DBReqEvent with the supplied SQL Statement.
 */
inline csm::daemon::DBReqEvent* CreateDBReqEvent(
                    const std::string &sqlStmt,
  				    const csm::daemon::EventContext_sptr context,
                    const csm::db::DBConnection_sptr dbConn = nullptr )
{
    csm::db::DBReqContent dbcontent(sqlStmt, dbConn);
    return new csm::daemon::DBReqEvent(
                 dbcontent, csm::daemon::EVENT_TYPE_DB_Request, context);
}

/** @brief Create a DBReqEvent containing a parameterized sql statement, param list and context.
 *  @ingroup Event_Types
 *
 *  @param[in] context The Context of a handler invoking this Create.
 *  @param[in] dbConn  A Connection to the Database, optional.
 *
 *  @return A DBReqEvent with the supplied SQL Statement.
 */
inline csm::daemon::DBReqEvent* CreateDBReqEvent(
                    csm::db::DBReqContent dbcontent,
  				    const csm::daemon::EventContext_sptr context,
                    const csm::db::DBConnection_sptr dbConn = nullptr )
{
    return new csm::daemon::DBReqEvent(
                 dbcontent, csm::daemon::EVENT_TYPE_DB_Request, context);
}


/** @brief Create a Timer Event with a lifespan of @p aMilliSeconds.
 *  @ingroup Event_Types
 *
 *  @param[in] aMilliSeconds The length of the timer in Milliseconds.
 *  @param[in] aTargetState  The state that the timer is supposed to be handled by.
 *  @param[in] aContext      The context of the invoking handler.
 *
 *  @return A TimerEvent of the specified length.
 */
inline  csm::daemon::TimerEvent *CreateTimerEvent( 
                        const uint64_t aMilliSeconds, 
                        const uint64_t aTargetStateId,
                        const csm::daemon::EventContext_sptr aContext )
{
    csm::daemon::TimerContent content( aMilliSeconds, aTargetStateId );
    return new csm::daemon::TimerEvent( content, csm::daemon::EVENT_TYPE_TIMER, aContext );
}

/** @brief Create an error reply NetworkEvent at Master
 *  @ingroup Event_Types
 *  @note aEvent must be a NetworkEvent
 * 
 *  @param[in] buffer        A buffer containing the error object to be sent over the wire.
 *  @param[in] buffer_length The length of the buffer contained in @p buffer.
 *  @param[in] reqContent Contains a _Msg and Address used in constructing 
 *                          the sessage sent.
 *
 *  @return An ErrorEvent with the supplied details, a nullptr is returned if the 
 *          Message is unable to be constructed.
 */
inline csm::daemon::NetworkEvent* CreateErrorEvent(
                        char* buffer, uint32_t buffer_length,
                        const csm::daemon::CoreEvent& aEvent)
{
    uint8_t   flags         = CSM_HEADER_RESP_BIT | CSM_HEADER_ERR_BIT; 
    csm::daemon::NetworkEvent *ev = (csm::daemon::NetworkEvent *)&aEvent;
    csm::network::MessageAndAddress reqContent = ev->GetContent();

    // Generate the message.
    csm::network::Message rsp_msg;    
    bool ret  = csm::daemon::helper::CreateNetworkMessage(
                      reqContent._Msg, buffer, buffer_length, flags, rsp_msg);
    
    // FIXME THIS CAN SEGFAULT!!!
    // Determine the target address for this event.
    csm::network::Address_sptr rsp_address = csm::daemon::helper::CreateReplyAddress(
                      reqContent.GetAddr().get());
    csm::daemon::NetworkEvent *net_event   = nullptr; 

    // Check to see if it's a self send.
    bool isSelf = rsp_address->GetAddrType() == csm::network::CSM_NETWORK_TYPE_ABSTRACT &&
        ( std::dynamic_pointer_cast<csm::network::AddressAbstract>(rsp_address)->_AbstractName ==
            csm::network::ABSTRACT_ADDRESS_SELF );

    // If everything has been sucessfully created generate the event.
    if ( ret && rsp_address && !isSelf )
    {
          csm::network::MessageAndAddress errContent ( rsp_msg, rsp_address );
          net_event = new csm::daemon::NetworkEvent(errContent,
                              csm::daemon::EVENT_TYPE_NETWORK, nullptr);
    }
    
    return net_event;
}

/** @brief Create an error reply NetworkEvent at Master
 *  @ingroup Event_Types
 *  @note aEvent must be a NetworkEvent
 * 
 *  @param[in] errcode    The errcode needs to be added in the Message payload
 *  @param[in] errmsg     The errmsg needs to be added in the Message payload
 *  @param[in] reqContent Contains a _Msg and Address used in constructing 
 *                          the sessage sent.
 *
 *  @return An ErrorEvent with the supplied details, a nullptr is returned if the 
 *          Message is unable to be constructed.
 */
inline csm::daemon::NetworkEvent* CreateErrorEvent(
                        const int errcode, 
                        const std::string& errmsg,
                        csm::network::MessageAndAddress &reqContent)
{
    // Generate the buffer for the message.
    uint8_t   flags         = CSM_HEADER_RESP_BIT | CSM_HEADER_ERR_BIT; 
    uint32_t  buffer_length = 0;                                        
    char     *buffer        = csmi_err_pack(errcode, errmsg.c_str(), &buffer_length);
    
    // Generate the message.
    csm::network::Message rsp_msg;    
    bool ret  = csm::daemon::helper::CreateNetworkMessage(
                      reqContent._Msg, buffer, buffer_length, flags, rsp_msg);
    if (buffer) free(buffer);
    
    
    // FIXME THIS CAN SEGFAULT!!!
    // Determine the target address for this event.
    csm::network::Address_sptr rsp_address = csm::daemon::helper::CreateReplyAddress(
                      reqContent.GetAddr().get());
    csm::daemon::NetworkEvent *net_event   = nullptr; 

    // Check to see if it's a self send.
    bool isSelf = rsp_address->GetAddrType() == csm::network::CSM_NETWORK_TYPE_ABSTRACT &&
        ( std::dynamic_pointer_cast<csm::network::AddressAbstract>(rsp_address)->_AbstractName ==
            csm::network::ABSTRACT_ADDRESS_SELF);
    
    // If everything has been sucessfully created generate the event.
    if ( ret && rsp_address && !isSelf )
    {
          csm::network::MessageAndAddress errContent ( rsp_msg, rsp_address );
          net_event = new csm::daemon::NetworkEvent(errContent,
                              csm::daemon::EVENT_TYPE_NETWORK, nullptr);
    }
    
    return net_event;
}

/** @brief Creates an error event using an existing event to generate the MessageAndAddress component.
 *  @ingroup Event_Types
 *
 *  @param[in] errcode The errcode needs to be added in the Message payload
 *  @param[in] errmsg  The errmsg needs to be added in the Message payload
 *  @param[in] aEvent  The Event that spawned the ErrorEvent. The MessageAndAddress of
 *                      this event is used to populate the ErrorEvent.
 *
 *  @return An ErrorEvent with the supplied details, a nullptr is returned if the
 *              Message is unable to be constructed.
 */
inline csm::daemon::NetworkEvent* CreateErrorEvent(
                        const int errcode, 
                        const std::string& errmsg,
                        const csm::daemon::CoreEvent& aEvent)
{
    csm::daemon::NetworkEvent *ev = (csm::daemon::NetworkEvent *)&aEvent;
    csm::network::MessageAndAddress reqContent = ev->GetContent();

    return CreateErrorEvent(errcode, errmsg, reqContent);
}
/**
 *@brief Creates an error event for a PTP connection.
 * TODO document and cleanup!
 */
inline csm::daemon::NetworkEvent* CreateErrorEventAgg(
    const int errcode, 
    const std::string& errmsg,
    const csm::daemon::CoreEvent& aEvent,
    const csm::daemon::EventContext_sptr aContext)
{
    csm::daemon::NetworkEvent *ev = (csm::daemon::NetworkEvent *)&aEvent;
    csm::network::MessageAndAddress reqContent = ev->GetContent();
    csm::daemon::NetworkEvent *netEvent = nullptr;

    uint32_t bufLen = 0;
    char*    buf    = csmi_err_pack(errcode, errmsg.c_str(), &bufLen); 
    uint8_t flags = CSM_HEADER_RESP_BIT | CSM_HEADER_ERR_BIT;


    if ( buf )
    {
        csm::network::Message rspMsg;
        bool ret = CreateNetworkMessage(reqContent._Msg, buf, bufLen, flags, rspMsg);
        free(buf);
        
        // If the context exists get and cast its handler, otherwise set it to a nullptr
        CSMI_BASE* handler = aContext ? 
            static_cast<CSMI_BASE*>( aContext->GetEventHandler() ) : nullptr;

        if ( handler && ret )
        {
          csm::network::MessageAndAddress errContent ( 
                                            rspMsg, 
                                            handler->GetAbstractAggregator());
          
          netEvent = new csm::daemon::NetworkEvent(errContent,
                                csm::daemon::EVENT_TYPE_NETWORK, nullptr);
        }
    }
    return netEvent;
}

/** @defgroup Master_Daemon_Helpers Helper functions for the Master Daemon. */


/**
 * @brief Create a reply NetworkEvent at Master
 * @note reqEvent has to be a NetworkEvent
 * 
 * @param[in] payload The byte array in the Message payload
 * @param[in] payloadLen The length of the byte array
 * @param[in] reqEvent The request NetworkEvent. Will use its Message header to construct a reply
 * @param[in] aContext The context will be associated with the reply
 * @param[in] isError if true, the error flag in the reply will be set.
 * 
 * @ingroup Master_Daemon_Helpers
 * @return Return the Reply NetworkEvent at Master. Nullptr if an error occurs
*/
inline csm::daemon::NetworkEvent* CreateReplyNetworkEvent(
    const char *payload, 
    const uint32_t payloadLen,
    const csm::daemon::CoreEvent& reqEvent, 
    const csm::daemon::EventContext_sptr aContext,
    bool isError = false)
{
    // Get the data from the request to fuel the response.
    csm::daemon::NetworkEvent *netEvent        = nullptr;
    csm::daemon::NetworkEvent *ev              = (csm::daemon::NetworkEvent *)&reqEvent; 
    csm::network::MessageAndAddress reqContent = ev->GetContent();          
    csm::network::Address_sptr rspAddress      = CreateReplyAddress(reqContent.GetAddr().get());     
    
    // Possible one line reduction: 
    // uint8_t flags = CSM_HEADER_RESP_BIT | (isError * CSM_HEADER_ERR_BIT);
    uint8_t flags = CSM_HEADER_RESP_BIT;
    if (isError) 
        flags |= CSM_HEADER_ERR_BIT;

    // Check to see if it's a self send.
    bool isSelf = rspAddress->GetAddrType() == csm::network::CSM_NETWORK_TYPE_ABSTRACT &&
        ( std::dynamic_pointer_cast<csm::network::AddressAbstract>(rspAddress)->_AbstractName ==
            csm::network::ABSTRACT_ADDRESS_SELF);
      
    // If there is a valid address to send this to, create the event.
    if ( rspAddress && !isSelf )
        netEvent = csm::daemon::helper::CreateNetworkEvent(payload, payloadLen, flags, 
            reqContent._Msg, rspAddress, aContext, isError);

    return netEvent;
}

// TODO revise Documentation.
/**
 * @brief Create a NetworkEvent with the original NetworkEvent and a list of string at Master
 * @ingroup Master_Daemon_Helpers
 *
 * @param[in] msg The message to multicast.
 * @param[in] list A list of nodes to multicast to.
 * @param[in,out] aContext A context containing contextual details about the invoking hander.
 *
 * @return Return a Multi-cast NetworkEvent, nullptr if failed.
 */
inline csm::daemon::NetworkEvent* CreateMulticastEvent(
    const csm::network::Message &msg,
    const std::vector<std::string> &list,
    const csm::daemon::EventContext_sptr aContext)
{
    csm::network::Message outMsg;

    /// @todo Is there a better way to do this?
    // If the context exists get and cast its handler, otherwise set it to a nullptr
    CSMI_BASE* handler = aContext ? 
        static_cast<CSMI_BASE*>(aContext->GetEventHandler()) : nullptr;

    // If the handler is null or a message is unable to be created, return a nullptr.
    if ( !handler ||  !csm::network::CreateMulticastMessage(msg, list, outMsg) ) 
    {
        return nullptr;
    }

    // master should send this message to aggregators using CSM/MASTER/QUERY mqtt topic
    csm::network::MessageAndAddress content( outMsg, handler->GetAbstractAggregator() );

    csm::daemon::NetworkEvent *netEvent = new csm::daemon::NetworkEvent(content,
                            csm::daemon::EVENT_TYPE_NETWORK, aContext);
      
    return netEvent;
}


/** @attention FIXME This is not OOP, we should override the copy constructor of CoreEvent? -John Dunham (jdunham@us.ibm)
 * @brief Make a copy of a CoreEvent
 *
 * @param aEvent The event to be copied.
 */
inline csm::daemon::CoreEvent* CopyEvent(const csm::daemon::CoreEvent &aEvent)
{
    csm::daemon::CoreEvent *ret = nullptr;
    // NETWORK_EVENT_TYPE
    if (aEvent.HasSameContentTypeAs(csm::daemon::helper::NETWORK_EVENT_TYPE))
        ret = new csm::daemon::NetworkEvent( ((csm::daemon::NetworkEvent *) &aEvent)->GetContent(),
                    aEvent.GetEventType(), aEvent.GetEventContext() );

    else if (aEvent.HasSameContentTypeAs(csm::daemon::helper::DB_REQ_EVENT_TYPE))
        ret = new csm::daemon::DBReqEvent( ((csm::daemon::DBReqEvent *) &aEvent)->GetContent(),
                    aEvent.GetEventType(), aEvent.GetEventContext() );

    else if (aEvent.HasSameContentTypeAs(csm::daemon::helper::DB_RESP_EVENT_TYPE))
        ret = new csm::daemon::DBRespEvent( ((csm::daemon::DBRespEvent *) &aEvent)->GetContent(),
                    aEvent.GetEventType(), aEvent.GetEventContext() );

    else if (aEvent.HasSameContentTypeAs(csm::daemon::helper::TIMER_EVENT_TYPE))
        ret = new csm::daemon::TimerEvent( ((csm::daemon::TimerEvent *) &aEvent)->GetContent(),
                    aEvent.GetEventType(), aEvent.GetEventContext() );

    else if (aEvent.HasSameContentTypeAs(csm::daemon::helper::SYSTEM_EVENT_TYPE))
        ret = new csm::daemon::SystemEvent( ((csm::daemon::SystemEvent *) &aEvent)->GetContent(),
                    aEvent.GetEventType(), aEvent.GetEventContext() );
    return ret;
  
}

/** 
 * @brief Creates an event registering the start of a job, to be invoked on the compute node.
 * @param[in] aContext The context to spawn the event from.
 *
 * @return A System Event with the JOB_START content.
 */
inline csm::daemon::SystemEvent* CreateJobStartSystemEvent( 
    const csm::daemon::EventContext_sptr aContext )
{
    csm::daemon::SystemContent content( csm::daemon::SystemContent::JOB_START );
    
    return new csm::daemon::SystemEvent( content, csm::daemon::EVENT_TYPE_SYSTEM, aContext );
}

/**
 * @brief Creates an event registering the end of a job, to be invoked on the compute node.
 * @param[in] aContext The context to spawn the event from.
 *
 * @return A System Event with the JOB_END content.
 */
inline csm::daemon::SystemEvent* CreateJobEndSystemEvent( 
    const csm::daemon::EventContext_sptr aContext )
{
    csm::daemon::SystemContent content(csm::daemon::SystemContent::JOB_END);

    return new csm::daemon::SystemEvent( content, csm::daemon::EVENT_TYPE_SYSTEM, aContext );
}

inline csm::daemon::NetworkEvent* CreateRasEventMessage(const std::string &msg_id, 
                                                        const std::string &location_name,
                                                        const std::string &raw_data,
                                                        const std::string &kvcsv,
                                                        const csm::network::Address_sptr aAddr)
{
    // Set up the buffer.
    char* buffer = nullptr;
    uint32_t bufferLength = 0;

    // ----------------------------------------------------------------------------
    // Generate a timestamp for this event 
    // Example format:
    // 2016-05-12 15:12:11.799506
    const int32_t TS_BUFF_SIZE(80);
    const int32_t TS_USEC_SIZE(120);
    char time_stamp_buffer[TS_BUFF_SIZE];
    char time_stamp_with_usec[TS_USEC_SIZE];

    struct timeval now_tv;
    time_t rawtime;
    struct tm *info;

    gettimeofday(&now_tv, NULL);
    rawtime = now_tv.tv_sec;
    info = localtime( &rawtime );

    strftime(time_stamp_buffer, TS_BUFF_SIZE, "%Y-%m-%d %H:%M:%S", info);
    snprintf(time_stamp_with_usec, TS_USEC_SIZE, "%s.%06lu", time_stamp_buffer, now_tv.tv_usec);
    // ----------------------------------------------------------------------------

    // ----------------------------------------------------------------------------
    // Build and serialize the struct.
    csm_ras_event_create_input_t rargs;
    // TODO versioning.

    csm_init_struct_versioning(&rargs);
    rargs.msg_id        = strdup(msg_id.c_str());
    rargs.time_stamp    = strdup(time_stamp_with_usec);
    rargs.location_name = strdup(location_name.c_str());
    rargs.raw_data      = strdup(raw_data.c_str());
    rargs.kvcsv         = strdup(kvcsv.c_str());
    
    csm_serialize_struct(csm_ras_event_create_input_t, &rargs, &buffer, &bufferLength );
    csm_free_struct(csm_ras_event_create_input_t, rargs);
    // ----------------------------------------------------------------------------

    if ( buffer )
    {
        std::string payload(buffer, bufferLength);
        free(buffer);

        uint8_t flags = CSM_HEADER_INT_BIT;
        csm::network::Message rspMsg;
        bool hdrvalid = rspMsg.Init(CSM_CMD_ras_event_create,
                            flags,
                            CSM_PRIORITY_DEFAULT,               // aPriority
                            0,                                  // aMessageID
                            1,                                  // aSrcAddr
                            1,                                  // aDstAddr
                            geteuid(), getegid(),               // user, group ID
                            payload);

        if (!hdrvalid) {
            LOG(csmd, error) << __FILE__ << ":" << __LINE__ << 
                " " << "Message.Init returned FALSE!";
            return NULL;
        } else {
            csm::network::MessageAndAddress netcontent( rspMsg, aAddr );
            csm::daemon::NetworkEvent *netEvent = 
                new csm::daemon::NetworkEvent(netcontent,csm::daemon::EVENT_TYPE_NETWORK, NULL);
            return netEvent;
        }
    }
    return NULL;
}

} // End namespace helpers
} // End namespace daemon
} // End namespace csm


#endif
