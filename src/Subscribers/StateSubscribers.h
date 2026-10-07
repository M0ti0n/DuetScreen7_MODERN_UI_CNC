#pragma once

#include "Subscribers/Subscribers.h"

class StateSubscribers : public SubscriberMap
{
  public:
	StateSubscribers()
	{
		addSubscriber("network:name", networkName);
		addSubscriber("network:interfaces^:actualIP", networkActualIP);
		addSubscriber("state:status", status);
		addSubscriber("state:currentTool", currentTool);
		addSubscriber("state:nextTool", nextTool);
		addSubscriber("state:messageBox", nullMessageBox);
		addSubscriber("state:messageBox:axisControls", messageBoxAxisControls);
		addSubscriber("state:messageBox:message", messageBoxMessage);
		addSubscriber("state:messageBox:mode", messageBoxMode);
		addSubscriber("state:messageBox:seq", messageBoxSeq);
		addSubscriber("state:messageBox:timeout", messageBoxTimeout);
		addSubscriber("state:messageBox:title", messageBoxTitle);
		addSubscriber("state:messageBox:min", messageBoxMin);
		addSubscriber("state:messageBox:max", messageBoxMax);
		addSubscriber("state:messageBox:default", messageBoxDefault);
		addSubscriber("state:messageBox:cancelButton", messageBoxCancelButton);
		addSubscriber("state:messageBox:choices^", messageBoxChoices);
		addSubscriber("state:time", time);
		addSubscriber("state:thisInput", inputChannel);
	}

  private:
	static bool networkName(Comm::JsonDecoder* decoder, const char* data, const size_t indices[]);
	static bool networkActualIP(Comm::JsonDecoder* decoder, const char* data, const size_t indices[]);
	static bool status(Comm::JsonDecoder* decoder, const char* data, const size_t indices[]);
	static bool currentTool(Comm::JsonDecoder* decoder, const int32_t& data, const size_t indices[]);
	static bool nextTool(Comm::JsonDecoder* decoder, const int32_t& data, const size_t indices[]);
	static bool nullMessageBox(Comm::JsonDecoder* decoder, const char* data, const size_t indices[]);
	static bool messageBoxAxisControls(Comm::JsonDecoder* decoder, const uint32_t& data, const size_t indices[]);
	static bool messageBoxMessage(Comm::JsonDecoder* decoder, const char* data, const size_t indices[]);
	static bool messageBoxMode(Comm::JsonDecoder* decoder, const int32_t& data, const size_t indices[]);
	static bool messageBoxSeq(Comm::JsonDecoder* decoder, const uint32_t& data, const size_t indices[]);
	static bool messageBoxTimeout(Comm::JsonDecoder* decoder, const uint32_t& data, const size_t indices[]);
	static bool messageBoxTitle(Comm::JsonDecoder* decoder, const char* data, const size_t indices[]);
	static bool messageBoxMin(Comm::JsonDecoder* decoder, const char* data, const size_t indices[]);
	static bool messageBoxMax(Comm::JsonDecoder* decoder, const char* data, const size_t indices[]);
	static bool messageBoxDefault(Comm::JsonDecoder* decoder, const char* data, const size_t indices[]);
	static bool messageBoxCancelButton(Comm::JsonDecoder* decoder, const bool& data, const size_t indices[]);
	static bool messageBoxChoices(Comm::JsonDecoder* decoder, const char* data, const size_t indices[]);
	static bool time(Comm::JsonDecoder* decoder, const char* data, const size_t indices[]);
	static bool inputChannel(Comm::JsonDecoder* decoder, const uint32_t& data, const size_t indices[]);
};
