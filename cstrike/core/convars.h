#pragma once

#include <cstdint>

class CConVar;

namespace CONVAR
{
	// dump convars to file
	bool Dump(const wchar_t* wszFileName);
	// setup convars
	bool Setup();
	// Native-safe exact-name lookup used by Linux features and the runtime dump.
	CConVar* Find(const char* name);
	// Type-checked value access. Linux additionally validates the mapped value
	// storage and confirms every write by reading it back.
	bool ReadBool(CConVar* convar, bool& value);
	bool ReadFloat(CConVar* convar, float& value);
	bool ReadInt32(CConVar* convar, std::int32_t& value);
	bool WriteBool(CConVar* convar, bool value, bool* readback = nullptr);
	bool WriteFloat(CConVar* convar, float value, float* readback = nullptr);
	bool WriteInt32(CConVar* convar, std::int32_t value, std::int32_t* readback = nullptr);

	inline CConVar* m_pitch = nullptr;
	inline CConVar* m_yaw = nullptr;
	inline CConVar* sensitivity = nullptr;

	inline CConVar* game_type = nullptr;
	inline CConVar* game_mode = nullptr;

	inline CConVar* mp_teammates_are_enemies = nullptr;

	inline CConVar* sv_autobunnyhopping = nullptr;

	inline CConVar* cam_idealdist = nullptr;

	inline CConVar* cam_collision = nullptr;

	inline CConVar* cam_snapto = nullptr;
	inline CConVar* cl_thirdperson = nullptr;

	inline CConVar* c_thirdpersonshoulder = nullptr;

	inline CConVar* c_thirdpersonshoulderaimdist = nullptr;

	inline CConVar* c_thirdpersonshoulderdist = nullptr;

	inline CConVar* c_thirdpersonshoulderheight = nullptr;

	inline CConVar* c_thirdpersonshoulderoffset = nullptr;

	inline CConVar* cl_interpolate = nullptr;

	inline CConVar* cl_interp_ratio = nullptr;
}
