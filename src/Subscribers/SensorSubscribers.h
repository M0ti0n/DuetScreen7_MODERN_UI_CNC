#pragma once

#include "Subscribers/Subscribers.h"

class SensorSubscribers : public SubscriberMap
{
  public:
	SensorSubscribers()
	{
		addSubscriber("sensors:analog^", nullAnalogSensor);
		addSubscriber("sensors:analog^:lastReading", analogSensorReading);
		addSubscriber("sensors:analog^:name", analogSensorName);
		addSubscriber("sensors:endstops^", nullEndstop);
		addSubscriber("sensors:endstops^:triggered", endstopTriggered);
		addSubscriber("sensors:probes^:value^", probeValue);

		addArrayEndSubscriber("sensors:analog^", analogSensorArrayEnd);
		addArrayEndSubscriber("sensors:endstops^", endstopArrayEnd);
	}

  private:
	static bool nullAnalogSensor(Comm::JsonDecoder* decoder, const char* data, const size_t indices[]);
	static bool analogSensorReading(Comm::JsonDecoder* decoder, const float& data, const size_t indices[]);
	static bool analogSensorName(Comm::JsonDecoder* decoder, const char* data, const size_t indices[]);
	static bool nullEndstop(Comm::JsonDecoder* decoder, const char* data, const size_t indices[]);
	static bool endstopTriggered(Comm::JsonDecoder* decoder, const bool& data, const size_t indices[]);
	static bool probeValue(Comm::JsonDecoder* decoder, const int32_t& data, const size_t indices[]);

	static bool analogSensorArrayEnd(Comm::JsonDecoder* decoder, const size_t indices[]);
	static bool endstopArrayEnd(Comm::JsonDecoder* decoder, const size_t indices[]);
};
