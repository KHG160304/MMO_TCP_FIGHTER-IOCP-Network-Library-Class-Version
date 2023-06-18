#pragma once
#ifndef __I_SESSION_EVENT_HANDLER_H__
#define	__I_SESSION_EVENT_HANDLER_H__

typedef	unsigned long long SESSIONID;

class SerializationBuffer;

class ISessionEventHandler
{
public:
	virtual void OnRecv(SESSIONID, SerializationBuffer&) = 0;
	virtual void OnAccept(SESSIONID) = 0;
	virtual void OnRelease(SESSIONID) = 0;
};
#endif