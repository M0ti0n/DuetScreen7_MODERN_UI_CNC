#include "Debug.h"

#include "ObjectModel/Sensor.h"
#include "SensorSubscribers.h"
#include "UI/Core/Model.h"

bool SensorSubscribers::nullAnalogSensor(Comm::JsonDecoder* decoder, const char* data, const size_t indices[])
{
	ZoneScoped;
	UNUSED(decoder);
	UNUSED(data);
	OM::RemoveAnalogSensor(indices[0], false);
	Model::get().post<EventType::AnalogSensorData>();
	return true;
}

bool SensorSubscribers::analogSensorReading(Comm::JsonDecoder* decoder, const float& data, const size_t indices[])
{
	ZoneScoped;
	UNUSED(decoder);
	if (!OM::UpdateAnalogSensorReading(indices[0], data))
	{
		LOG_ERROR("Failed to update analog sensor {:d} reading to {:g}", indices[0], data);
		return false;
	}
	return true;
}

bool SensorSubscribers::analogSensorName(Comm::JsonDecoder* decoder, const char* data, const size_t indices[])
{
	ZoneScoped;
	UNUSED(decoder);
	if (!OM::UpdateAnalogSensorName(indices[0], data))
	{
		LOG_ERROR("Failed to update analog sensor {:d} name to {:s}", indices[0], data);
		return false;
	}
	return true;
}

bool SensorSubscribers::nullEndstop(Comm::JsonDecoder* decoder, const char* data, const size_t indices[])
{
	ZoneScoped;
	UNUSED(decoder);
	UNUSED(data);
	OM::RemoveEndstop(indices[0], false);
	Model::get().post<EventType::EndstopData>();
	return true;
}

bool SensorSubscribers::endstopTriggered(Comm::JsonDecoder* decoder, const bool& data, const size_t indices[])
{
	ZoneScoped;
	UNUSED(decoder);
	if (!OM::UpdateEndstopTriggered(indices[0], data))
	{
		LOG_ERROR("Failed to update endstop {:d} triggered to {:d}", indices[0], data);
		return false;
	}
	return true;
}

// Only the first reading of each probe (value[0]); ProbeData is posted when it changes
bool SensorSubscribers::probeValue(Comm::JsonDecoder* decoder, const int32_t& data, const size_t indices[])
{
	ZoneScoped;
	UNUSED(decoder);
	if (indices[1] == 0 && OM::UpdateProbeValue(indices[0], data))
	{
		Model::get().post<EventType::ProbeData>();
	}
	return true;
}

bool SensorSubscribers::analogSensorArrayEnd(Comm::JsonDecoder* decoder, const size_t indices[])
{
	ZoneScoped;
	UNUSED(decoder);
	if (OM::RemoveAnalogSensor(indices[0], true))
	{
	}
	Model::get().post<EventType::AnalogSensorData>();
	return true;
}

bool SensorSubscribers::endstopArrayEnd(Comm::JsonDecoder* decoder, const size_t indices[])
{
	ZoneScoped;
	UNUSED(decoder);
	if (OM::RemoveEndstop(indices[0], true))
	{
	}
	Model::get().post<EventType::EndstopData>();
	return true;
}
