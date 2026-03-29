#ifndef PTZDEVICE_H
#define PTZDEVICE_H

#include "FreeRTOS.h"
#include <Arduino.h>
#include <array>

struct ControlData
{
	uint8_t pan;
	uint8_t tilt;
	uint8_t zoom;
	uint8_t speed;
	bool operator==(const ControlData &rhs)
	{
		return pan == rhs.pan && tilt == rhs.tilt && zoom == rhs.zoom && speed == rhs.speed;
	}
	bool operator!=(const ControlData &rhs)
	{
		return pan != rhs.pan || tilt != rhs.tilt || zoom != rhs.zoom || speed != rhs.speed;
	}
};

class PTZDevice
{
public:
	virtual ~PTZDevice() {}
	virtual void init() = 0;
	// maybe needs "queue camera change" fn to finish old connec7on (stop camera etc) before change
	virtual void change_camera_ip(IPAddress ip, uint16_t port = 80) = 0;
	virtual void send_data(const ControlData &data) = 0;
	virtual void save_preset(uint8_t key, uint8_t preset_speed) = 0;
	virtual void detach_camera() = 0;
	virtual void run_preset(uint8_t preset) = 0;
};

class DeviceHandler
{
public:
	std::array<PTZDevice *, 3> _impls;

	DeviceHandler(std::array<PTZDevice *, 3> p_impls) : _impls(p_impls) {}

	void set_impl_index(size_t idx)
	{
		if (idx < _impls.size())
		{
			if (_impls[idx] != nullptr)
			{
				_impl = _impls[idx];
			}
		}
	}

	void set_impl(PTZDevice *p_impl)
	{
		_impl = p_impl;
	}

	void detach_camera()
	{
		_impl->detach_camera();
	}
	void init()
	{
		_impl->init();
	}

	void change_camera_ip(IPAddress ip, uint16_t port)
	{
		_impl->change_camera_ip(ip, port);
	}

	void send_data(const ControlData &data)
	{
		_impl->send_data(data);
	}
	void save_preset(uint8_t key, uint8_t preset_speed)
	{
		_impl->save_preset(key, preset_speed);
	}

	void run_preset(uint8_t preset)
	{
		_impl->run_preset(preset);
	}

private:
	PTZDevice *_impl;
};

#endif