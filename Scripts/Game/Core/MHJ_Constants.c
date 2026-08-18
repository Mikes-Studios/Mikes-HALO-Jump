//------------------------------------------------------------------------------------------------
//! Shared HALO numbers. Altitudes are metres above terrain at the drop.
//! Freefall is quadratic drag plus body lift. Canopy is a ram-air wing in the wind.
//------------------------------------------------------------------------------------------------
class MHJ_Constants
{
	static const float JUMP_ALT_MIN = 200;
	static const float JUMP_ALT_MAX = 3000;
	static const float JUMP_ALT_DEFAULT = 1000;

	static const float OPEN_ALT_MIN = 80;
	static const float OPEN_ALT_MAX = 1500;
	static const float OPEN_ALT_DEFAULT = 250;

	static const float OPEN_MARGIN = 50;
	static const float LAND_AGL = 1.6;
	static const float FLARE_AGL = 20;
	static const float FLARE_END_AGL = 2.5;
	static const float LAND_HARD_SINK = 8;
	static const float LAND_FATAL_SINK = 18;
	static const float LAND_CARRY_MIN = 2.5;
	static const float LAND_CARRY_FULL = 10;
	static const float LAND_CARRY_ANALOG_MIN = 0.55;
	static const float LAND_CARRY_ANALOG_MAX = 1.15;
	static const float LAND_CARRY_TIME = 1.0;
	static const float LAND_CARRY_INPUT_BREAK = 0.2;

	static const float GRAVITY = 9.81;
	static const float MASS = 200;
	static const float AIR_DENSITY_SL = 1.325;
	static const float BODY_AREA = 0.55;

	static const float FREEFALL_TERMINAL = 54;
	static const float FREEFALL_SLOW_TERMINAL = 45;
	static const float FREEFALL_TRACK_TERMINAL = 58;
	static const float FREEFALL_TRACK_LIFT = 1.15;
	static const float FREEFALL_TRACK_BASE = 0.42;
	static const float FREEFALL_SLIDE = 0.45;
	static const float FREEFALL_TURN_RATE = 1.35;
	static const float FREEFALL_TURN_ALIGN = 2.8;
	static const float FREEFALL_BANK_MAX = 34;
	//! Prone fall is already belly-down. These are extra tuck / arch on that pose.
	static const float FREEFALL_PITCH_ARCH = 0;
	static const float FREEFALL_PITCH_TRACK = -16;
	static const float FREEFALL_PITCH_SLOW = 12;
	static const int FREEFALL_INPUT_HOLD_MS = 250;
	//! Body and canopy path never go past this. 90° straight down gimbal-flips look and lift.
	static const float DIVE_ANGLE_MAX = 85;
	static const float FREEFALL_EXIT_TIME = 1.15;
	static const float FREEFALL_MAX_SPEED = 90;

	static const float CANOPY_AREA = 23;
	static const float CANOPY_STALL_SPEED = 6.4;
	static const float CANOPY_CRUISE_TAS = 11;
	static const float CANOPY_MAX_TAS = 52;
	static const float CANOPY_CL_TRIM = 0.55;
	static const float CANOPY_CD_TRIM = 0.15;
	static const float CANOPY_CL_DIVE_DROP = 0.50;
	static const float CANOPY_CL_BRAKE_ADD = 0.48;
	static const float CANOPY_CD_DIVE_ADD = -0.12;
	static const float CANOPY_CD_BRAKE_ADD = 0.18;
	static const float CANOPY_CL_ENERGY = 0.62;
	//! Hands-off high TAS keeps a dive-like CD so a released dive coasts instead of slamming to trim.
	static const float CANOPY_CD_ENERGY_DROP = 0.10;
	static const float CANOPY_DIVE_ACCEL = 14;
	static const float CANOPY_MAX_CLIMB = 2.2;
	static const float CANOPY_TURN_RATE = 0.95;
	static const float CANOPY_HEADING_LERP = 2.5;
	static const float CANOPY_BANK_MAX = 32;
	static const float CANOPY_BANK_INERTIA = 0.38;
	static const float CANOPY_PITCH_CRUISE = 0;
	static const float CANOPY_PITCH_DIVE = -85;
	static const float CANOPY_PITCH_FLARE = 16;
	static const float CANOPY_PITCH_SNATCH = 0;
	static const float CANOPY_PITCH_INERTIA = 0.45;
	//! Path angles must match visual pitch in cruise/dive so the wing flies where it points.
	static const float CANOPY_PATH_CRUISE = -18;
	static const float CANOPY_PATH_DIVE = -85;
	static const float CANOPY_PATH_FLARE = -2;
	static const float CANOPY_PATH_ALIGN = 2.4;
	static const float CANOPY_PATH_ALIGN_DIVE = 2.4;
	static const float CANOPY_PATH_ALIGN_FLARE = 2.0;
	static const float CANOPY_PATH_INERTIA = 0.42;
	static const float CANOPY_PATH_IN_MAX = 70;
	//! Excess TAS toward cruise after the path has flattened. 0.16 is ~6 s; 1.1 dumped a dive in one second.
	static const float CANOPY_RELEASE_BLEED = 0.16;
	static const float CANOPY_HANG_LENGTH = 4.2;
	static const float CANOPY_HANG_INERTIA = 0.42;
	static const float CANOPY_STRETCH_TIME = 0.32;
	static const float CANOPY_INFLATE_TIME = 1.05;
	static const float CANOPY_OPEN_TIME = 2.4;
	static const float CANOPY_SNATCH_OVERSHOOT = 1.18;
	static const float CANOPY_PITCH_IN_TIME = 0.82;
	static const float CANOPY_PITCH_IN_MAX = 3;
	static const float CANOPY_DIVE_IN_TIME = 1.42;
	static const float CANOPY_DIVE_IN_MAX = 1.8;

	static const float WIND_REF_HEIGHT = 10;
	static const float WIND_SHEAR_EXP = 0.11;
	static const float WIND_SHEAR_MAX = 2.2;
	static const float WIND_GUST = 0.12;
	static const float WIND_TURB = 0.08;

	static const float STEER_FILTER_TIME = 0.2;
	static const float STEER_FILTER_MAX = 90;

	static const float WORLD_SIZE_FALLBACK = 12800;

	static const string CANOPY_PREFAB = "{C4E8A27B1F906D90}Prefabs/MHJ_DeployedCanopy.et";
	static const float CANOPY_MESH_OFFSET_Y = -1.45;
	static const float CANOPY_SEAT_OFFSET_Y = 0.674;
	static const string CANOPY_VEHICLE_AGR = "{66618A6A119CAD93}Assets/Vehicles/Wheeled/workspaces/Vehicles_Wheeled_Graph.agr";
	static const string CANOPY_VEHICLE_ASI = "{AA88048D862E1368}Assets/Vehicles/Wheeled/workspaces/Player_Wheeled_Ural.asi";
	static const string CANOPY_VEHICLE_BINDING = "Vehicle";
}

//------------------------------------------------------------------------------------------------
enum MHJ_EHaloPhase
{
	FREEFALL,
	CANOPY,
	LANDED
}
