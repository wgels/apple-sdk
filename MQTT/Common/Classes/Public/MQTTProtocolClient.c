#include <stdlib.h>             // C Standard
#include <stdbool.h>            // C Standard
#include "MQTTProtocolClient.h" // MQTT (Public)
#if !defined(NO_PERSISTENCE)
    #include "MQTTPersistence.h"// MQTT (Public)
#endif
#include "SocketBuffer.h"       // MQTT (Web)
#include "StackTrace.h"         // MQTT (Utilities)
#include "Heap.h"               // MQTT (Utilities)

#pragma mark - Definitions

#if !defined(min)
#define min(A,B) ( (A) < (B) ? (A):(B))
#endif

#pragma mark - Variables

extern MQTTProtocol state;
extern ClientStates* bstate;


#pragma mark - Private prototypes

void MQTTProtocol_storeQoS0(Clients* pubclient, Publish* publish);
int MQTTProtocol_startPublishCommon(Clients* pubclient, Publish* publish, int qos, int retained);
void MQTTProtocol_retries(time_t now, Clients* client, int regardless);

// MQTTAsync private functions
void Protocol_processPublication(Publish* publish, Clients* client);
void MQTTProtocol_closeSession(Clients* client, int sendwill);

#pragma mark - Public API

int MQTTProtocol_startPublish(Clients* pubclient, Publish* publish, int qos, int retained, Messages** mm)
{
    Publish p = *publish;
    int rc = 0;
    
    FUNC_ENTRY;
    if (qos > 0)
    {
        *mm = MQTTProtocol_createMessage(publish, mm, qos, retained);
        ListAppend(pubclient->outboundMsgs, *mm, (*mm)->len);
        /* we change these pointers to the saved message location just in case the packet could not be written
         entirely; the socket buffer will use these locations to finish writing the packet */
        p.payload = (*mm)->publish->payload;
        p.topic = (*mm)->publish->topic;
    }
    rc = MQTTProtocol_startPublishCommon(pubclient, &p, qos, retained);
    FUNC_EXIT_RC(rc);
    return rc;
}

Messages* MQTTProtocol_createMessage(Publish* publish, Messages **mm, int qos, int retained)
{
    Messages* m = malloc(sizeof(Messages));
    
    FUNC_ENTRY;
    m->len = sizeof(Messages);
    if (*mm == NULL || (*mm)->publish == NULL)
    {
        size_t len1;
        *mm = m;
        m->publish = MQTTProtocol_storePublication(publish, &len1);
        m->len += len1;
    }
    else
    {
        ++(((*mm)->publish)->refcount);
        m->publish = (*mm)->publish;
    }
    m->msgid = publish->msgId;
    m->qos = qos;
    m->retain = retained;
    time(&(m->lastTouch));
    if (qos == 2)
        m->nextMessageType = PUBREC;
    FUNC_EXIT;
    return m;
}

Publications* MQTTProtocol_storePublication(Publish* publish, size_t* len)
{
    Publications* p = malloc(sizeof(Publications));
    
    FUNC_ENTRY;
    p->refcount = 1;
    
    *len = strlen(publish->topic)+1;
    if (Heap_findItem(publish->topic))
        p->topic = publish->topic;
    else
    {
        p->topic = malloc(*len);
        strcpy(p->topic, publish->topic);
    }
    *len += sizeof(Publications);
    
    p->topiclen = publish->topiclen;
    p->payloadlen = publish->payloadlen;
    p->payload = malloc(publish->payloadlen);
    memcpy(p->payload, publish->payload, p->payloadlen);
    *len += publish->payloadlen;
    
    ListAppend(&(state.publications), p, *len);
    FUNC_EXIT;
    return p;
}

void MQTTProtocol_removePublication(Publications* p)
{
    FUNC_ENTRY;
    if (--(p->refcount) == 0)
    {
        free(p->payload);
        free(p->topic);
        ListRemove(&(state.publications), p);
    }
    FUNC_EXIT;
}

int MQTTProtocol_assignMsgId(Clients* client)
{
    int start_msgid = client->msgID;
    int msgid = start_msgid;
    
    FUNC_ENTRY;
    msgid = (msgid == MAX_MSG_ID) ? 1 : msgid + 1;
    while (ListFindItem(client->outboundMsgs, &msgid, messageIDCompare) != NULL)
    {
        msgid = (msgid == MAX_MSG_ID) ? 1 : msgid + 1;
        if (msgid == start_msgid)
        { /* we've tried them all - none free */
            msgid = 0;
            break;
        }
    }
    if (msgid != 0)
        client->msgID = msgid;
    FUNC_EXIT_RC(msgid);
    return msgid;
}

int MQTTProtocol_handlePublishes(void* pack, int sock)
{
    Publish* publish = (Publish*)pack;
    Clients* client = NULL;
    char* clientid = NULL;
    int rc = TCPSOCKET_COMPLETE;
    
    FUNC_ENTRY;
    client = (Clients*)(ListFindItem(bstate->clients, &sock, clientSocketCompare)->content);
    clientid = client->clientID;
    Log(LOG_PROTOCOL, 11, NULL, sock, clientid, publish->msgId, publish->header.bits.qos,
        publish->header.bits.retain, min(20, publish->payloadlen), publish->payload);
    
    if (publish->header.bits.qos == 0)
        Protocol_processPublication(publish, client);
    else if (publish->header.bits.qos == 1)
    {
        /* send puback before processing the publications because a lot of return publications could fill up the socket buffer */
        rc = MQTTPacket_send_puback(publish->msgId, &client->net, client->clientID);
        /* if we get a socket error from sending the puback, should we ignore the publication? */
        Protocol_processPublication(publish, client);
    }
    else if (publish->header.bits.qos == 2)
    {
        /* store publication in inbound list */
        size_t len;
        ListElement* listElem = NULL;
        Messages* m = malloc(sizeof(Messages));
        Publications* p = MQTTProtocol_storePublication(publish, &len);
        m->publish = p;
        m->msgid = publish->msgId;
        m->qos = publish->header.bits.qos;
        m->retain = publish->header.bits.retain;
        m->nextMessageType = PUBREL;
        if ( ( listElem = ListFindItem(client->inboundMsgs, &(m->msgid), messageIDCompare) ) != NULL )
        {   /* discard queued publication with same msgID that the current incoming message */
            Messages* msg = (Messages*)(listElem->content);
            MQTTProtocol_removePublication(msg->publish);
            ListInsert(client->inboundMsgs, m, sizeof(Messages) + len, listElem);
            ListRemove(client->inboundMsgs, msg);
        } else
            ListAppend(client->inboundMsgs, m, sizeof(Messages) + len);
        rc = MQTTPacket_send_pubrec(publish->msgId, &client->net, client->clientID);
        publish->topic = NULL;
    }
    MQTTPacket_freePublish(publish);
    FUNC_EXIT_RC(rc);
    return rc;
}

int MQTTProtocol_handlePubacks(void* pack, int sock)
{
    Puback* puback = (Puback*)pack;
    Clients* client = NULL;
    int rc = TCPSOCKET_COMPLETE;
    
    FUNC_ENTRY;
    client = (Clients*)(ListFindItem(bstate->clients, &sock, clientSocketCompare)->content);
    Log(LOG_PROTOCOL, 14, NULL, sock, client->clientID, puback->msgId);
    
    /* look for the message by message id in the records of outbound messages for this client */
    if (ListFindItem(client->outboundMsgs, &(puback->msgId), messageIDCompare) == NULL)
        Log(TRACE_MIN, 3, NULL, "PUBACK", client->clientID, puback->msgId);
    else
    {
        Messages* m = (Messages*)(client->outboundMsgs->current->content);
        if (m->qos != 1)
            Log(TRACE_MIN, 4, NULL, "PUBACK", client->clientID, puback->msgId, m->qos);
        else
        {
            Log(TRACE_MIN, 6, NULL, "PUBACK", client->clientID, puback->msgId);
#if !defined(NO_PERSISTENCE)
            rc = MQTTPersistence_remove(client, PERSISTENCE_PUBLISH_SENT, m->qos, puback->msgId);
#endif
            MQTTProtocol_removePublication(m->publish);
            ListRemove(client->outboundMsgs, m);
        }
    }
    free(pack);
    FUNC_EXIT_RC(rc);
    return rc;
}

int MQTTProtocol_handlePubrecs(void* pack, int sock)
{
    Pubrec* pubrec = (Pubrec*)pack;
    Clients* client = NULL;
    int rc = TCPSOCKET_COMPLETE;
    
    FUNC_ENTRY;
    client = (Clients*)(ListFindItem(bstate->clients, &sock, clientSocketCompare)->content);
    Log(LOG_PROTOCOL, 15, NULL, sock, client->clientID, pubrec->msgId);
    
    /* look for the message by message id in the records of outbound messages for this client */
    client->outboundMsgs->current = NULL;
    if (ListFindItem(client->outboundMsgs, &(pubrec->msgId), messageIDCompare) == NULL)
    {
        if (pubrec->header.bits.dup == 0)
            Log(TRACE_MIN, 3, NULL, "PUBREC", client->clientID, pubrec->msgId);
    }
    else
    {
        Messages* m = (Messages*)(client->outboundMsgs->current->content);
        if (m->qos != 2)
        {
            if (pubrec->header.bits.dup == 0)
                Log(TRACE_MIN, 4, NULL, "PUBREC", client->clientID, pubrec->msgId, m->qos);
        }
        else if (m->nextMessageType != PUBREC)
        {
            if (pubrec->header.bits.dup == 0)
                Log(TRACE_MIN, 5, NULL, "PUBREC", client->clientID, pubrec->msgId);
        }
        else
        {
            rc = MQTTPacket_send_pubrel(pubrec->msgId, 0, &client->net, client->clientID);
            m->nextMessageType = PUBCOMP;
            time(&(m->lastTouch));
        }
    }
    free(pack);
    FUNC_EXIT_RC(rc);
    return rc;
}

int MQTTProtocol_handlePubrels(void* pack, int sock)
{
    Pubrel* pubrel = (Pubrel*)pack;
    Clients* client = NULL;
    int rc = TCPSOCKET_COMPLETE;
    
    FUNC_ENTRY;
    client = (Clients*)(ListFindItem(bstate->clients, &sock, clientSocketCompare)->content);
    Log(LOG_PROTOCOL, 17, NULL, sock, client->clientID, pubrel->msgId);
    
    /* look for the message by message id in the records of inbound messages for this client */
    if (ListFindItem(client->inboundMsgs, &(pubrel->msgId), messageIDCompare) == NULL)
    {
        if (pubrel->header.bits.dup == 0)
            Log(TRACE_MIN, 3, NULL, "PUBREL", client->clientID, pubrel->msgId);
        else
        /* Apparently this is "normal" behaviour, so we don't need to issue a warning */
            rc = MQTTPacket_send_pubcomp(pubrel->msgId, &client->net, client->clientID);
    }
    else
    {
        Messages* m = (Messages*)(client->inboundMsgs->current->content);
        if (m->qos != 2)
            Log(TRACE_MIN, 4, NULL, "PUBREL", client->clientID, pubrel->msgId, m->qos);
        else if (m->nextMessageType != PUBREL)
            Log(TRACE_MIN, 5, NULL, "PUBREL", client->clientID, pubrel->msgId);
        else
        {
            Publish publish;
            
            /* send pubcomp before processing the publications because a lot of return publications could fill up the socket buffer */
            rc = MQTTPacket_send_pubcomp(pubrel->msgId, &client->net, client->clientID);
            publish.header.bits.qos = m->qos;
            publish.header.bits.retain = m->retain;
            publish.msgId = m->msgid;
            publish.topic = m->publish->topic;
            publish.topiclen = m->publish->topiclen;
            publish.payload = m->publish->payload;
            publish.payloadlen = m->publish->payloadlen;
            Protocol_processPublication(&publish, client);
#if !defined(NO_PERSISTENCE)
            rc += MQTTPersistence_remove(client, PERSISTENCE_PUBLISH_RECEIVED, m->qos, pubrel->msgId);
#endif
            ListRemove(&(state.publications), m->publish);
            ListRemove(client->inboundMsgs, m);
            ++(state.msgs_received);
        }
    }
    free(pack);
    FUNC_EXIT_RC(rc);
    return rc;
}

int MQTTProtocol_handlePubcomps(void* pack, int sock)
{
    Pubcomp* pubcomp = (Pubcomp*)pack;
    Clients* client = NULL;
    int rc = TCPSOCKET_COMPLETE;
    
    FUNC_ENTRY;
    client = (Clients*)(ListFindItem(bstate->clients, &sock, clientSocketCompare)->content);
    Log(LOG_PROTOCOL, 19, NULL, sock, client->clientID, pubcomp->msgId);
    
    /* look for the message by message id in the records of outbound messages for this client */
    if (ListFindItem(client->outboundMsgs, &(pubcomp->msgId), messageIDCompare) == NULL)
    {
        if (pubcomp->header.bits.dup == 0)
            Log(TRACE_MIN, 3, NULL, "PUBCOMP", client->clientID, pubcomp->msgId);
    }
    else
    {
        Messages* m = (Messages*)(client->outboundMsgs->current->content);
        if (m->qos != 2)
            Log(TRACE_MIN, 4, NULL, "PUBCOMP", client->clientID, pubcomp->msgId, m->qos);
        else
        {
            if (m->nextMessageType != PUBCOMP)
                Log(TRACE_MIN, 5, NULL, "PUBCOMP", client->clientID, pubcomp->msgId);
            else
            {
                Log(TRACE_MIN, 6, NULL, "PUBCOMP", client->clientID, pubcomp->msgId);
#if !defined(NO_PERSISTENCE)
                rc = MQTTPersistence_remove(client, PERSISTENCE_PUBLISH_SENT, m->qos, pubcomp->msgId);
#endif
                MQTTProtocol_removePublication(m->publish);
                ListRemove(client->outboundMsgs, m);
                (++state.msgs_sent);
            }
        }
    }
    free(pack);
    FUNC_EXIT_RC(rc);
    return rc;
}

void MQTTProtocol_keepalive(time_t now)
{
    ListElement* current = NULL;
    
    FUNC_ENTRY;
    ListNextElement(bstate->clients, &current);
    while (current)
    {
        Clients* client =	(Clients*)(current->content);
        ListNextElement(bstate->clients, &current); 
        if (client->connected && client->keepAliveInterval > 0 &&
            (difftime(now, client->net.lastSent) >= client->keepAliveInterval ||
             difftime(now, client->net.lastReceived) >= client->keepAliveInterval))
        {
            if (client->ping_outstanding == 0)
            {
                if (Socket_noPendingWrites(client->net.socket))
                {
                    if (MQTTPacket_send_pingreq(&client->net, client->clientID) != TCPSOCKET_COMPLETE)
                    {
                        Log(TRACE_PROTOCOL, -1, "Error sending PINGREQ for client %s on socket %d, disconnecting", client->clientID, client->net.socket);
                        MQTTProtocol_closeSession(client, 1);
                    }
                    else
                    {
                        client->net.lastSent = now;
                        client->ping_outstanding = 1;
                    }
                }
            }
            else
            {
                Log(TRACE_PROTOCOL, -1, "PINGRESP not received in keepalive interval for client %s on socket %d, disconnecting", client->clientID, client->net.socket);
                MQTTProtocol_closeSession(client, 1);
            }
        }
    }
    FUNC_EXIT;
}

void MQTTProtocol_retry(time_t now, int doRetry, int regardless)
{
    ListElement* current = NULL;
    
    FUNC_ENTRY;
    ListNextElement(bstate->clients, &current);
    /* look through the outbound message list of each client, checking to see if a retry is necessary */
    while (current)
    {
        Clients* client = (Clients*)(current->content);
        ListNextElement(bstate->clients, &current);
        if (client->connected == 0)
            continue;
        if (client->good == 0)
        {
            MQTTProtocol_closeSession(client, 1);
            continue;
        }
        if (Socket_noPendingWrites(client->net.socket) == 0)
            continue;
        if (doRetry)
            MQTTProtocol_retries(now, client, regardless);
    }
    FUNC_EXIT;
}

void MQTTProtocol_freeClient(Clients* client)
{
    FUNC_ENTRY;
    /* free up pending message lists here, and any other allocated data */
    MQTTProtocol_freeMessageList(client->outboundMsgs);
    MQTTProtocol_freeMessageList(client->inboundMsgs);
    ListFree(client->messageQueue);
    free(client->clientID);
    if (client->will)
    {
        free(client->will->msg);
        free(client->will->topic);
        free(client->will);
    }
#if defined(OPENSSL)
    if (client->sslopts)
    {
        if (client->sslopts->trustStore)
            free((void*)client->sslopts->trustStore);
        if (client->sslopts->keyStore)
            free((void*)client->sslopts->keyStore);
        if (client->sslopts->privateKey)
            free((void*)client->sslopts->privateKey);
        if (client->sslopts->privateKeyPassword)
            free((void*)client->sslopts->privateKeyPassword);
        if (client->sslopts->enabledCipherSuites)
            free((void*)client->sslopts->enabledCipherSuites);
        free(client->sslopts);
    }
#endif
    /* don't free the client structure itself... this is done elsewhere */
    FUNC_EXIT;
}

void MQTTProtocol_emptyMessageList(List* msgList)
{
    ListElement* current = NULL;
    
    FUNC_ENTRY;
    while (ListNextElement(msgList, &current))
    {
        Messages* m = (Messages*)(current->content);
        MQTTProtocol_removePublication(m->publish);
    }
    ListEmpty(msgList);
    FUNC_EXIT;
}

void MQTTProtocol_freeMessageList(List* msgList)
{
    FUNC_ENTRY;
    MQTTProtocol_emptyMessageList(msgList);
    ListFree(msgList);
    FUNC_EXIT;
}

char* MQTTStrncpy(char *dest, const char *src, size_t dest_size)
{
    size_t count = dest_size;
    char *temp = dest;
    
    FUNC_ENTRY;
    if (dest_size < strlen(src))
        Log(TRACE_MIN, -1, "the src string is truncated");
    
    /* We must copy only the first (dest_size - 1) bytes */
    while (count > 1 && (*temp++ = *src++))
        count--;
    
    *temp = '\0';
    
    FUNC_EXIT;
    return dest;
}

char* MQTTStrdup(const char* src)
{
    size_t mlen = strlen(src) + 1;
    char* temp = malloc(mlen);
    MQTTStrncpy(temp, src, mlen);
    return temp;
}

bool messageIDCompare(void const* a, void const* b)
{
    Messages* msg = (Messages*)a;
    return (msg->msgid == *(int*)b) ? true : false;
}

#pragma mark - Private functionality

void MQTTProtocol_storeQoS0(Clients* pubclient, Publish* publish)
{
	size_t len;
	pending_write* pw = NULL;

	FUNC_ENTRY;
	/* store the publication until the write is finished */
	pw = malloc(sizeof(pending_write));
	Log(TRACE_MIN, 12, NULL);
	pw->p = MQTTProtocol_storePublication(publish, &len);
	pw->socket = pubclient->net.socket;
	ListAppend(&(state.pending_writes), pw, sizeof(pending_write)+len);
	/* we don't copy QoS 0 messages unless we have to, so now we have to tell the socket buffer where
	the saved copy is */
	if (SocketBuffer_updateWrite(pw->socket, pw->p->topic, pw->p->payload) == NULL)
		Log(LOG_SEVERE, 0, "Error updating write");
	FUNC_EXIT;
}

/*!
 *  @abstract Utility function to start a new publish exchange.
 *
 *  @param pubclient the client to send the publication to
 *  @param publish the publication data
 *  @param qos the MQTT QoS to use
 *  @param retained boolean - whether to set the MQTT retained flag
 *  @return the completion code
 */
int MQTTProtocol_startPublishCommon(Clients* pubclient, Publish* publish, int qos, int retained)
{
	int rc = TCPSOCKET_COMPLETE;

	FUNC_ENTRY;
	rc = MQTTPacket_send_publish(publish, 0, qos, retained, &pubclient->net, pubclient->clientID);
	if (qos == 0 && rc == TCPSOCKET_INTERRUPTED)
		MQTTProtocol_storeQoS0(pubclient, publish);
	FUNC_EXIT_RC(rc);
	return rc;
}

/*!
 *  @abstract MQTT retry processing per client.
 *
 *  @param now current time.
 *  @param client - the client to which to apply the retry processing.
 *  @param regardless boolean - retry packets regardless of retry interval (used on reconnect).
 */
void MQTTProtocol_retries(time_t now, Clients* client, int regardless)
{
	ListElement* outcurrent = NULL;

	FUNC_ENTRY;

	if (!regardless && client->retryInterval <= 0) /* 0 or -ive retryInterval turns off retry except on reconnect */
		goto exit;

	while (client && ListNextElement(client->outboundMsgs, &outcurrent) &&
		   client->connected && client->good &&        /* client is connected and has no errors */
		   Socket_noPendingWrites(client->net.socket)) /* there aren't any previous packets still stacked up on the socket */
	{
		Messages* m = (Messages*)(outcurrent->content);
		if (regardless || difftime(now, m->lastTouch) > max(client->retryInterval, 10))
		{
			if (m->qos == 1 || (m->qos == 2 && m->nextMessageType == PUBREC))
			{
				Publish publish;
				int rc;

				Log(TRACE_MIN, 7, NULL, "PUBLISH", client->clientID, client->net.socket, m->msgid);
				publish.msgId = m->msgid;
				publish.topic = m->publish->topic;
				publish.payload = m->publish->payload;
				publish.payloadlen = m->publish->payloadlen;
				rc = MQTTPacket_send_publish(&publish, 1, m->qos, m->retain, &client->net, client->clientID);
				if (rc == SOCKET_ERROR)
				{
					client->good = 0;
					Log(TRACE_PROTOCOL, 29, NULL, client->clientID, client->net.socket,
												Socket_getpeer(client->net.socket));
					MQTTProtocol_closeSession(client, 1);
					client = NULL;
				}
				else
				{
					if (m->qos == 0 && rc == TCPSOCKET_INTERRUPTED)
						MQTTProtocol_storeQoS0(client, &publish);
					time(&(m->lastTouch));
				}
			}
			else if (m->qos && m->nextMessageType == PUBCOMP)
			{
				Log(TRACE_MIN, 7, NULL, "PUBREL", client->clientID, client->net.socket, m->msgid);
				if (MQTTPacket_send_pubrel(m->msgid, 0, &client->net, client->clientID) != TCPSOCKET_COMPLETE)
				{
					client->good = 0;
					Log(TRACE_PROTOCOL, 29, NULL, client->clientID, client->net.socket,
							Socket_getpeer(client->net.socket));
					MQTTProtocol_closeSession(client, 1);
					client = NULL;
				}
				else
					time(&(m->lastTouch));
			}
			/* break; why not do all retries at once? */
		}
	}
exit:
	FUNC_EXIT;
}
